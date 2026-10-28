#include "command.h"
#include "test.h"

using namespace linuxxi;

TEST(the_command_reads_its_words_in_any_case_and_with_any_spacing)
{
    CHECK(ParseCommand("").kind == CommandKind::Status);
    CHECK(ParseCommand("   ").kind == CommandKind::Status);
    CHECK(ParseCommand(" Memory ").kind == CommandKind::Memory);
    CHECK(ParseCommand("dump").kind == CommandKind::Dump);
    const Command off = ParseCommand("pool  off ");
    CHECK(off.kind == CommandKind::Switch && off.fix == Fix::Pool && !off.on);
    const Command on = ParseCommand("GRACE On");
    CHECK(on.kind == CommandKind::Switch && on.fix == Fix::Grace && on.on);
    const Command locale = ParseCommand("locale on");
    CHECK(locale.kind == CommandKind::Switch && locale.fix == Fix::Locale && locale.on);
}

TEST(anything_else_asks_for_the_usage)
{
    for (const char* words : {"pool", "pool maybe", "locale on now", "memory dump", "help", "on"})
        CHECK(ParseCommand(words).kind == CommandKind::Usage);
}
