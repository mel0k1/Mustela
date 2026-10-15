#include <windows.h>

int mustela_main(int, char**) {
    MessageBoxA(nullptr,
                "Mustela Windows port requires a WinCairo WebKit SDK.\n"
                "Configure with -DMUSTELA_WEBKIT_SDK=<path to include/ and lib/>.\n"
                "See docs/BUILDING.md for details.",
                "Mustela",
                MB_OK | MB_ICONINFORMATION);
    return 1;
}
