#include "scyllagpt/window.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    return scyllagpt::run_ui(inst, show);
}
