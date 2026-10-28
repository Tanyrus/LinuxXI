#include "shot_folders.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace linuxxi
{
    namespace
    {
        constexpr std::string_view kHome  = "home";
        constexpr const char* kShotPath   = "\\open\\ScreenShots\\FinalFantasyXI";
        constexpr const char* kPub        = "\\pub";
        constexpr int kViewerUp           = 3; // polcore.dll, com, viewer

        bool IsHome(const std::string& name)
        {
            if (name.size() <= kHome.size()) return false;
            for (size_t i = 0; i < kHome.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(name[i])) != kHome[i]) return false;
            return std::all_of(name.begin() + kHome.size(), name.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
        }
    }

    std::vector<std::string> ShotFolders(const std::string& pub, const std::vector<std::string>& names)
    {
        std::vector<std::string> folders;
        for (const std::string& name : names)
            if (IsHome(name)) folders.push_back(pub + "\\" + name + kShotPath);
        return folders;
    }

    std::optional<std::string> ViewerPub(const std::string& polcore)
    {
        size_t end = polcore.size();
        for (int i = 0; i < kViewerUp; ++i)
        {
            const size_t slash = polcore.find_last_of('\\', end - 1);
            if (slash == std::string::npos || slash == 0) return std::nullopt;
            end = slash;
        }
        return polcore.substr(0, end) + kPub;
    }
}
