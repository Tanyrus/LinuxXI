#include "pool.h"
#include "test.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

using namespace linuxxi;

namespace
{
    // Chunks from malloc, with what was reserved and returned.
    struct System
    {
        std::vector<uint8_t*> reserved, returned;
        bool refuse = false;

        // A pool keeps its chunks for good, as the plugin's does; the test's go with it.
        ~System()
        {
            for (uint8_t* base : reserved)
                if (std::find(returned.begin(), returned.end(), base) == returned.end()) std::free(base);
        }

        Pool MakePool()
        {
            return Pool(
                [this](size_t bytes) -> uint8_t* {
                    if (refuse) return nullptr;
                    reserved.push_back(static_cast<uint8_t*>(std::malloc(bytes)));
                    return reserved.back();
                },
                [this](uint8_t* base) {
                    returned.push_back(base);
                    std::free(base);
                });
        }
    };

    std::vector<void*> TakeMany(Pool& pool, size_t count)
    {
        std::vector<void*> blocks;
        for (size_t i = 0; i < count; ++i)
            blocks.push_back(pool.Take());
        return blocks;
    }
}

TEST(each_block_holds_a_whole_copy_apart_from_the_others)
{
    System system;
    Pool pool                 = system.MakePool();
    std::vector<void*> blocks = TakeMany(pool, kBlocksPerChunk + 3);
    for (size_t i = 0; i < blocks.size(); ++i)
        std::memset(blocks[i], static_cast<int>(i & 0xFF), kCopyBytes);
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const auto* bytes = static_cast<const uint8_t*>(blocks[i]);
        CHECK(bytes[0] == (i & 0xFF) && bytes[kCopyBytes - 1] == (i & 0xFF));
    }
    CHECK_EQ(std::set<void*>(blocks.begin(), blocks.end()).size(), blocks.size());
    CHECK_EQ(system.reserved.size(), size_t{2});
    for (void* block : blocks)
        CHECK(pool.Give(block));
}

TEST(a_freed_block_is_handed_out_again_before_a_new_chunk_is_made)
{
    System system;
    Pool pool                 = system.MakePool();
    std::vector<void*> blocks = TakeMany(pool, kBlocksPerChunk);
    CHECK(pool.Give(blocks[7]));
    CHECK(pool.Take() == blocks[7]);
    CHECK_EQ(system.reserved.size(), size_t{1});
    CHECK_EQ(pool.Stats().held, static_cast<uint32_t>(kBlocksPerChunk));
}

TEST(an_empty_chunk_goes_back_to_the_system_while_another_empty_one_is_kept)
{
    System system;
    Pool pool                 = system.MakePool();
    std::vector<void*> blocks = TakeMany(pool, 3 * kBlocksPerChunk);
    CHECK_EQ(pool.Stats().chunks, 3u);
    for (void* block : blocks)
        CHECK(pool.Give(block));
    CHECK_EQ(system.returned.size(), size_t{2});
    CHECK_EQ(pool.Stats().chunks, 1u);
    CHECK_EQ(pool.Stats().chunksReturned, 2u);
    CHECK_EQ(pool.Stats().held, 0u);
    CHECK_EQ(pool.Stats().peakHeld, static_cast<uint32_t>(3 * kBlocksPerChunk));
    CHECK(pool.Take() != nullptr);
    CHECK_EQ(system.reserved.size(), size_t{3});
}

TEST(blocks_come_from_the_lowest_chunk_with_room)
{
    System system;
    Pool pool                 = system.MakePool();
    std::vector<void*> blocks = TakeMany(pool, 2 * kBlocksPerChunk);
    const uint8_t* low        = std::min(system.reserved[0], system.reserved[1]);
    void* inLow               = nullptr;
    void* inHigh              = nullptr;
    for (void* block : blocks)
        (static_cast<uint8_t*>(block) < low + kChunkBytes ? inLow : inHigh) = block;
    CHECK(pool.Give(inHigh));
    CHECK(pool.Give(inLow));
    CHECK(pool.Take() == inLow);
}

TEST(memory_outside_the_pool_is_left_to_the_heap)
{
    System system;
    Pool pool   = system.MakePool();
    void* block = pool.Take();
    int other   = 0;
    CHECK(!pool.Give(&other));
    CHECK(!pool.Owns(&other));
    CHECK(pool.Owns(block));
    CHECK(pool.Give(block));
    CHECK_EQ(pool.Stats().held, 0u);
}

// The heap does not own the pool's memory, so even a free the host should not have made stays with the pool, and a
// second free must not put a block on the free list twice, where two copies would share it.
TEST(a_second_give_or_a_pointer_into_a_block_stays_with_the_pool_and_changes_nothing)
{
    System system;
    Pool pool = system.MakePool();
    void* a   = pool.Take();
    void* b   = pool.Take();
    CHECK(pool.Give(static_cast<uint8_t*>(a) + 16));
    CHECK(pool.Give(a));
    CHECK(pool.Give(a));
    CHECK_EQ(pool.Stats().held, 1u);
    CHECK_EQ(pool.Stats().strayGives, uint64_t{2});
    CHECK(pool.Held() == std::vector<const void*>{b});
    void* c = pool.Take();
    void* d = pool.Take();
    CHECK(c != d && c != b && d != b);
}

TEST(no_block_is_handed_out_when_the_system_refuses_a_chunk)
{
    System system;
    system.refuse = true;
    Pool pool     = system.MakePool();
    CHECK(pool.Take() == nullptr);
    CHECK_EQ(pool.Stats().refused, uint64_t{1});
    CHECK_EQ(pool.Stats().held, 0u);
    CHECK(pool.Low() == 0 && pool.High() == 0);
}

TEST(the_held_blocks_are_those_taken_and_not_given_back)
{
    System system;
    Pool pool                 = system.MakePool();
    std::vector<void*> blocks = TakeMany(pool, kBlocksPerChunk + 5);
    CHECK(pool.Give(blocks[3]));
    CHECK(pool.Give(blocks[kBlocksPerChunk + 1]));
    const std::vector<const void*> held = pool.Held();
    const std::set<const void*> heldSet(held.begin(), held.end());
    CHECK_EQ(held.size(), blocks.size() - 2);
    CHECK(heldSet.count(blocks[3]) == 0 && heldSet.count(blocks[kBlocksPerChunk + 1]) == 0);
    CHECK(heldSet.count(blocks[0]) == 1 && heldSet.count(blocks[kBlocksPerChunk + 4]) == 1);
}

// The free hook trusts the span alone to tell a copy from the heap's memory, so it must cover every chunk and no more.
TEST(the_span_covers_every_chunk_and_shrinks_when_the_top_one_goes_back)
{
    System system;
    Pool pool                 = system.MakePool();
    std::vector<void*> blocks = TakeMany(pool, 3 * kBlocksPerChunk);
    std::vector<uint8_t*> bases = system.reserved;
    std::sort(bases.begin(), bases.end());
    CHECK(pool.Low() == reinterpret_cast<uintptr_t>(bases[0]));
    CHECK(pool.High() == reinterpret_cast<uintptr_t>(bases[2]) + kChunkBytes);
    const auto in = [](void* block, const uint8_t* base) { return block >= base && block < base + kChunkBytes; };
    for (void* block : blocks)
        if (in(block, bases[1])) pool.Give(block); // emptied and kept: one empty chunk is held for the next copies
    for (void* block : blocks)
        if (in(block, bases[2])) pool.Give(block); // emptied with another empty one about, so it goes back
    CHECK(pool.Low() == reinterpret_cast<uintptr_t>(bases[0]));
    CHECK(pool.High() == reinterpret_cast<uintptr_t>(bases[1]) + kChunkBytes);
    CHECK_EQ(pool.Stats().peakChunks, uint32_t{3});
    CHECK_EQ(pool.Stats().served, uint64_t{3 * kBlocksPerChunk});
}

// The host's copy: the packet starts at the first 32-byte boundary that leaves room for the 32-bit pointer before it.
TEST(a_copys_packet_starts_at_the_first_boundary_after_its_pointer)
{
    CHECK_EQ(CopyData(0x1000), uintptr_t{0x1020});
    CHECK_EQ(CopyData(0x1010), uintptr_t{0x1020});
    CHECK_EQ(CopyData(0x101C), uintptr_t{0x1020});
    CHECK_EQ(CopyData(0x101D), uintptr_t{0x1040});
}

// FFXI's header word: the id in its low nine bits, the size above them. 0x118 needs the ninth.
TEST(a_packets_id_is_read_from_its_header_word)
{
    CHECK_EQ(PacketIdOf(0x00002B18u), uint32_t{0x118});
    CHECK_EQ(PacketIdOf(0x00003A0Du), uint32_t{0x00D});
}

// calloc(count, size) is a copy only when its blocks make exactly the size the host asks malloc for.
TEST(calloc_asks_for_a_copy_only_when_its_blocks_make_exactly_one)
{
    CHECK(IsCopyRequest(1, kCopyBytes) && IsCopyRequest(kCopyBytes, 1) && IsCopyRequest(3, 3345) && IsCopyRequest(5, 2007));
    CHECK(!IsCopyRequest(0, kCopyBytes) && !IsCopyRequest(2, 5017) && !IsCopyRequest(2, 5018) && !IsCopyRequest(1, kCopyBytes + 1));
}
