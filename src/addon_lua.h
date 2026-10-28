#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace linuxxi
{
    // What an addon's sources say it handles. Ashita's settings library registers a packet_in handler in every addon
    // that requires it, which only the addon itself can tell (package.loaded.settings).
    struct Handlers
    {
        bool packetIn = false, packetOut = false, settings = false;
        bool Any() const { return packetIn || packetOut || settings; }
    };

    Handlers HandlersIn(std::string_view source);

    // Lua for /addon exec (no double quotes: the command takes the code in them): for each direction the addon
    // handles, a handler stepping its collector by a copy's 10 KB or a share of a big heap, unregistered first.
    std::string HookCode(const Handlers& handlers);
    std::string UnhookCode();

    // The plugin event each addon raises with "<addon> <kilobytes>", its Lua heap.
    constexpr const char* kLuaEvent = "linuxxi_lua";

    // What an addon is sent each turn, by its folder name: its hook and the ask for its Lua memory, which raises
    // kLuaEvent; nothing for a name /addon exec and Lua's quotes cannot carry.
    std::optional<std::string> AddonCode(const std::string& addon, const Handlers& handlers);

    struct LuaReport
    {
        std::string addon;
        uint32_t kilobytes = 0;
    };
    std::optional<LuaReport> LuaReportOf(const void* data, uint32_t size);

    // The addon /addon exec found not loaded, from the line Ashita answers with (its colour codes left in).
    std::optional<std::string> NotLoadedAddon(std::string_view line);
}
