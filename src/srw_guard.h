#pragma once

#include <windows.h>

namespace linuxxi
{
    // Holds an SRW lock for its scope, so a throw cannot leave it held. SRW locks never allocate, which the allocator
    // hooks need.
    template <bool Shared>
    class SrwGuard
    {
    public:
        explicit SrwGuard(SRWLOCK& lock) : m_Lock(lock)
        {
            if constexpr (Shared)
                AcquireSRWLockShared(&m_Lock);
            else
                AcquireSRWLockExclusive(&m_Lock);
        }
        ~SrwGuard()
        {
            if constexpr (Shared)
                ReleaseSRWLockShared(&m_Lock);
            else
                ReleaseSRWLockExclusive(&m_Lock);
        }
        SrwGuard(const SrwGuard&)            = delete;
        SrwGuard& operator=(const SrwGuard&) = delete;

    private:
        SRWLOCK& m_Lock;
    };

    using ExclusiveGuard = SrwGuard<false>;
    using SharedGuard    = SrwGuard<true>;
}
