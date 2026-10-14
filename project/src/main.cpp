// green_lantern - ReXGlue Recompiled Project

#include "generated/hinted/green_lantern_init.h"
#include "green_lantern_app.h"

#include <cmath>

// ReXGlue emits roundevenf for PPC round-to-nearest-even instructions. The
// Windows CRT does not export that C23 symbol, while nearbyintf implements
// the same operation under the default floating-point rounding mode.
extern "C" float roundevenf(float value) {
    return std::nearbyintf(value);
}

REX_DEFINE_APP(green_lantern, GreenLanternApp::Create)
