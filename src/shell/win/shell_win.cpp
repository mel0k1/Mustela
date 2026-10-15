#include <windows.h>

int mustela_main(int, char**) {
    MessageBoxA(nullptr,
                "Mustela Windows port is under construction.\nTarget: WinCairo flavor of WebKit.",
                "Mustela",
                MB_OK | MB_ICONINFORMATION);
    return 1;
}
