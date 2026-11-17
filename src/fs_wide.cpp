#include "fs_wide.h"

#include <algorithm>
#include <cstdint>

#include "gba_bus.h"
#include "gba_ppu.h"
#include "runtime.h"
#include "runtime_bus_bridge.h"

// What the stage keeps in RAM (found 16 Nov 2026 by matching the hardware
// tile maps against RAM, docs/DESIGN.md section 11):
//
//   0x03000470 u16  camera X, 0x03000472 camera Y: the world pixel at the
//                   top-left of the screen. BG1 and BG2 scroll by its low 9 bits.
//   0x03000478 u16  stage width, 0x0300047A stage height, in pixels.
//   0x020031D0      BG1 (upper layer) tile entries for the whole stage,
//   0x02016D90      BG2 (lower layer): 128 x 128 hardware tile-map entries
//                   each, 0x100 bytes per row. The 256x256 hardware maps are a
//                   scrolling window onto these.
//
// The callbacks run while the mirror draws a scanline of one GBA, with that
// GBA as the runtime's active machine, so they read its memory and no other.
// They only read.

namespace {

constexpr std::uint32_t kCameraX = 0x0470, kCameraY = 0x0472;      // IWRAM offsets
constexpr std::uint32_t kStageWidth = 0x0478, kStageHeight = 0x047A;
constexpr std::uint32_t kGameMode = 0x6D10;
constexpr std::uint8_t kModeStage = 2;
constexpr std::uint32_t kMapBg1 = 0x031D0, kMapBg2 = 0x16D90;      // EWRAM offsets
constexpr std::uint32_t kMapRowBytes = 0x100;
constexpr int kMapPixels = 1024;

int u16(const std::uint8_t* p) { return p[0] | (p[1] << 8); }

// The world coordinate the hardware is scrolled to: the camera's high bits
// with the scroll register's 9 bits, whichever such value is nearest the
// camera (the register may be a frame behind it).
int world(int camera, int scroll) {
    int w = (camera & ~0x1FF) | scroll;
    if (w - camera > 256) w -= 512;
    if (camera - w > 256) w += 512;
    return w;
}

// The tile-map entry at a screen position outside the native 240x160.
int stage_tile(int bg, int hx, int sy, std::uint16_t* out) {
    const gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus || (bg != 1 && bg != 2)) return gba::kWsTilemapUnavailable;
    const std::uint8_t* io = bus->io().raw();
    const std::uint8_t* iw = bus->iwram_ptr();
    const int x = world(u16(iw + kCameraX), u16(io + 0x10 + bg * 4) & 0x1FF) + hx;
    const int y = world(u16(iw + kCameraY), u16(io + 0x12 + bg * 4) & 0x1FF) + sy;
    if (x < 0 || y < 0 || x >= std::min(u16(iw + kStageWidth), kMapPixels) ||
        y >= std::min(u16(iw + kStageHeight), kMapPixels)) {
        return gba::kWsTilemapUnavailable;
    }
    *out = static_cast<std::uint16_t>(
        u16(bus->ewram_ptr() + (bg == 1 ? kMapBg1 : kMapBg2) + (y >> 3) * kMapRowBytes + (x >> 3) * 2));
    return gba::kWsTilemapReplace;
}

void on_init(std::uint32_t /*left*/, std::uint32_t /*right*/) {
    gba::g_ws_tilemap_provider = &stage_tile;
    gba::g_ws_bg_x_provider_layers = 0;
}

void on_frame(const gbarecomp::ExtendedViewFrameInfo* info) {
    const gba::GbaBus* bus = gbarecomp::active_bus();
    const std::uint8_t* iw = bus ? bus->iwram_ptr() : nullptr;
    // Outside a stage (title, lobby, menus) the margins stay black.
    if (!iw || iw[kGameMode] != kModeStage) {
        gba::g_ws_pillarbox = 1;
        gba::g_ws_native_view_left = -1;
        return;
    }
    gba::g_ws_pillarbox = 0;
    // The game stops its camera where a 240-pixel screen meets the stage's
    // edge. There the wide picture slides instead of showing nothing: the
    // native 240 columns move toward that side of the canvas.
    const int extra = static_cast<int>(info->extra_left + info->extra_right);
    const int camera = u16(iw + kCameraX);
    const int width = std::min(u16(iw + kStageWidth), kMapPixels);
    int left = static_cast<int>(info->extra_left);
    left = std::max(left, camera + 240 + extra - width);   // right edge of the stage
    left = std::min(left, camera);                          // left edge of the stage
    gba::g_ws_native_view_left = std::clamp(left, 0, extra);
}

void on_reset() {
    gba::g_ws_tilemap_provider = nullptr;
    gba::g_ws_pillarbox = 0;
    gba::g_ws_native_view_left = -1;
}

}  // namespace

namespace fsr {

gbarecomp::GbaNetplayViewPolicy wide_view_policy() {
    gbarecomp::GbaNetplayViewPolicy policy;
    policy.supported = true;
    policy.max_width = kWideViewWidth;
    policy.init = &on_init;
    policy.frame = &on_frame;
    policy.reset = &on_reset;
    return policy;
}

}  // namespace fsr
