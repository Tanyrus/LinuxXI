#include "Ashita.h"

#include "d3d_grace.h"
#include "command.h"
#include "imports.h"
#include "addon_lua.h"
#include "pool.h"
#include "report.h"
#include "sandbox_compare.h"
#include "shot_folders.h"
#include "srw_guard.h"
#include "turns.h"
#include "figures.h"
#include "memory_query.h"

#include <algorithm>
#include <atomic>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace linuxxi;

    constexpr const char* kName        = "LinuxXI";
    constexpr const char* kAuthor      = "tanyrus";
    constexpr const char* kDescription = "Fixes the memory, zone change, locale and screenshot problems the game has under Linux.";
    constexpr double kVersion          = 1.0;
    constexpr int32_t kChatMode        = 1; // the chat mode Ashita's own plugins print in
    constexpr const char* kCommand     = "/linuxxi";
    constexpr uint32_t kHostCheckFrames = 60; // an addon host loaded again comes with imports of its own
    // Before the addon host, so a "not loaded" answer is blanked before an addon such as logs writes it down.
    constexpr int32_t kPriority        = -1000;
    constexpr const char* kHeapDll     = "api-ms-win-crt-heap-l1-1-0.dll";
    constexpr const char* kStringDll   = "api-ms-win-crt-string-l1-1-0.dll";
    constexpr std::string_view kLuaFile = ".lua";
    constexpr const char* kSandbox     = "sandbox.dll";
    constexpr const char* kCrt         = "ucrtbase.dll";
    constexpr const char* kPolcore     = "polcore.dll";
    constexpr const char* kWcsnicmp    = "_wcsnicmp";
    constexpr const char* kWcsicmp     = "_wcsicmp";
    // The addon host under the names launchers give its builds.
    constexpr const char* kHosts[] = {"addons.dll", "addons_default.dll", "addons_linux.dll"};
    constexpr ULONGLONG kReportMs     = static_cast<ULONGLONG>(kReportSeconds * 1000);
    constexpr size_t kAddonsPerLine   = 6;

    using MallocFn    = void*(__cdecl*)(size_t);
    using FreeFn      = void(__cdecl*)(void*);
    using ReallocFn   = void*(__cdecl*)(void*, size_t);
    using MsizeFn     = size_t(__cdecl*)(void*);
    using CallocFn    = void*(__cdecl*)(size_t, size_t);
    using WcsnicmpFn  = int(__cdecl*)(const wchar_t*, const wchar_t*, size_t);
    using WcsicmpFn   = int(__cdecl*)(const wchar_t*, const wchar_t*);
    using SetLocaleFn = char*(__cdecl*)(int, const char*);

    // All of it outlives the plugin: copies handed out stay with the host after an unload, and the module is pinned
    // so the hooks that take them back stay too.
    SRWLOCK g_Lock = SRWLOCK_INIT;
    Pool* g_Pool   = nullptr;
    std::atomic<uintptr_t> g_Low{0}, g_High{0};
    std::atomic<bool> g_PoolOn{true};
    MallocFn g_Malloc       = nullptr;
    FreeFn g_Free           = nullptr;
    ReallocFn g_Realloc     = nullptr;
    MsizeFn g_Msize         = nullptr;
    CallocFn g_Calloc       = nullptr;
    std::atomic<bool> g_LocaleOn{true};
    WcsnicmpFn g_Wcsnicmp   = nullptr;
    WcsicmpFn g_Wcsicmp     = nullptr;

    bool InPool(const void* block)
    {
        const auto at = reinterpret_cast<uintptr_t>(block);
        return at >= g_Low.load(std::memory_order_acquire) && at < g_High.load(std::memory_order_acquire);
    }

    // Under g_Lock, for InPool's reads without it.
    void PublishBounds()
    {
        g_Low.store(g_Pool->Low(), std::memory_order_release);
        g_High.store(g_Pool->High(), std::memory_order_release);
    }

    // Null while the pool is switched off, or when the pool could not grow, so the copy comes from the heap: nothing
    // may throw into the host.
    void* TakeCopy()
    {
        if (!g_PoolOn.load(std::memory_order_relaxed)) return nullptr;
        try
        {
            const ExclusiveGuard guard(g_Lock);
            void* block = g_Pool->Take();
            PublishBounds();
            return block;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    bool GiveCopy(void* block)
    {
        if (!InPool(block)) return false;
        const ExclusiveGuard guard(g_Lock);
        const bool ours = g_Pool->Give(block);
        PublishBounds();
        return ours;
    }

    bool OwnsCopy(const void* block)
    {
        if (!InPool(block)) return false;
        const SharedGuard guard(g_Lock);
        return g_Pool->Owns(block);
    }

    void* __cdecl HookMalloc(size_t size)
    {
        if (size == kCopyBytes)
            if (void* block = TakeCopy()) return block;
        return g_Malloc(size);
    }

    void __cdecl HookFree(void* block)
    {
        if (!GiveCopy(block)) g_Free(block);
    }

    void* __cdecl HookRealloc(void* block, size_t size)
    {
        if (block == nullptr) return HookMalloc(size);
        if (!OwnsCopy(block)) return g_Realloc(block, size);
        if (size == 0)
        {
            GiveCopy(block);
            return nullptr;
        }
        void* moved = HookMalloc(size);
        if (moved == nullptr) return nullptr;
        std::memcpy(moved, block, size < kCopyBytes ? size : kCopyBytes);
        GiveCopy(block);
        return moved;
    }

    size_t __cdecl HookMsize(void* block) { return OwnsCopy(block) ? kCopyBytes : g_Msize(block); }

    void* __cdecl HookCalloc(size_t count, size_t size)
    {
        if (IsCopyRequest(count, size))
            if (void* block = TakeCopy())
            {
                std::memset(block, 0, kCopyBytes);
                return block;
            }
        return g_Calloc(count, size);
    }

    // Points one import at its hook, keeping the first real target before the slot is written, as another thread may
    // call through it the moment it changes; false when the host imports no such function or it could not be patched.
    template <typename Fn>
    bool Hook(HMODULE host, const char* dll, const char* function, Fn hook, Fn& original)
    {
        void** slot = ImportSlot(host, dll, function);
        if (slot == nullptr) return false;
        if (*slot == reinterpret_cast<void*>(hook)) return true;
        if (original == nullptr) original = reinterpret_cast<Fn>(*slot);
        return WriteSlot(slot, reinterpret_cast<void*>(hook));
    }

    // What frees a copy goes in before what hands one out, so no copy can reach a free that is not ours, and malloc
    // before calloc, as a realloc of a copy calls it. HeapAlloc is left alone: every copy came by malloc when measured.
    bool HookHost(HMODULE host)
    {
        const bool frees = Hook(host, kHeapDll, "free", &HookFree, g_Free) && Hook(host, kHeapDll, "realloc", &HookRealloc, g_Realloc) &&
                           Hook(host, kHeapDll, "_msize", &HookMsize, g_Msize);
        if (!frees || !Hook(host, kHeapDll, "malloc", &HookMalloc, g_Malloc)) return false;
        Hook(host, kHeapDll, "calloc", &HookCalloc, g_Calloc);
        return true;
    }

    int __cdecl HookWcsnicmp(const wchar_t* a, const wchar_t* b, size_t count) { return CompareNoCase(a, b, count); }
    int __cdecl HookWcsicmp(const wchar_t* a, const wchar_t* b) { return CompareNoCase(a, b, SIZE_MAX); }

    enum class Compares
    {
        NoSandbox,
        Failed, // they could not both be pointed, and neither was
        Pointed,
    };

    // Sandbox's registry hooks compare key names with these two, which under Wine in any locale but C open a registry
    // key of their own, back into the hook until the stack runs out. Points both at C's (own) or the CRT's, or neither.
    Compares PointSandboxCompares(bool own)
    {
        const HMODULE sandbox = GetModuleHandleA(kSandbox);
        if (sandbox == nullptr) return Compares::NoSandbox;
        void** wcsnicmp = ImportSlot(sandbox, kStringDll, kWcsnicmp);
        void** wcsicmp  = ImportSlot(sandbox, kStringDll, kWcsicmp);
        if (wcsnicmp == nullptr || wcsicmp == nullptr) return Compares::Failed;
        if (g_Wcsnicmp == nullptr) g_Wcsnicmp = reinterpret_cast<WcsnicmpFn>(*wcsnicmp);
        if (g_Wcsicmp == nullptr) g_Wcsicmp = reinterpret_cast<WcsicmpFn>(*wcsicmp);
        void* const was = *wcsnicmp;
        if (!WriteSlot(wcsnicmp, own ? reinterpret_cast<void*>(&HookWcsnicmp) : reinterpret_cast<void*>(g_Wcsnicmp))) return Compares::Failed;
        if (WriteSlot(wcsicmp, own ? reinterpret_cast<void*>(&HookWcsicmp) : reinterpret_cast<void*>(g_Wcsicmp))) return Compares::Pointed;
        WriteSlot(wcsnicmp, was);
        return Compares::Failed;
    }

    // The locale of the game's C runtime, which sandbox, the addons' Lua and most DLLs share, as setlocale names it.
    std::string GameLocale()
    {
        const HMODULE crt     = GetModuleHandleA(kCrt);
        const auto setLocale  = crt != nullptr ? reinterpret_cast<SetLocaleFn>(reinterpret_cast<void*>(GetProcAddress(crt, "setlocale"))) : nullptr;
        const char* name      = setLocale != nullptr ? setLocale(LC_ALL, nullptr) : nullptr;
        return name != nullptr ? name : "unknown";
    }

    struct Target
    {
        std::string name, code;
        bool stepped = false; // it has packet handlers, so its collector is stepped
    };

    template <typename Visit>
    void ForEachEntry(const std::string& folder, Visit visit)
    {
        WIN32_FIND_DATAA found{};
        const HANDLE search = FindFirstFileA((folder + "*").c_str(), &found);
        if (search == INVALID_HANDLE_VALUE) return;
        do
        {
            const std::string name = found.cFileName;
            if (name != "." && name != "..") visit(name, (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        } while (FindNextFileA(search, &found));
        FindClose(search);
    }

    void ReadSources(const std::string& folder, std::string& into)
    {
        ForEachEntry(folder, [&](const std::string& name, bool isFolder) {
            if (isFolder)
                ReadSources(folder + name + "\\", into);
            else if (name.size() > kLuaFile.size() && _stricmp(name.c_str() + name.size() - kLuaFile.size(), kLuaFile.data()) == 0)
            {
                std::ifstream file(folder + name, std::ios::binary);
                std::ostringstream text;
                text << file.rdbuf();
                into += text.str();
                into += '\n';
            }
        });
    }

    // Each installed addon (addons/NAME/NAME.lua): it reports its Lua memory, and one whose sources show a packet handler
    // or the settings library has its collector stepped too.
    std::vector<Target> TargetsIn(const std::string& root)
    {
        std::vector<Target> targets;
        ForEachEntry(root, [&](const std::string& name, bool isFolder) {
            const std::string folder = root + name + "\\";
            if (!isFolder || GetFileAttributesA((folder + name + std::string(kLuaFile)).c_str()) == INVALID_FILE_ATTRIBUTES) return;
            std::string sources;
            ReadSources(folder, sources);
            const Handlers handlers = HandlersIn(sources);
            if (const std::optional<std::string> code = AddonCode(name, handlers)) targets.push_back(Target{name, *code, handlers.Any()});
        });
        return targets;
    }

    std::optional<MemoryReport> MemoryNow(bool wine)
    {
        const std::optional<uint64_t> mapped = VirtualSizeNow();
        if (!mapped) return std::nullopt;
        return MemoryOf(FreeSpaceOf(RegionAt), *mapped, wine);
    }

    struct ShotsMade
    {
        size_t made   = 0;
        size_t failed = 0;
    };

    // Makes the screenshot folders every PlayOnline home lacks; none when the viewer's folder is not found.
    std::optional<ShotsMade> MakeShotFolders()
    {
        const HMODULE polcore = GetModuleHandleA(kPolcore);
        if (polcore == nullptr) return std::nullopt;
        char path[MAX_PATH];
        const DWORD length = GetModuleFileNameA(polcore, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return std::nullopt;
        const std::optional<std::string> pub = ViewerPub(std::string(path, length));
        if (!pub || GetFileAttributesA(pub->c_str()) == INVALID_FILE_ATTRIBUTES) return std::nullopt;
        std::vector<std::string> names;
        ForEachEntry(*pub + "\\", [&](const std::string& name, bool isFolder) {
            if (isFolder) names.push_back(name);
        });
        ShotsMade shots;
        for (const std::string& folder : ShotFolders(*pub, names))
        {
            std::error_code error;
            if (std::filesystem::create_directories(folder, error))
                ++shots.made;
            else if (error)
                ++shots.failed;
        }
        return shots;
    }

    class LinuxXI final : public IPlugin
    {
        IAshitaCore* m_Core = nullptr;
        uint32_t m_Frame    = 0;
        std::string m_Hosts;
        std::vector<Target> m_Targets;
        AddonTurns m_Turns{0};
        IDirect3DDevice8* m_Device = nullptr;
        bool m_GraceStarted = false; // at the first Present: DXVK's device takes no locks, and loading is on another thread
        Compares m_Compares = Compares::NoSandbox;
        bool m_Wine = false; // asked once, at load: it opens a registry key, which sandbox's hook compares
        std::optional<ShotsMade> m_Shots;
        ULONGLONG m_ReportAt = 0;
        SRWLOCK m_Lock       = SRWLOCK_INIT; // m_Hosts, m_Turns and m_Lua: Ashita's command threads use them too
        std::map<std::string, LuaEntry> m_Lua;

        uint32_t SteppedCount() const
        {
            return static_cast<uint32_t>(std::count_if(m_Targets.begin(), m_Targets.end(), [](const Target& t) { return t.stepped; }));
        }

        Report ReportNow()
        {
            FixState fixes;
            {
                const SharedGuard guard(g_Lock);
                fixes.pool = g_Pool->Stats();
            }
            fixes.grace        = GraceNow();
            fixes.poolOn       = g_PoolOn;
            fixes.localeActive = m_Compares == Compares::Pointed;
            fixes.localeOn     = g_LocaleOn;
            fixes.stepped      = SteppedCount();
            const SharedGuard guard(m_Lock);
            fixes.poolHooked = !m_Hosts.empty();
            fixes.notLoaded  = static_cast<uint32_t>(m_Turns.NotLoadedCount());
            return BuildReport(fixes, m_Lua, GetTickCount64(), m_Turns.StaleMs());
        }

        void Exec(const std::string& addon, const std::string& code)
        {
            m_Core->GetChatManager()->QueueCommand(static_cast<int32_t>(Ashita::CommandMode::AshitaParse), ("/addon exec " + addon + " \"" + code + "\"").c_str());
        }

        void SendNext()
        {
            std::optional<size_t> next;
            {
                const ExclusiveGuard guard(m_Lock);
                next = m_Turns.Next(GetTickCount64());
            }
            if (next) Exec(m_Targets[*next].name, m_Targets[*next].code);
        }

        void HookHosts()
        {
            std::string hooked;
            for (const char* name : kHosts)
                if (const HMODULE host = GetModuleHandleA(name); host != nullptr && HookHost(host))
                    hooked += (hooked.empty() ? "" : ", ") + std::string(name);
            const ExclusiveGuard guard(m_Lock);
            m_Hosts = hooked;
        }

        // The held copies by packet id, and how many copies each packet has, to a file in Ashita's logs: a copy the host
        // never frees shows here once every addon that had it is unloaded.
        void Dump()
        {
            std::vector<std::pair<uint32_t, uint32_t>> words; // each held copy's first two
            {
                const SharedGuard guard(g_Lock);
                for (const void* block : g_Pool->Held())
                {
                    const auto* packet = reinterpret_cast<const uint8_t*>(CopyData(reinterpret_cast<uintptr_t>(block)));
                    uint32_t head = 0, body = 0;
                    std::memcpy(&head, packet, sizeof head);
                    std::memcpy(&body, packet + sizeof head, sizeof body);
                    words.emplace_back(head, body);
                }
            }
            std::map<uint32_t, uint32_t> byId;
            std::map<std::pair<uint32_t, uint32_t>, uint32_t> copiesOf;
            for (const auto& pair : words)
            {
                ++byId[PacketIdOf(pair.first)];
                ++copiesOf[pair];
            }
            std::map<uint32_t, uint32_t> packetsWith;
            for (const auto& [packet, copies] : copiesOf)
                ++packetsWith[copies];
            const std::string path = std::string(m_Core->GetInstallPath()) + "logs\\" + std::string(kName) + "-dump.txt";
            FILE* out = std::fopen(path.c_str(), "w");
            if (out == nullptr)
            {
                Print("Could not write " + path + ".");
                return;
            }
            std::fprintf(out, "%u held\nby packet id:\n", static_cast<unsigned>(words.size()));
            for (const auto& [id, count] : byId)
                std::fprintf(out, "  0x%03X %u\n", id, count);
            std::fprintf(out, "copies a packet: packets\n");
            for (const auto& [copies, packets] : packetsWith)
                std::fprintf(out, "  %u: %u\n", copies, packets);
            std::fclose(out);
            Print("Wrote " + path);
        }

        void Print(const std::string& message)
        {
            const std::string line = Ashita::Chat::Header(kName) + Ashita::Chat::Message(message);
            m_Core->GetChatManager()->Write(kChatMode, false, line.c_str());
        }

    public:
        const char* GetName(void) const override { return kName; }
        const char* GetAuthor(void) const override { return kAuthor; }
        const char* GetDescription(void) const override { return kDescription; }
        const char* GetLink(void) const override { return ""; }
        double GetVersion(void) const override { return kVersion; }
        uint32_t GetFlags(void) const override
        {
            return static_cast<uint32_t>(Ashita::PluginFlags::UseDirect3D) | static_cast<uint32_t>(Ashita::PluginFlags::UseCommands) |
                   static_cast<uint32_t>(Ashita::PluginFlags::UseText) | static_cast<uint32_t>(Ashita::PluginFlags::UsePluginEvents);
        }
        int32_t GetPriority(void) const override { return kPriority; }

        // An exception that reached Ashita would cross into code built by another compiler, so none leaves a callback.
        bool Initialize(IAshitaCore* core, ILogManager* logger, const uint32_t id) override
        {
            UNREFERENCED_PARAMETER(logger);
            UNREFERENCED_PARAMETER(id);
            m_Core = core;
            HMODULE self = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                    reinterpret_cast<LPCWSTR>(&HookFree), &self))
                return false;
            try
            {
                m_Compares = PointSandboxCompares(g_LocaleOn); // the switch outlives an unload, as the module stays
                m_Wine     = UnderWine();
                m_Shots    = MakeShotFolders();
                if (g_Pool == nullptr)
                    g_Pool = new Pool(
                        [](size_t bytes) {
                            return static_cast<uint8_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
                        },
                        [](uint8_t* base) { VirtualFree(base, 0, MEM_RELEASE); });
                HookHosts();
                m_Targets = TargetsIn(std::string(core->GetInstallPath()) + "addons\\");
                m_Turns   = AddonTurns(m_Targets.size());
            }
            catch (...)
            {
                return false;
            }
            return true;
        }

        // The pool and its hooks stay (the module is pinned); the addons' stepping handlers go, and so does holding
        // releases back, as nothing would let them go.
        void Release(void) override
        {
            StopGrace();
            try
            {
                std::vector<std::string> loaded;
                {
                    const SharedGuard guard(m_Lock);
                    for (size_t i = 0; i < m_Targets.size(); ++i)
                        if (m_Turns.Loaded(i)) loaded.push_back(m_Targets[i].name);
                }
                for (const std::string& name : loaded)
                    Exec(name, UnhookCode());
            }
            catch (...)
            {
            }
        }

        bool Direct3DInitialize(IDirect3DDevice8* device) override
        {
            m_Device = device;
            return true;
        }

        void Direct3DPresent(const RECT* source, const RECT* dest, HWND window, const RGNDATA* dirty) override
        {
            UNREFERENCED_PARAMETER(source);
            UNREFERENCED_PARAMETER(dest);
            UNREFERENCED_PARAMETER(window);
            UNREFERENCED_PARAMETER(dirty);
            try
            {
                if (!m_GraceStarted) StartGrace(m_Device);
                m_GraceStarted = true;
                if (++m_Frame % kHostCheckFrames == 0) HookHosts();
                SendNext();
                GraceFrame();
                if (const ULONGLONG now = GetTickCount64(); now >= m_ReportAt)
                {
                    m_ReportAt = now + kReportMs;
                    const Report report = ReportNow();
                    m_Core->GetPluginManager()->RaiseEvent(kReportEvent, &report, sizeof report);
                }
            }
            catch (...)
            {
            }
        }

        bool HandleIncomingText(int32_t mode, bool indent, const char* message, int32_t* modifiedMode, bool* modifiedIndent,
            char* modifiedMessage, bool injected, bool blocked) override
        {
            UNREFERENCED_PARAMETER(mode);
            UNREFERENCED_PARAMETER(indent);
            UNREFERENCED_PARAMETER(modifiedMode);
            UNREFERENCED_PARAMETER(modifiedIndent);
            UNREFERENCED_PARAMETER(injected);
            UNREFERENCED_PARAMETER(blocked);
            try
            {
                const std::optional<std::string> addon = message != nullptr ? NotLoadedAddon(message) : std::nullopt;
                if (!addon) return false;
                const auto target = std::find_if(m_Targets.begin(), m_Targets.end(), [&](const Target& t) { return t.name == *addon; });
                if (target == m_Targets.end()) return false;
                {
                    const ExclusiveGuard guard(m_Lock);
                    m_Turns.NotLoaded(static_cast<size_t>(target - m_Targets.begin()), GetTickCount64());
                    m_Lua.erase(*addon);
                }
                if (modifiedMessage != nullptr) modifiedMessage[0] = '\0';
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        void HandleEvent(const char* eventName, const void* eventData, const uint32_t eventSize) override
        {
            try
            {
                if (eventName == nullptr || std::strcmp(eventName, kLuaEvent) != 0) return;
                const std::optional<LuaReport> report = LuaReportOf(eventData, eventSize);
                if (!report) return;
                const ExclusiveGuard guard(m_Lock);
                m_Lua[report->addon] = LuaEntry{report->kilobytes, GetTickCount64()};
            }
            catch (...)
            {
            }
        }

    private:
        void PrintMemory()
        {
            if (const std::optional<MemoryReport> memory = MemoryNow(m_Wine))
                Print("Memory: " + Grouped(memory->usedMb) + " MB used, " + Grouped(memory->leftMb) + " MB left of " + Grouped(memory->ceilingMb) + " MB.");
            else
                Print("Memory: the space used could not be read.");
        }

        void PrintStatus()
        {
            PrintMemory();
            const PoolStats stats = [] {
                const SharedGuard guard(g_Lock);
                return g_Pool->Stats();
            }();
            std::string hosts;
            size_t missing = 0;
            {
                const SharedGuard guard(m_Lock);
                hosts   = m_Hosts;
                missing = m_Turns.NotLoadedCount();
            }
            Print(std::string("Pool") + (g_PoolOn ? "" : " (switched off)") + ": " + Grouped(stats.held) + " copies held now, " +
                  Grouped(stats.peakHeld) + " at most; " + Grouped(stats.chunks) + " chunks of " + std::to_string(kChunkMb) + " MB now, " +
                  Grouped(stats.peakChunks) + " at most, " + Grouped(stats.chunksReturned) + " given back.");
            const std::string stray = stats.strayGives == 0 ? "" : ", " + Grouped(stats.strayGives) + " stray frees kept from the heap";
            Print(Grouped(stats.served) + " copies served, " + Grouped(stats.refused) + " refused" + stray + "; hooked: " +
                  (hosts.empty() ? "nothing" : hosts) + ".");
            Print(std::to_string(SteppedCount()) + " addons with packet handlers stepped, of " + std::to_string(m_Targets.size()) +
                  " installed; " + std::to_string(missing) + " not loaded.");
            const GraceState grace = GraceNow();
            if (const std::optional<std::string> unused = GraceUnused())
                Print(*unused);
            else
                Print(std::string("Grace") + (grace.on ? "" : " (switched off)") + ": " + Grouped(grace.heldBack) +
                      " last releases held back so far, " + Grouped(grace.waiting) + " waiting.");
            if (const std::optional<std::string> unused = LocaleUnused())
                Print(*unused);
            else
                Print(std::string("Locale") + (g_LocaleOn ? "" : " (switched off)") + ": " + kSandbox + "'s registry compares " +
                      (g_LocaleOn ? "keep to the C locale" : "follow the game's locale") + "; the game's locale is " + GameLocale() + ".");
            PrintShots();
        }

        void PrintShots()
        {
            if (!m_Shots)
            {
                Print(std::string("Screenshots: not in use, as PlayOnline's viewer folder was not found beside ") + kPolcore + ".");
                return;
            }
            const std::string failed = m_Shots->failed == 0 ? "" : ", " + std::to_string(m_Shots->failed) + " could not be made";
            Print("Screenshots: " + std::to_string(m_Shots->made) + " folders made at load" + failed + "; the rest were there.");
        }

        // Why a fix does nothing in this game, when it does nothing; the status and the switches say the same.
        std::optional<std::string> GraceUnused() const
        {
            if (!GraceNow().active) return std::string("Grace: not in use, as Direct3D's releases could not be hooked.");
            return std::nullopt;
        }

        std::optional<std::string> LocaleUnused() const
        {
            if (m_Compares == Compares::NoSandbox) return std::string("Locale: not in use, as ") + kSandbox + " is not in this game.";
            if (m_Compares == Compares::Failed) return std::string("Locale: not in use, as ") + kSandbox + "'s compares could not be pointed.";
            return std::nullopt;
        }

        std::optional<std::string> Unused(Fix fix) const
        {
            switch (fix)
            {
                case Fix::Pool: return std::nullopt;
                case Fix::Grace: return GraceUnused();
                case Fix::Locale: return LocaleUnused();
            }
            return std::nullopt;
        }

        void Switch(Fix fix, bool on)
        {
            if (const std::optional<std::string> unused = Unused(fix))
            {
                Print(*unused);
                return;
            }
            if (fix == Fix::Pool)
                g_PoolOn = on;
            else if (fix == Fix::Grace)
                SetGraceOn(on);
            else if (PointSandboxCompares(on) != Compares::Pointed)
            {
                Print(std::string("Could not point ") + kSandbox + "'s compares; the locale fix is as it was.");
                return;
            }
            else
                g_LocaleOn = on;
            Print("Switched the " + std::string(FixWord(fix)) + (on ? " on." : " off."));
        }

    public:
        bool HandleCommand(int32_t mode, const char* command, bool injected) override
        {
            UNREFERENCED_PARAMETER(mode);
            UNREFERENCED_PARAMETER(injected);
            if (command == nullptr) return false;
            const size_t length = std::strlen(kCommand);
            if (_strnicmp(command, kCommand, length) != 0 || (command[length] != '\0' && command[length] != ' ')) return false;
            try
            {
                const Command parsed = ParseCommand(command + length);
                switch (parsed.kind)
                {
                    case CommandKind::Status: PrintStatus(); break;
                    case CommandKind::Dump: Dump(); break;
                    case CommandKind::Memory:
                        PrintMemory();
                        for (const std::string& line : LuaLines(ReportNow(), kAddonsPerLine))
                            Print(line);
                        break;
                    case CommandKind::Switch: Switch(parsed.fix, parsed.on); break;
                    case CommandKind::Usage: Print(Usage(kCommand)); break;
                }
            }
            catch (...)
            {
            }
            return true;
        }
    };
}

extern "C" __declspec(dllexport) double __stdcall expGetInterfaceVersion(void)
{
    return ASHITA_INTERFACE_VERSION;
}

extern "C" __declspec(dllexport) IPlugin* __stdcall expCreatePlugin(const char* args)
{
    UNREFERENCED_PARAMETER(args);
    try
    {
        return new LinuxXI();
    }
    catch (...)
    {
        return nullptr;
    }
}

extern "C" __declspec(dllexport) void __stdcall expDestroyPlugin(void* instance)
{
    delete static_cast<IPlugin*>(instance);
}
