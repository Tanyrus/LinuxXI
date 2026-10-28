#include "figures.h"
#include "test.h"

#include <cstdint>

using namespace linuxxi;

TEST(a_count_reads_in_groups_of_three_digits)
{
    CHECK(Grouped(0) == "0" && Grouped(999) == "999" && Grouped(1000) == "1,000" && Grouped(1234567) == "1,234,567");
    CHECK(Grouped(uint64_t{1} << 32) == "4,294,967,296");
}

TEST(kilobytes_read_as_megabytes_from_a_whole_megabyte_up)
{
    CHECK(KbText(1023) == "1,023 KB");
    CHECK(KbText(1024) == "1.0 MB");
    CHECK(KbText(41250) == "40.3 MB");
}
