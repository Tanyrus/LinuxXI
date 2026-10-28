#include "shot_folders.h"
#include "test.h"

#include <string>
#include <vector>

using namespace linuxxi;

TEST(each_playonline_home_gets_the_games_screenshot_folder)
{
    // A real pub folder: all, then one home per character slot.
    const std::vector<std::string> folders = ShotFolders("X:\\POL\\pub", {"all", "home50", "home96"});
    CHECK(folders == (std::vector<std::string>{"X:\\POL\\pub\\home50\\open\\ScreenShots\\FinalFantasyXI",
                                               "X:\\POL\\pub\\home96\\open\\ScreenShots\\FinalFantasyXI"}));
}

TEST(only_home_and_a_number_is_a_home)
{
    CHECK(ShotFolders("P", {"home", "homes", "home9x", "Home7", "my home1"}) ==
          (std::vector<std::string>{"P\\Home7\\open\\ScreenShots\\FinalFantasyXI"}));
    CHECK(ShotFolders("P", {}).empty());
}

TEST(the_viewer_is_three_folders_above_polcore)
{
    CHECK(ViewerPub("Z:\\Games\\SquareEnix\\PlayOnlineViewer\\viewer\\com\\polcore.dll") ==
          std::optional<std::string>("Z:\\Games\\SquareEnix\\PlayOnlineViewer\\pub"));
    CHECK(!ViewerPub("polcore.dll"));
    CHECK(!ViewerPub("C:\\com\\polcore.dll"));
}
