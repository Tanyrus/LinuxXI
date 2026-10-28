#pragma once

#include <windows.h>

namespace linuxxi
{
    // The slot module calls its import of function from dll (any case) through; null when it imports no such function.
    void** ImportSlot(HMODULE module, const char* dll, const char* function);
    // Writes value into a slot of a table that may be read-only; false when its page could not be made writable.
    bool WriteSlot(void** slot, void* value);
}
