#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace linuxxi
{
    // Ashita's addon host copies each packet for each addon into 10,000 bytes aligned to 32, behind the pointer it
    // frees the block by, so it asks malloc for room to align: 10,035 bytes.
    constexpr size_t kPacketBufferBytes = 10000;
    constexpr size_t kCopyAlign         = 32;
    constexpr size_t kHostPointerBytes  = sizeof(uint32_t); // the host is 32-bit, whatever the tests are
    constexpr size_t kCopyBytes         = kPacketBufferBytes + kCopyAlign - 1 + kHostPointerBytes;
    static_assert(kCopyBytes == 10035, "the size the host was measured asking for, which the hooks match exactly");
    constexpr size_t kMallocStep        = 16;
    constexpr size_t kBlockBytes        = (kCopyBytes + kMallocStep - 1) / kMallocStep * kMallocStep;
    constexpr size_t kChunkMb           = 4;
    constexpr size_t kChunkBytes        = kChunkMb << 20;
    constexpr size_t kBlocksPerChunk    = kChunkBytes / kBlockBytes;
    constexpr uint32_t kPacketIdMask    = 0x1FF; // a header word's low nine bits; its size is above them

    // Where the packet starts in a copy the host made at block.
    constexpr uintptr_t CopyData(uintptr_t block) { return (block + kHostPointerBytes + kCopyAlign - 1) & ~uintptr_t{kCopyAlign - 1}; }
    constexpr uint32_t PacketIdOf(uint32_t header) { return header & kPacketIdMask; }
    // Whether calloc(count, size) asks for one copy; the division keeps the product from wrapping round to it.
    constexpr bool IsCopyRequest(size_t count, size_t size) { return count != 0 && size == kCopyBytes / count && count * size == kCopyBytes; }

    struct PoolStats
    {
        uint32_t held = 0, peakHeld = 0, chunks = 0, peakChunks = 0, chunksReturned = 0;
        uint64_t served = 0, refused = 0;
        uint64_t strayGives = 0; // frees of pool memory that was not a block handed out
    };

    // Copies in chunks of their own, where Wine's heap keeps 31 together until all are free. A chunk goes back once
    // empty (one empty chunk is kept), and copies come from the lowest chunk with room, so the high ones empty out.
    class Pool
    {
    public:
        using Reserve = std::function<uint8_t*(size_t bytes)>;
        using Release = std::function<void(uint8_t* base)>;

        Pool(Reserve reserve, Release release);

        // Null when no chunk could be had.
        void* Take();
        // False for memory outside the pool, which is the heap's to free. Memory in it that is not a block handed out
        // (a second free, a pointer into a block) is kept from the heap and counted, and changes nothing.
        bool Give(void* block);
        bool Owns(const void* block) const;
        // The span the chunks lie in, for a check without the lock; empty while there are none.
        uintptr_t Low() const { return m_Low; }
        uintptr_t High() const { return m_High; }
        const PoolStats& Stats() const { return m_Stats; }
        // Every block handed out and not given back, in no order.
        std::vector<const void*> Held() const;

    private:
        struct Chunk
        {
            uint8_t* base;
            uint32_t freeHead; // a free block's first four bytes hold the next one's index
            uint32_t fresh;    // blocks from here on were never handed out
            uint32_t used;
            std::bitset<kBlocksPerChunk> out; // which blocks are handed out
        };

        Chunk* ChunkOf(const void* block);
        void Bounds();

        Reserve m_Reserve;
        Release m_Release;
        std::vector<Chunk> m_Chunks; // by base
        uintptr_t m_Low = 0, m_High = 0;
        PoolStats m_Stats;
    };
}
