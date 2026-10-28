#include "pool.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace linuxxi
{
    namespace
    {
        constexpr uint32_t kNone = UINT32_MAX;
    }

    Pool::Pool(Reserve reserve, Release release) : m_Reserve(std::move(reserve)), m_Release(std::move(release)) {}

    void* Pool::Take()
    {
        auto room = std::ranges::find_if(m_Chunks, [](const Chunk& c) { return c.used < kBlocksPerChunk; });
        if (room == m_Chunks.end())
        {
            uint8_t* base = m_Reserve(kChunkBytes);
            if (base == nullptr)
            {
                ++m_Stats.refused;
                return nullptr;
            }
            room = m_Chunks.insert(std::ranges::upper_bound(m_Chunks, base, {}, &Chunk::base), Chunk{base, kNone, 0, 0, {}});
            m_Stats.chunks     = static_cast<uint32_t>(m_Chunks.size());
            m_Stats.peakChunks = std::max(m_Stats.peakChunks, m_Stats.chunks);
            Bounds();
        }
        uint32_t index = room->freeHead;
        if (index != kNone)
            std::memcpy(&room->freeHead, room->base + size_t{index} * kBlockBytes, sizeof room->freeHead);
        else
            index = room->fresh++;
        room->out.set(index);
        ++room->used;
        ++m_Stats.served;
        m_Stats.peakHeld = std::max(m_Stats.peakHeld, ++m_Stats.held);
        return room->base + size_t{index} * kBlockBytes;
    }

    bool Pool::Give(void* block)
    {
        Chunk* chunk = ChunkOf(block);
        if (chunk == nullptr) return false;
        const size_t offset = static_cast<size_t>(static_cast<uint8_t*>(block) - chunk->base);
        const size_t index  = offset / kBlockBytes;
        if (offset % kBlockBytes != 0 || index >= kBlocksPerChunk || !chunk->out.test(index))
        {
            ++m_Stats.strayGives;
            return true;
        }
        chunk->out.reset(index);
        std::memcpy(block, &chunk->freeHead, sizeof chunk->freeHead);
        chunk->freeHead = static_cast<uint32_t>(index);
        --chunk->used;
        --m_Stats.held;
        const bool spare = std::ranges::any_of(m_Chunks, [&](const Chunk& c) { return &c != chunk && c.used == 0; });
        if (chunk->used == 0 && spare)
        {
            m_Release(chunk->base);
            m_Chunks.erase(m_Chunks.begin() + (chunk - m_Chunks.data()));
            m_Stats.chunks = static_cast<uint32_t>(m_Chunks.size());
            ++m_Stats.chunksReturned;
            Bounds();
        }
        return true;
    }

    std::vector<const void*> Pool::Held() const
    {
        std::vector<const void*> held;
        for (const Chunk& chunk : m_Chunks)
            for (uint32_t index = 0; index < chunk.fresh; ++index)
                if (chunk.out.test(index)) held.push_back(chunk.base + size_t{index} * kBlockBytes);
        return held;
    }

    bool Pool::Owns(const void* block) const { return const_cast<Pool*>(this)->ChunkOf(block) != nullptr; }

    Pool::Chunk* Pool::ChunkOf(const void* block)
    {
        const auto* at = static_cast<const uint8_t*>(block);
        auto after     = std::ranges::upper_bound(m_Chunks, at, {}, [](const Chunk& c) { return static_cast<const uint8_t*>(c.base); });
        if (after == m_Chunks.begin()) return nullptr;
        Chunk& chunk = *std::prev(after);
        return at < chunk.base + kChunkBytes ? &chunk : nullptr;
    }

    void Pool::Bounds()
    {
        m_Low  = m_Chunks.empty() ? 0 : reinterpret_cast<uintptr_t>(m_Chunks.front().base);
        m_High = m_Chunks.empty() ? 0 : reinterpret_cast<uintptr_t>(m_Chunks.back().base) + kChunkBytes;
    }
}
