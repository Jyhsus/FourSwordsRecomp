#pragma once
// fs_layout.h — where each player's screen goes in the local multiplayer
// window, and how a GBA frame is scaled into it (docs/DESIGN.md section 10).
// Header-only and SDL-free so tests/fs_layout_test.cpp can check it.
//
// Every view has the same size and the same aspect as its source (view_w x
// view_h logical pixels: 240x160 for the original 3:2 picture). The views sit
// together in the middle of the canvas, as large as the canvas allows:
//
//   1 view   centred
//   2 views  P1 left, P2 right
//   3 views  P1 top left, P2 top right, P3 bottom centre
//   4 views  P1/P2 on top, P3/P4 below
//
// Nothing is stretched or cropped; what is left of the canvas stays as bars.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace fsr {

struct ViewRect {
    int x = 0, y = 0, w = 0, h = 0;
};

struct LocalLayout {
    std::array<ViewRect, 4> view{};
    int views = 0;
};

// Pixels between two views, so their edges do not run into each other. Left
// out when it would cost a whole-number scale (two views on a 1920-wide
// screen are exactly 4x only without it).
inline constexpr int kViewGap = 2;

// integer_scale keeps every source pixel a whole number of canvas pixels
// (larger bars); otherwise the views grow to the largest size that fits.
inline LocalLayout local_layout(int views, int canvas_w, int canvas_h, int view_w, int view_h,
                                bool integer_scale) {
    LocalLayout out;
    out.views = std::clamp(views, 1, 4);
    if (view_w < 1 || view_h < 1) return out;
    const int cols = out.views == 1 ? 1 : 2, rows = out.views > 2 ? 2 : 1;

    struct Size {
        int w = 0, h = 0, whole = 0;   // whole: the scale when it is a whole number
    };
    const auto size_for = [&](int gap) {
        Size s;
        const int cell_w = (canvas_w - (cols - 1) * gap) / cols;
        const int cell_h = (canvas_h - (rows - 1) * gap) / rows;
        if (cell_w < 1 || cell_h < 1) return s;
        s.w = cell_w;
        s.h = cell_h;
        if (static_cast<std::int64_t>(s.w) * view_h <= static_cast<std::int64_t>(s.h) * view_w) {
            s.h = std::max(1, static_cast<int>((static_cast<std::int64_t>(s.w) * view_h + view_w / 2) / view_w));
        } else {
            s.w = std::max(1, static_cast<int>((static_cast<std::int64_t>(s.h) * view_w + view_h / 2) / view_h));
        }
        // A fit within two pixels of a whole scale takes the whole scale (a
        // window opened at 2x must not draw at 2.004x). Below 1x there is no
        // whole scale; the fit stays.
        const int k = std::min(cell_w / view_w, cell_h / view_h);
        if (k >= 1 && (integer_scale || (s.w - k * view_w <= 2 && s.h - k * view_h <= 2))) {
            s.w = k * view_w;
            s.h = k * view_h;
            s.whole = k;
        }
        return s;
    };
    int gap = kViewGap;
    Size size = size_for(gap);
    if (const Size tight = size_for(0); tight.whole > size.whole) {
        size = tight;
        gap = 0;
    }
    const int w = size.w, h = size.h;
    if (w < 1 || h < 1) return out;

    const int block_w = cols * w + (cols - 1) * gap;
    const int block_h = rows * h + (rows - 1) * gap;
    const int x0 = (canvas_w - block_w) / 2, y0 = (canvas_h - block_h) / 2;
    for (int i = 0; i < out.views; ++i) {
        int x = x0 + (i % 2) * (w + gap);
        if (out.views == 3 && i == 2) x = x0 + (block_w - w) / 2;
        out.view[i] = {x, y0 + (i / 2) * (h + gap), w, h};
    }
    return out;
}

// Scales RGB888 frames into a view. Each source pixel becomes a block of
// floor(scale) whole canvas pixels and only the leftover fraction is blended
// at the block edges, so pixels keep one size and stay sharp at any window
// size. At a whole scale this is exact pixel repetition. Not a smoothing
// filter: no neighbouring pixels are mixed inside a block.
class ViewScaler {
public:
    void resize(int src_w, int src_h, int dst_w, int dst_h) {
        if (src_w == src_w_ && src_h == src_h_ && dst_w == dst_w_ && dst_h == dst_h_) return;
        src_w_ = src_w; src_h_ = src_h; dst_w_ = dst_w; dst_h_ = dst_h;
        make_axis(x_, dst_w, src_w);
        make_axis(y_, dst_h, src_h);
        rows_.assign(static_cast<std::size_t>(src_h) * dst_w * 3, 0);
    }

    // src: src_w x src_h, tightly packed. dst: top-left of the view inside a
    // canvas whose rows are dst_stride bytes apart.
    void blit(const std::uint8_t* src, std::uint8_t* dst, std::size_t dst_stride) {
        const std::size_t row_bytes = static_cast<std::size_t>(dst_w_) * 3;
        for (int y = 0; y < src_h_; ++y) {
            const std::uint8_t* s = src + static_cast<std::size_t>(y) * src_w_ * 3;
            std::uint8_t* d = rows_.data() + y * row_bytes;
            for (int x = 0; x < dst_w_; ++x, d += 3) {
                const std::uint8_t* a = s + static_cast<std::size_t>(x_[x].index) * 3;
                const unsigned f = x_[x].weight;
                if (f == 0) {
                    d[0] = a[0]; d[1] = a[1]; d[2] = a[2];
                } else {
                    for (int c = 0; c < 3; ++c) {
                        d[c] = static_cast<std::uint8_t>((a[c] * (256 - f) + a[c + 3] * f) >> 8);
                    }
                }
            }
        }
        for (int y = 0; y < dst_h_; ++y) {
            const std::uint8_t* a = rows_.data() + y_[y].index * row_bytes;
            const unsigned f = y_[y].weight;
            std::uint8_t* d = dst + y * dst_stride;
            if (f == 0) {
                std::memcpy(d, a, row_bytes);
            } else {
                const std::uint8_t* b = a + row_bytes;
                for (std::size_t i = 0; i < row_bytes; ++i) {
                    d[i] = static_cast<std::uint8_t>((a[i] * (256 - f) + b[i] * f) >> 8);
                }
            }
        }
    }

private:
    // Output = source[index] * (256 - weight) + source[index + 1] * weight.
    // weight is 0 whenever index is the last source pixel.
    struct Tap {
        int index = 0;
        unsigned weight = 0;
    };

    static void make_axis(std::vector<Tap>& axis, int dst, int src) {
        axis.assign(static_cast<std::size_t>(std::max(dst, 0)), {});
        const double scale = static_cast<double>(dst) / src;
        const double block = std::max(1.0, std::floor(scale));
        for (int i = 0; i < dst; ++i) {
            const double u = (i + 0.5) / scale - 0.5;
            const double whole = std::floor(u);
            const double f = std::clamp((u - whole - 0.5) * block + 0.5, 0.0, 1.0);
            int index = static_cast<int>(whole);
            unsigned weight = static_cast<unsigned>(f * 256.0 + 0.5);
            if (weight == 256) { ++index; weight = 0; }
            if (index < 0) { index = 0; weight = 0; }
            if (index >= src - 1) { index = src - 1; weight = 0; }
            axis[i] = {index, weight};
        }
    }

    int src_w_ = 0, src_h_ = 0, dst_w_ = 0, dst_h_ = 0;
    std::vector<Tap> x_, y_;
    std::vector<std::uint8_t> rows_;
};

}  // namespace fsr
