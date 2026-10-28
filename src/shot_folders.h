#pragma once

#include <optional>
#include <string>
#include <vector>

namespace linuxxi
{
    // The game saves a screenshot into <home>\open\ScreenShots\FinalFantasyXI but makes only its last folder, then
    // waits for PlayOnline's save for ever when the rest is missing, as it is when the viewer never ran.
    std::vector<std::string> ShotFolders(const std::string& pub, const std::vector<std::string>& names);

    // The viewer's pub folder from polcore.dll's path (<viewer>\viewer\com\polcore.dll), when it has three above it.
    std::optional<std::string> ViewerPub(const std::string& polcore);
}
