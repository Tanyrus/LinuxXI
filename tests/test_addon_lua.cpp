#include "addon_lua.h"
#include "test.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace linuxxi;

namespace
{
    constexpr double kSmallHeapKb = 1000;
    constexpr double kBigHeapKb   = 2560000; // 2.5 GB, as xiui's heap reached in a long session
    constexpr double kCopyKb      = 10;      // one packet copy, 10,035 bytes, rounded up

    // Stand-ins for what Ashita gives an addon. A second registration under one name is refused, as both would run.
    constexpr const char* kStage = R"(
local registered, raised, stepped = {}, nil, nil
ashita = { events = {
    register = function(e, a, f) local k = e .. '/' .. a; if registered[k] then error('registered twice: ' .. k) end; registered[k] = f end,
    unregister = function(e, a) registered[e .. '/' .. a] = nil end } }
AshitaCore = { GetPluginManager = function() return { RaiseEvent = function(_, name, bytes) raised = name .. '|' .. string.char(unpack(bytes)) end } end }
collectgarbage = function(what, n) if what == 'count' then return HEAP end; if what == 'step' then stepped = n end; return 0 end
package.loaded.settings = SETTINGS
)";

    // What is left registered, what was raised, and what each handler left steps the collector by.
    constexpr const char* kTell = R"(
local keys = {}
for k in pairs(registered) do keys[#keys + 1] = k end
table.sort(keys)
for _, k in ipairs(keys) do stepped = nil; registered[k](); print('registered ' .. k .. ' steps ' .. tostring(stepped)) end
if raised then print('raised ' .. raised) end
)";

    std::string Replaced(std::string text, const std::string& what, const std::string& with)
    {
        text.replace(text.find(what), what.size(), with);
        return text;
    }

    // Runs code after the stand-ins under LuaJIT and returns what it printed, errors included.
    std::string Run(const std::string& code, double heapKb, bool settings)
    {
        const std::string stage = Replaced(Replaced(kStage, "HEAP", std::to_string(heapKb)), "SETTINGS", settings ? "{}" : "nil");
        const std::filesystem::path file = std::filesystem::temp_directory_path() / ("linuxxi-test-" + std::to_string(getpid()) + ".lua");
        std::ofstream(file) << stage << code << '\n' << kTell;
        std::string out;
        if (FILE* pipe = popen(("luajit " + file.string() + " 2>&1").c_str(), "r"))
        {
            char line[512];
            while (std::fgets(line, sizeof line, pipe) != nullptr)
                out += line;
            pclose(pipe);
        }
        std::filesystem::remove(file);
        return out;
    }

    std::string Registered(const std::string& code, bool settings = false)
    {
        std::string out = Run(code, kSmallHeapKb, settings), keys;
        for (size_t at = out.find("registered "); at != std::string::npos; at = out.find("registered ", at + 1))
            keys += out.substr(at + 11, out.find(' ', at + 11) - at - 11) + ";";
        return out.find("error") != std::string::npos ? out : keys;
    }

    double Steps(const std::string& code, double heapKb)
    {
        const std::string out = Run(code, heapKb, false);
        const size_t at       = out.find(" steps ");
        return at == std::string::npos ? -1 : std::stod(out.substr(at + 7));
    }
}

TEST(an_addons_own_packet_handlers_are_read_from_its_source)
{
    // As craftbook, castdelay and clock register theirs.
    const Handlers craftbook = HandlersIn("ashita.events.register('packet_in', 'packet_in_cb', function (e)\nlocal settings = require('settings');");
    CHECK(craftbook.packetIn && !craftbook.packetOut && craftbook.settings);
    const Handlers castdelay = HandlersIn("ashita.events.register(\"packet_out\", \"castdelay_out\", function (e) end)");
    CHECK(!castdelay.packetIn && castdelay.packetOut && !castdelay.settings);
    const Handlers clock = HandlersIn("local settings = require 'settings'\nashita.events.register('d3d_present', 'p', f)");
    CHECK(!clock.packetIn && !clock.packetOut && clock.settings && clock.Any());
    CHECK(!HandlersIn("ashita.events.register('command', 'c', f) -- packet_in mentioned").Any());
}

TEST(only_a_registration_outside_comments_counts_as_a_handler)
{
    CHECK(!HandlersIn("ashita.events.unregister('packet_in', 'x')").packetIn);
    CHECK(!HandlersIn("-- ashita.events.register('packet_out', 'x', f)").packetOut);
    CHECK(!HandlersIn("--[[ old:\nashita.events.register('packet_in', 'x', f)\n]]").packetIn);
    CHECK(!HandlersIn("--[==[ ashita.events.register('packet_in', 'x', f) ]==]").packetIn);
    CHECK(!HandlersIn("-- local settings = require('settings')").settings);
    CHECK(!HandlersIn("ashita.events.register('command', 'c', f) -- the 'packet_in' event").Any());
    // A string may hold what reads as a comment.
    CHECK(HandlersIn("local s = '--' ashita.events.register('packet_in', 'x', f)").packetIn);
    CHECK(HandlersIn("local s = [[--]] ashita.events.register('packet_in', 'x', f)").packetIn);
    CHECK(HandlersIn("local s = \"a\\\"--\" ashita.events.register('packet_out', 'x', f)").packetOut);
}

TEST(the_hook_registers_one_stepping_handler_for_each_direction_the_addon_has)
{
    CHECK_EQ(Registered(HookCode(Handlers{true, false, false})), std::string("packet_in/linuxxi_in;"));
    CHECK_EQ(Registered(HookCode(Handlers{false, true, false})), std::string("packet_out/linuxxi_out;"));
    CHECK_EQ(Registered(HookCode(Handlers{true, true, false})), std::string("packet_in/linuxxi_in;packet_out/linuxxi_out;"));
    // The settings library registers a packet_in handler of its own only in an addon that has loaded it.
    CHECK_EQ(Registered(HookCode(Handlers{false, false, true}), true), std::string("packet_in/linuxxi_in;"));
    CHECK_EQ(Registered(HookCode(Handlers{false, false, true}), false), std::string());
}

TEST(the_hook_sent_again_replaces_itself_and_the_unhook_takes_out_all_of_it)
{
    const std::string hook = HookCode(Handlers{true, true, false});
    CHECK_EQ(Registered(hook + "\n" + hook), std::string("packet_in/linuxxi_in;packet_out/linuxxi_out;"));
    CHECK_EQ(Registered(hook + "\n" + UnhookCode()), std::string());
    CHECK(hook.find('"') == std::string::npos && UnhookCode().find('"') == std::string::npos); // /addon exec takes the code in quotes
}

TEST(a_handler_steps_the_collector_by_a_copy_or_by_a_share_of_a_big_heap)
{
    const std::string hook = HookCode(Handlers{true, false, false});
    CHECK_EQ(Steps(hook, kSmallHeapKb), kCopyKb);
    const double big = Steps(hook, kBigHeapKb);
    CHECK(big > kCopyKb && big < kBigHeapKb);
}

TEST(the_code_asks_the_addon_for_its_lua_memory_in_whole_kilobytes)
{
    const std::string code = AddonCode("craftbook", Handlers{}).value_or("");
    CHECK(code.find('"') == std::string::npos);
    CHECK_EQ(Registered(code), std::string());
    const std::string out = Run(code, 9800.75, false);
    const std::string mark = std::string("raised ") + kLuaEvent + "|";
    const size_t at        = out.find(mark);
    CHECK(at != std::string::npos);
    const std::string bytes = at == std::string::npos ? "" : out.substr(at + mark.size(), out.find('\n', at) - at - mark.size());
    const std::optional<LuaReport> report = LuaReportOf(bytes.data(), static_cast<uint32_t>(bytes.size()));
    CHECK(report.has_value() && report->addon == "craftbook" && report->kilobytes == 9800);
}

TEST(a_not_loaded_answer_names_its_addon_through_the_colour_codes)
{
    // Built as the SDK's Chat.h builds a line (its header, then Color1 runs); the live game's had codes around the name.
    const std::string line = "\x1E\x51[\x1E\x06" "Addons" "\x1E\x51]\x1E\x01 " "\x1E\x44" "Failed to execute Lua string within addon '" "\x1E\x01"
                             "\x1E\x02" "radgamepad" "\x1E\x01" "\x1E\x44" "'. Reason: Addon is not loaded." "\x1E\x01";
    CHECK(NotLoadedAddon(line) == std::optional<std::string>("radgamepad"));
    // Color2 runs begin with 0x1F.
    CHECK(NotLoadedAddon("Failed to execute Lua string within addon '\x1F\x02radgamepad\x1E\x01'. Reason: Addon is not loaded.") ==
          std::optional<std::string>("radgamepad"));
    CHECK(NotLoadedAddon("[Addons] Loaded addon: craftbook version: 1.2 - by: Frankie") == std::nullopt);
    // A Lua error in a loaded addon comes in the same words up to its reason.
    CHECK(NotLoadedAddon("Failed to execute Lua string within addon 'craftbook'. Reason: [string]:1: attempt to call a nil value") ==
          std::nullopt);
}

// /addon exec takes the name up to a space, and the report carries it in Lua's single quotes.
TEST(an_addon_whose_name_the_command_cannot_carry_is_sent_nothing)
{
    for (const char* name : {"odd'name", "two words", ""})
        CHECK(!AddonCode(name, Handlers{true, true, true}).has_value());
    CHECK(AddonCode("Boussole_2", Handlers{true, false, false}).has_value());
}

TEST(an_addon_with_packet_handlers_is_sent_the_hook_and_the_ask_for_its_memory)
{
    const std::string out = Run(AddonCode("craftbook", Handlers{true, false, false}).value_or(""), kSmallHeapKb, false);
    CHECK(out.find("registered packet_in/linuxxi_in") != std::string::npos);
    CHECK(out.find(std::string("raised ") + kLuaEvent + "|craftbook ") != std::string::npos);
}

TEST(a_lua_report_is_read_back_as_its_addon_and_kilobytes)
{
    const std::string text = "craftbook 9800";
    const std::optional<LuaReport> report = LuaReportOf(text.data(), static_cast<uint32_t>(text.size()));
    CHECK(report.has_value() && report->addon == "craftbook" && report->kilobytes == 9800);
    // The last is past what 32 bits hold.
    for (const std::string bad : {"craftbook", "craftbook x", " 12", "craftbook 12 more", "craftbook 4294967296"})
        CHECK(!LuaReportOf(bad.data(), static_cast<uint32_t>(bad.size())).has_value());
    CHECK(!LuaReportOf(nullptr, 4).has_value());
}
