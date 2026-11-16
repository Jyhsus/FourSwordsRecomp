// fs_layout_test.cpp — the local multiplayer window layout and view scaler
// (src/fs_layout.h, docs/DESIGN.md section 10). No ROM, no SDL.
//
//   ctest --test-dir build -R fs_layout

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "fs_layout.h"

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
    if (ok) return;
    std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
    ++failures;
}
#define CHECK(x) check((x), #x, __LINE__)

// Every view inside the canvas, all the same size, none overlapping, the
// source aspect kept to within the one pixel rounding allows.
void check_common(const fsr::LocalLayout& l, int canvas_w, int canvas_h, int view_w, int view_h) {
    for (int i = 0; i < l.views; ++i) {
        const fsr::ViewRect& v = l.view[i];
        CHECK(v.w > 0 && v.h > 0);
        CHECK(v.x >= 0 && v.y >= 0 && v.x + v.w <= canvas_w && v.y + v.h <= canvas_h);
        CHECK(v.w == l.view[0].w && v.h == l.view[0].h);
        CHECK(std::abs(v.w * view_h - v.h * view_w) <= std::max(view_w, view_h));
        for (int j = 0; j < i; ++j) {
            const fsr::ViewRect& o = l.view[j];
            CHECK(v.x >= o.x + o.w || o.x >= v.x + v.w || v.y >= o.y + o.h || o.y >= v.y + v.h);
        }
    }
}

void layouts(int view_w, int view_h) {
    const int sizes[][2] = {{1920, 1080}, {2560, 1440}, {1280, 720}, {1024, 768}, {960, 640},
                            {1366, 768}, {800, 1280}, {482, 162}, {300, 200}};
    for (const auto& size : sizes) {
        const int cw = size[0], ch = size[1];
        for (int integer = 0; integer < 2; ++integer) {
            for (int views = 1; views <= 4; ++views) {
                const auto l = fsr::local_layout(views, cw, ch, view_w, view_h, integer != 0);
                CHECK(l.views == views);
                check_common(l, cw, ch, view_w, view_h);
                const auto& v = l.view;
                if (views == 1) {   // centred
                    CHECK(std::abs((v[0].x * 2 + v[0].w) - cw) <= 1);
                    CHECK(std::abs((v[0].y * 2 + v[0].h) - ch) <= 1);
                }
                if (views >= 2) {   // P1 left of P2, same row, the pair centred
                    CHECK(v[0].x < v[1].x && v[0].y == v[1].y);
                    CHECK(std::abs(v[0].x - (cw - (v[1].x + v[1].w))) <= 1);
                }
                if (views == 2) CHECK(std::abs((v[0].y * 2 + v[0].h) - ch) <= 1);
                if (views == 3) {   // P3 below, centred under the pair
                    CHECK(v[2].y > v[0].y);
                    CHECK(std::abs((v[2].x * 2 + v[2].w) - (v[0].x + v[1].x + v[1].w)) <= 1);
                }
                if (views == 4) {   // P3 under P1, P4 under P2
                    CHECK(v[2].x == v[0].x && v[3].x == v[1].x);
                    CHECK(v[2].y == v[3].y && v[2].y > v[0].y);
                }
                if (views >= 3) CHECK(std::abs(v[0].y - (ch - (v[2].y + v[2].h))) <= 1);
                // The block touches the canvas on one axis (no wasted margin)
                // unless a whole-number scale asked for the bars.
                if (!integer) {
                    const int right = views == 1 ? v[0].x + v[0].w : v[1].x + v[1].w;
                    const int bottom = v[views - 1].y + v[views - 1].h;
                    CHECK(v[0].x + (cw - right) <= 5 || v[0].y + (ch - bottom) <= 5);
                } else if (v[0].w >= view_w) {
                    CHECK(v[0].w % view_w == 0 && v[0].h % view_h == 0 &&
                          v[0].w / view_w == v[0].h / view_h);
                }
            }
        }
    }
}

void known_sizes() {
    // 1080p: two views are exactly 4x; four views fit at 3.37x or 3x whole.
    auto two = fsr::local_layout(2, 1920, 1080, 240, 160, false);
    CHECK(two.view[0].w == 960 && two.view[0].h == 640);   // exactly 4x, so no gap
    CHECK(two.view[1].x == 960 && two.view[0].y == 220);
    two = fsr::local_layout(2, 1920, 1080, 240, 160, true);
    CHECK(two.view[0].w == 960 && two.view[0].h == 640);
    auto four = fsr::local_layout(4, 1920, 1080, 240, 160, false);
    CHECK(four.view[0].w == 809 && four.view[0].h == 539);
    four = fsr::local_layout(4, 1920, 1080, 240, 160, true);
    CHECK(four.view[0].w == 720 && four.view[0].h == 480);
    // A window opened at 2x (the views plus the gap) draws at exactly 2x.
    four = fsr::local_layout(4, 964, 644, 240, 160, false);
    CHECK(four.view[0].w == 480 && four.view[0].h == 320);
    // A 16:9 view (284 x 160), for when the game can fill one: four of them
    // cover a 16:9 screen.
    four = fsr::local_layout(4, 1920, 1080, 284, 160, false);
    CHECK(four.view[0].w == 957 && four.view[0].h == 539);
}

std::vector<std::uint8_t> pattern(int w, int h) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = px.data() + (static_cast<std::size_t>(y) * w + x) * 3;
            p[0] = static_cast<std::uint8_t>(x * 7 + y * 3);
            p[1] = static_cast<std::uint8_t>(x ^ (y * 5));
            p[2] = static_cast<std::uint8_t>((x * y) & 0xFF);
        }
    }
    return px;
}

void scaler() {
    const int sw = 240, sh = 160;
    const auto src = pattern(sw, sh);
    // Whole scales repeat pixels exactly.
    for (int k = 1; k <= 4; ++k) {
        const int dw = sw * k, dh = sh * k;
        std::vector<std::uint8_t> dst(static_cast<std::size_t>(dw) * dh * 3, 0xAA);
        fsr::ViewScaler s;
        s.resize(sw, sh, dw, dh);
        s.blit(src.data(), dst.data(), static_cast<std::size_t>(dw) * 3);
        bool exact = true;
        for (int y = 0; y < dh && exact; ++y) {
            for (int x = 0; x < dw && exact; ++x) {
                exact = std::memcmp(dst.data() + (static_cast<std::size_t>(y) * dw + x) * 3,
                                    src.data() + (static_cast<std::size_t>(y / k) * sw + x / k) * 3, 3) == 0;
            }
        }
        CHECK(exact);
    }
    // A fractional scale keeps flat areas untouched (no colour drift) and
    // writes only inside the view.
    {
        const int dw = 809, dh = 539, pad = 4;
        std::vector<std::uint8_t> flat(static_cast<std::size_t>(sw) * sh * 3);
        for (std::size_t i = 0; i < flat.size(); i += 3) { flat[i] = 200; flat[i + 1] = 100; flat[i + 2] = 50; }
        const std::size_t stride = static_cast<std::size_t>(dw + pad * 2) * 3;
        std::vector<std::uint8_t> dst(stride * (dh + pad * 2), 0);
        fsr::ViewScaler s;
        s.resize(sw, sh, dw, dh);
        s.blit(flat.data(), dst.data() + pad * stride + pad * 3, stride);
        bool ok = true;
        for (int y = 0; y < dh + pad * 2 && ok; ++y) {
            for (int x = 0; x < dw + pad * 2 && ok; ++x) {
                const std::uint8_t* p = dst.data() + y * stride + static_cast<std::size_t>(x) * 3;
                const bool inside = x >= pad && x < pad + dw && y >= pad && y < pad + dh;
                ok = inside ? (p[0] == 200 && p[1] == 100 && p[2] == 50) : (p[0] == 0 && p[1] == 0 && p[2] == 0);
            }
        }
        CHECK(ok);
    }
    // At 3.37x every source pixel keeps at least 2 untouched canvas pixels:
    // the blend at a pixel boundary is 1.12 canvas pixels wide, so it touches
    // at most two columns.
    {
        const int dw = 809;
        std::vector<std::uint8_t> stripes(static_cast<std::size_t>(sw) * 3);
        for (int x = 0; x < sw; ++x) stripes[x * 3] = stripes[x * 3 + 1] = stripes[x * 3 + 2] = (x & 1) ? 255 : 0;
        std::vector<std::uint8_t> dst(static_cast<std::size_t>(dw) * 3);
        fsr::ViewScaler s;
        s.resize(sw, 1, dw, 1);
        s.blit(stripes.data(), dst.data(), dst.size());
        int blended = 0, run = 0, worst = 0;
        for (int x = 0; x < dw; ++x) {
            const bool mixed = dst[x * 3] != 0 && dst[x * 3] != 255;
            blended += mixed;
            run = mixed ? run + 1 : 0;
            worst = std::max(worst, run);
        }
        CHECK(worst <= 2);
        CHECK(blended <= sw * 2);
        CHECK(dw - blended >= sw * 2);
    }
}

}  // namespace

int main() {
    layouts(240, 160);
    layouts(284, 160);
    known_sizes();
    scaler();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("fs_layout: ok\n");
    return 0;
}
