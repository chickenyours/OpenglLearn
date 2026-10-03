#include "Brotato/Public/menu_layout.h"
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void Check(bool value, const char* name) {
    if (!value) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
bool Near(float a, float b) { return std::abs(a - b) < .0001f; }
}
int main() {
    using namespace Brotato::MenuLayout;
    const auto standard = Fit(1280, 720);
    Check(standard.x == 0 && standard.y == 0 && standard.width == 1280 && standard.height == 720, "16:9 viewport");
    const auto wide = Fit(1800, 720);
    Check(wide.x == 260 && wide.y == 0 && wide.width == 1280 && wide.height == 720, "pillarbox viewport");
    const auto tall = Fit(1280, 1000);
    Check(tall.x == 0 && tall.y == 140 && tall.width == 1280 && tall.height == 720, "letterbox viewport");
    auto point = CursorToCanvas(640, 360, 1280, 720, 1280, 720);
    Check(point && Near(point->x, 0) && Near(point->y, 0), "center cursor");
    point = CursorToCanvas(320, 180, 640, 360, 1280, 720);
    Check(point && Near(point->x, 0) && Near(point->y, 0), "HiDPI cursor");
    point = CursorToCanvas(260, 0, 1800, 720, 1800, 720);
    Check(point && Near(point->x, -HalfWidth) && Near(point->y, HalfHeight), "left top viewport corner");
    Check(!CursorToCanvas(259, 360, 1800, 720, 1800, 720), "reject left black bar");
    Check(!CursorToCanvas(1540, 360, 1800, 720, 1800, 720), "reject right edge and bar");
    Check(!CursorToCanvas(640, 139, 1280, 1000, 1280, 1000), "reject top black bar");
    Check(!CursorToCanvas(640, 860, 1280, 1000, 1280, 1000), "reject bottom edge and bar");
    point = CursorToCanvas(640, 500, 1280, 1000, 1280, 1000);
    Check(point && Near(point->x, 0) && Near(point->y, 0), "letterbox center");
    Check(!CursorToCanvas(0, 0, 0, 720, 1280, 720), "reject minimized window");
    Check(!CursorToCanvas(0, 0, 1280, 720, 0, 0), "reject minimized framebuffer");
    Check(!CursorToCanvas(std::numeric_limits<double>::quiet_NaN(), 0, 1280, 720, 1280, 720), "reject nonfinite cursor");
    for (std::size_t slot = 0; slot < PageSize; ++slot) {
        const auto card = Card(slot);
        Check(card.Contains(card.center), "card center hit");
        Check(!card.Contains(card.center + card.size), "card outside miss");
        const double px = (card.center.x / HalfWidth + 1) * 640;
        const double py = (1 - card.center.y / HalfHeight) * 360 + 140;
        point = CursorToCanvas(px, py, 1280, 1000, 1280, 1000);
        Check(point && card.Contains(*point), "rendered card matches letterbox click");
    }
    Check(Fit(1, 1).width == 1 && Fit(1, 1).height == 1, "tiny viewport remains valid");
    std::cout << "Brotato menu layout passed: viewport, HiDPI, black bars, card hit tests\n";
}
