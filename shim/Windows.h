// MinGW on Linux: case-sensitive include fix, plus an MSVC-style UNREFERENCED_PARAMETER (MinGW's assigns to the
// parameter, which fails for the const parameters in Ashita.h).
#pragma once
#include <windows.h>
#undef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) (void)(P)
