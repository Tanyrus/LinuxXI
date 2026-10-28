#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace linuxxi
{
    constexpr uint32_t kGraceFrames = 3;

    struct GraceState
    {
        bool active = false, on = true;
        uint64_t heldBack = 0;
        uint32_t waiting  = 0;
    };

    struct Held
    {
        uintptr_t object;
        uint32_t frame; // when it was held, or last seen bound
        int kind;       // which Release it goes to
    };

    // The last releases held back from Direct3D. Each waits kGraceFrames frames, and again as long as the device still
    // has it bound: a state block can bind it once more without a reference of its own.
    class GraceLedger
    {
    public:
        // False for one already held: a release too many of it changes nothing, ours is still its one reference.
        bool Hold(uintptr_t object, uint32_t frame, int kind);
        // Takes out and returns what may go now.
        std::vector<Held> Due(uint32_t frame, const std::vector<uintptr_t>& bound);
        // Takes out and returns everything, whatever its frames and binding.
        std::vector<Held> TakeAll();
        size_t Waiting() const { return m_Held.size(); }
        uint64_t HeldBack() const { return m_HeldBack; }

    private:
        std::vector<Held> m_Held;
        uint64_t m_HeldBack = 0;
    };
}
