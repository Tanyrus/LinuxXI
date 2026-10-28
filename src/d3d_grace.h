#pragma once

#include "grace.h"

#include <cstdint>

struct IDirect3DDevice8;

namespace linuxxi
{
    // Patches Release in the method tables every texture (2D, cube and volume), index and vertex buffer shares, once a
    // process.
    void StartGrace(IDirect3DDevice8* device);
    // Until the next StartGrace no release is held back; what is held waits for the frames after it.
    void StopGrace();
    // Once a Present: lets go of what has waited long enough.
    void GraceFrame();
    // Off, no new release is held back; what is held still goes in time.
    void SetGraceOn(bool on);
    GraceState GraceNow();
}
