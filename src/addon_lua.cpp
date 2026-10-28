#include "addon_lua.h"

#include "pool.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace linuxxi
{
    namespace
    {
        constexpr size_t kBytesPerKb = 1024;
        constexpr size_t kCopyKb     = (kCopyBytes + kBytesPerKb - 1) / kBytesPerKb;
        constexpr int kHeapShare     = 256;
        constexpr char kColour[]     = "\x1E\x1F"; // each is followed by one byte of the colour
        constexpr const char* kPacketIn  = "packet_in";
        constexpr const char* kPacketOut = "packet_out";
        constexpr const char* kSettings  = "settings"; // Ashita's settings library, by the name addons require it
        constexpr const char* kRegister  = "register";
        constexpr const char* kRequire   = "require";
        constexpr const char* kIn        = "linuxxi_in";
        constexpr const char* kOut       = "linuxxi_out";
        constexpr const char* kAddonName = "[A-Za-z0-9_-]+"; // a folder name Lua's quotes and the report's space can carry

        // The level of a Lua long bracket opening at source[at] ("[[" is 0, "[==[" is 2); nothing when none opens.
        std::optional<size_t> LongBracket(std::string_view source, size_t at)
        {
            if (at >= source.size() || source[at] != '[') return std::nullopt;
            const size_t level = source.find_first_not_of('=', at + 1);
            if (level == std::string_view::npos || source[level] != '[') return std::nullopt;
            return level - at - 1;
        }

        // Where the long bracket of level opening at at ends, past its closing; the source's end when it never closes.
        size_t PastLongBracket(std::string_view source, size_t at, size_t level)
        {
            const std::string close = "]" + std::string(level, '=') + "]";
            const size_t end        = source.find(close, at + level + 2);
            return end == std::string_view::npos ? source.size() : end + close.size();
        }

        // source with its comments blanked and its strings kept, so a commented-out call is not read as one.
        std::string WithoutComments(std::string_view source)
        {
            std::string out;
            out.reserve(source.size());
            for (size_t at = 0; at < source.size();)
            {
                const char c = source[at];
                size_t end   = at + 1;
                if (c == '\'' || c == '"')
                {
                    while (end < source.size() && source[end] != c && source[end] != '\n')
                        end += source[end] == '\\' ? 2 : 1;
                    end = std::min(end + 1, source.size());
                }
                else if (const std::optional<size_t> level = LongBracket(source, at))
                    end = PastLongBracket(source, at, *level);
                else if (source.compare(at, 2, "--") == 0)
                {
                    const std::optional<size_t> level = LongBracket(source, at + 2);
                    end = level ? PastLongBracket(source, at + 2, *level) : std::min(source.find('\n', at), source.size());
                    out += ' ';
                    at = end;
                    continue;
                }
                out.append(source.substr(at, end - at));
                at = end;
            }
            return out;
        }

        bool IdentifierChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

        // Whether code calls function with argument as a quoted string, "function('argument'" or, where the parenthesis
        // may go, "function 'argument'". A plain search: the installed addons' sources run to tens of megabytes.
        bool Calls(std::string_view code, std::string_view function, std::string_view argument, bool parenthesis)
        {
            for (size_t at = code.find(function); at != std::string_view::npos; at = code.find(function, at + 1))
            {
                if (at > 0 && IdentifierChar(code[at - 1])) continue;
                size_t next = code.find_first_not_of(" \t\r\n", at + function.size());
                const bool opened = next != std::string_view::npos && code[next] == '(';
                if (parenthesis && !opened) continue;
                if (opened) next = code.find_first_not_of(" \t\r\n", next + 1);
                if (next == std::string_view::npos || (code[next] != '\'' && code[next] != '"')) continue;
                const char quote = code[next];
                if (code.compare(next + 1, argument.size(), argument) == 0 && next + 1 + argument.size() < code.size() &&
                    code[next + 1 + argument.size()] == quote)
                    return true;
            }
            return false;
        }

        std::string Unregister(const char* event, const char* alias)
        {
            return "ashita.events.unregister('" + std::string(event) + "', '" + alias + "')";
        }

        // The handler for event, put in again; only while condition holds when one is given.
        std::string Clause(const char* event, const char* alias, const std::string& condition)
        {
            const std::string add = "ashita.events.register('" + std::string(event) + "', '" + alias +
                                    "', function() collectgarbage('step', math.max(" + std::to_string(kCopyKb) + ", collectgarbage('count') / " +
                                    std::to_string(kHeapShare) + ")) end)";
            return Unregister(event, alias) + " " + (condition.empty() ? add : "if " + condition + " then " + add + " end");
        }
    }

    Handlers HandlersIn(std::string_view source)
    {
        const std::string code = WithoutComments(source);
        return Handlers{Calls(code, kRegister, kPacketIn, true), Calls(code, kRegister, kPacketOut, true), Calls(code, kRequire, kSettings, false)};
    }

    std::string HookCode(const Handlers& handlers)
    {
        std::string code;
        if (handlers.packetIn || handlers.settings)
            code = Clause(kPacketIn, kIn, handlers.packetIn ? "" : "package.loaded." + std::string(kSettings) + " ~= nil");
        if (handlers.packetOut) code += (code.empty() ? "" : " ") + Clause(kPacketOut, kOut, "");
        return code;
    }

    std::string UnhookCode()
    {
        return Unregister(kPacketIn, kIn) + " " + Unregister(kPacketOut, kOut);
    }

    std::optional<std::string> AddonCode(const std::string& addon, const Handlers& handlers)
    {
        static const std::regex plain(kAddonName);
        if (!std::regex_match(addon, plain)) return std::nullopt;
        const std::string hook = HookCode(handlers);
        return (hook.empty() ? "" : hook + " ") + "local t = '" + addon + " ' .. math.floor(collectgarbage('count')) " +
               "AshitaCore:GetPluginManager():RaiseEvent('" + kLuaEvent + "', {t:byte(1, -1)})";
    }

    std::optional<LuaReport> LuaReportOf(const void* data, uint32_t size)
    {
        if (data == nullptr) return std::nullopt;
        static const std::regex form(std::string("(") + kAddonName + ") ([0-9]{1,9})");
        const std::string text(static_cast<const char*>(data), size);
        std::smatch found;
        if (!std::regex_match(text, found, form)) return std::nullopt;
        return LuaReport{found[1].str(), static_cast<uint32_t>(std::stoul(found[2].str()))};
    }

    std::optional<std::string> NotLoadedAddon(std::string_view line)
    {
        std::string plain;
        for (size_t i = 0; i < line.size(); ++i)
        {
            if (std::string_view(kColour).find(line[i]) != std::string_view::npos)
            {
                ++i;
                continue;
            }
            plain += line[i];
        }
        static const std::regex notLoaded(R"(within addon '([^']+)'\. Reason: Addon is not loaded)");
        std::smatch found;
        if (!std::regex_search(plain, found, notLoaded)) return std::nullopt;
        return found[1].str();
    }
}
