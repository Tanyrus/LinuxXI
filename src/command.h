#pragma once

#include <string>
#include <string_view>

namespace linuxxi
{
    enum class Fix
    {
        Pool,
        Grace,
        Locale,
    };

    enum class CommandKind
    {
        Status,
        Memory,
        Dump,
        Switch,
        Usage,
    };

    struct Command
    {
        CommandKind kind = CommandKind::Usage;
        Fix fix          = Fix::Pool; // for a switch
        bool on          = false;
    };

    // The words after /linuxxi, in any case and with any spacing.
    Command ParseCommand(std::string_view words);
    std::string Usage(std::string_view command);
    // "pool", "grace" or "locale", as the commands name the fix.
    std::string_view FixWord(Fix fix);
}
