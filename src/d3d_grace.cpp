// DXVK's state blocks keep plain pointers: Ashita's ImGui binds its buffers over a game buffer released while bound,
// which frees it, then puts the saved state back, binding the freed one. Held back here, it is still alive then.
#include "d3d_grace.h"

#include "grace.h"
#include "imports.h"
#include "srw_guard.h"

#include "Ashita.h"

#include <atomic>
#include <vector>

namespace linuxxi
{
    namespace
    {
        constexpr size_t kAddRefSlot  = 1;
        constexpr size_t kReleaseSlot = 2;
        constexpr size_t kTestCooperativeLevelSlot = 3; // the device's, after IUnknown's three
        constexpr UINT kStreams = 16, kStages = 8;
        constexpr UINT kProbeBytes = 64;

        using RefFn         = ULONG(STDMETHODCALLTYPE*)(IUnknown*);
        using CooperativeFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice8*);

        struct Kind
        {
            RefFn addRef = nullptr, release = nullptr;
            void** slot = nullptr; // where its Release was written, while it is ours
        };
        enum KindIndex
        {
            kTexture,
            kIndexBuffer,
            kVertexBuffer,
            kCubeTexture,
            kVolumeTexture,
            kKinds
        };

        // Outlive the plugin: the method tables stay patched, so the module is pinned and these stay with it.
        SRWLOCK g_Lock             = SRWLOCK_INIT;
        IDirect3DDevice8* g_Device = nullptr;
        Kind g_Kinds[kKinds];
        bool g_Active          = false;
        std::atomic<bool> g_On{true};
        std::atomic<bool> g_Running{false}; // only while loaded: after an unload no frame would let them go
        std::atomic<bool> g_Lost{false};
        CooperativeFn g_TestCooperativeLevel = nullptr;
        std::atomic<uint32_t> g_Frame{0};
        GraceLedger* g_Ledger  = nullptr;

        // The caller's reference is swapped for one of ours when it is the last, and ours goes once the frames have passed.
        template <KindIndex K>
        ULONG STDMETHODCALLTYPE GraceRelease(IUnknown* self)
        {
            const Kind& kind = g_Kinds[K];
            if (!g_On.load(std::memory_order_relaxed) || !g_Running.load(std::memory_order_relaxed) || g_Lost.load(std::memory_order_relaxed))
                return kind.release(self);
            const ULONG before = kind.addRef(self) - 1;
            if (before > 1)
            {
                kind.release(self);
                return kind.release(self);
            }
            try
            {
                const ExclusiveGuard guard(g_Lock);
                g_Ledger->Hold(reinterpret_cast<uintptr_t>(self), g_Frame.load(std::memory_order_relaxed), K);
            }
            catch (...)
            {
                kind.release(self); // the ledger could not grow: ours and the caller's go now, nothing throws into the game
                return kind.release(self);
            }
            kind.release(self);
            return 0;
        }

        void LetGo(const std::vector<Held>& held)
        {
            for (const Held& h : held)
                g_Kinds[h.kind].release(reinterpret_cast<IUnknown*>(h.object));
        }

        // A lost device is to be Reset, which fails while a default-pool resource lives, and the game's index buffers
        // are of that pool: what is held goes, and nothing is held again until the device is well.
        HRESULT STDMETHODCALLTYPE GraceTestCooperativeLevel(IDirect3DDevice8* device)
        {
            const HRESULT result = g_TestCooperativeLevel(device);
            g_Lost.store(FAILED(result), std::memory_order_relaxed);
            if (FAILED(result))
            {
                std::vector<Held> all;
                {
                    const ExclusiveGuard guard(g_Lock);
                    all = g_Ledger->TakeAll();
                }
                LetGo(all);
            }
            return result;
        }

        // What the device has bound now: its index buffer, vertex streams and textures.
        std::vector<uintptr_t> Bound()
        {
            std::vector<uintptr_t> bound;
            IDirect3DIndexBuffer8* indices = nullptr;
            UINT base = 0;
            if (SUCCEEDED(g_Device->GetIndices(&indices, &base)) && indices != nullptr)
            {
                bound.push_back(reinterpret_cast<uintptr_t>(indices));
                indices->Release();
            }
            for (UINT stream = 0; stream < kStreams; ++stream)
            {
                IDirect3DVertexBuffer8* vertices = nullptr;
                UINT stride = 0;
                if (SUCCEEDED(g_Device->GetStreamSource(stream, &vertices, &stride)) && vertices != nullptr)
                {
                    bound.push_back(reinterpret_cast<uintptr_t>(vertices));
                    vertices->Release();
                }
            }
            for (UINT stage = 0; stage < kStages; ++stage)
            {
                IDirect3DBaseTexture8* texture = nullptr;
                if (SUCCEEDED(g_Device->GetTexture(stage, &texture)) && texture != nullptr)
                {
                    bound.push_back(reinterpret_cast<uintptr_t>(texture));
                    texture->Release();
                }
            }
            return bound;
        }

        // Patches Release in the probe's method table and lets the probe go through the release it had; false when the
        // table could not be written.
        template <KindIndex K>
        bool Hook(void* probe)
        {
            Kind& kind   = g_Kinds[K];
            void** table = *reinterpret_cast<void***>(probe);
            // A wrapper may share one table between kinds (index and vertex buffers have the same methods).
            for (const Kind& other : g_Kinds)
                if (other.slot == &table[kReleaseSlot])
                {
                    kind.addRef  = other.addRef;
                    kind.release = other.release;
                    kind.release(reinterpret_cast<IUnknown*>(probe));
                    return true;
                }
            kind.addRef  = reinterpret_cast<RefFn>(table[kAddRefSlot]);
            kind.release = reinterpret_cast<RefFn>(table[kReleaseSlot]);
            if (WriteSlot(&table[kReleaseSlot], reinterpret_cast<void*>(&GraceRelease<K>))) kind.slot = &table[kReleaseSlot];
            kind.release(reinterpret_cast<IUnknown*>(probe));
            return kind.slot != nullptr;
        }

        // Puts back every Release written, so the grace covers all its kinds or none.
        void Unhook()
        {
            for (Kind& kind : g_Kinds)
                if (kind.slot != nullptr && WriteSlot(kind.slot, reinterpret_cast<void*>(kind.release))) kind.slot = nullptr;
        }
    }

    void StartGrace(IDirect3DDevice8* device)
    {
        if (g_Active || device == nullptr)
        {
            g_Running = g_Active;
            return;
        }
        void** cooperative = *reinterpret_cast<void***>(device) + kTestCooperativeLevelSlot;
        if (*cooperative != reinterpret_cast<void*>(&GraceTestCooperativeLevel))
        {
            g_TestCooperativeLevel = reinterpret_cast<CooperativeFn>(*cooperative);
            if (!WriteSlot(cooperative, reinterpret_cast<void*>(&GraceTestCooperativeLevel))) return;
        }
        g_Device = device;
        if (g_Ledger == nullptr) g_Ledger = new GraceLedger();
        IDirect3DTexture8* texture       = nullptr;
        IDirect3DIndexBuffer8* indices   = nullptr;
        IDirect3DVertexBuffer8* vertices = nullptr;
        bool all = SUCCEEDED(device->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture)) && texture != nullptr &&
                   Hook<kTexture>(texture);
        all = all && SUCCEEDED(device->CreateIndexBuffer(kProbeBytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &indices)) &&
              indices != nullptr && Hook<kIndexBuffer>(indices);
        all = all && SUCCEEDED(device->CreateVertexBuffer(kProbeBytes, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vertices)) &&
              vertices != nullptr && Hook<kVertexBuffer>(vertices);
        // Kinds the device cannot make, the game cannot use either; one it makes is covered with the rest or none is.
        IDirect3DCubeTexture8* cube     = nullptr;
        IDirect3DVolumeTexture8* volume = nullptr;
        if (all && SUCCEEDED(device->CreateCubeTexture(1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube)) && cube != nullptr)
            all = Hook<kCubeTexture>(cube);
        if (all && SUCCEEDED(device->CreateVolumeTexture(1, 1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &volume)) && volume != nullptr)
            all = Hook<kVolumeTexture>(volume);
        if (!all) Unhook();
        g_Active  = all;
        g_Running = g_Active;
    }

    void StopGrace() { g_Running = false; }

    void GraceFrame()
    {
        if (!g_Active) return;
        const uint32_t frame = g_Frame.fetch_add(1, std::memory_order_relaxed) + 1;
        {
            const SharedGuard guard(g_Lock);
            if (g_Ledger->Waiting() == 0) return;
        }
        const std::vector<uintptr_t> bound = Bound();
        std::vector<Held> due;
        {
            const ExclusiveGuard guard(g_Lock);
            due = g_Ledger->Due(frame, bound);
        }
        LetGo(due);
    }

    void SetGraceOn(bool on) { g_On.store(on, std::memory_order_relaxed); }

    GraceState GraceNow()
    {
        GraceState state;
        state.active = g_Active;
        state.on     = g_On.load(std::memory_order_relaxed);
        if (g_Ledger == nullptr) return state;
        const SharedGuard guard(g_Lock);
        state.heldBack = g_Ledger->HeldBack();
        state.waiting  = static_cast<uint32_t>(g_Ledger->Waiting());
        return state;
    }
}
