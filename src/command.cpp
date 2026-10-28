#include "command.h"

#include <array>
#include <cctype>
#include <sstream>
#include <vector>

namespace linuxxi
{
    namespace
    {
        constexpr std::string_view kMemoryWord = "memory";
        constexpr std::string_view kDumpWord   = "dump";
        constexpr std::string_view kOn         = "on";
        constexpr std::string_view kOff        = "off";
        constexpr std::array kFixes{Fix::Pool, Fix::Grace, Fix::Locale};
    }

    std::string_view FixWord(Fix fix)
    {
        switch (fix)
        {
            case Fix::Pool: return "pool";
            case Fix::Grace: return "grace";
            case Fix::Locale: return "locale";
        }
        return "";
    }

    Command ParseCommand(std::string_view words)
    {
        std::string lower(words);
        for (char& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::istringstream stream(lower);
        std::vector<std::string> parts;
        for (std::string part; stream >> part;)
            parts.push_back(part);
        if (parts.empty()) return Command{CommandKind::Status};
        if (parts.size() == 1 && parts[0] == kMemoryWord) return Command{CommandKind::Memory};
        if (parts.size() == 1 && parts[0] == kDumpWord) return Command{CommandKind::Dump};
        if (parts.size() == 2 && (parts[1] == kOn || parts[1] == kOff))
            for (const Fix fix : kFixes)
                if (parts[0] == FixWord(fix)) return Command{CommandKind::Switch, fix, parts[1] == kOn};
        return Command{};
    }

    std::string Usage(std::string_view command)
    {
        const std::string c(command);
        std::string fixes;
        for (const Fix fix : kFixes)
            fixes += (fixes.empty() ? "" : "|") + std::string(FixWord(fix));
        return "Usage: " + c + ", " + c + " " + std::string(kMemoryWord) + ", " + c + " " + std::string(kDumpWord) + ", or " + c + " " + fixes + " " +
               std::string(kOn) + "|" + std::string(kOff) + ".";
    }
}
