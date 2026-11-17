#include "fs_wide.h"

#include <algorithm>
#include <array>
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
// The HUD is BG0 (hofs = vofs = 0): the counter at the top left, the hearts
// at the top centre, the item frame at the top right and the rupees at the
// bottom right, plus priority-0 sprites drawn over those tiles (the item
// icon). Text boxes are BG0 too.
//
// The callbacks run while the mirror draws a scanline of one GBA, with that
// GBA as the runtime's active machine, so they read its memory and no other.
// They only read.

extern "C" unsigned g_ws_extra_left, g_ws_view_width;   // the mirror's geometry (runtime.cpp)

namespace {

constexpr std::uint32_t kCameraX = 0x0470, kCameraY = 0x0472;      // IWRAM offsets
constexpr std::uint32_t kStageWidth = 0x0478, kStageHeight = 0x047A;
constexpr std::uint32_t kGameMode = 0x6D10;
constexpr std::uint8_t kModeStage = 2;
constexpr std::uint32_t kMapBg1 = 0x031D0, kMapBg2 = 0x16D90;      // EWRAM offsets
constexpr std::uint32_t kMapRowBytes = 0x100;
constexpr int kMapPixels = 1024;
constexpr int kMaxMismatches = 150;   // of 600-651 tiles on screen, per layer

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

// How many of the tiles on the 240x160 screen differ from the stage buffer
// at the camera's position, for BG1 or BG2. Zero while the screen is showing
// the stage those buffers hold.
int screen_mismatches(const gba::GbaBus* bus, int bg) {
    const std::uint8_t* io = bus->io().raw();
    const std::uint8_t* iw = bus->iwram_ptr();
    const int cnt = u16(io + 0x08 + bg * 2);
    const std::uint8_t* hw = bus->vram_ptr() + ((cnt >> 8) & 0x1F) * 0x800;
    const std::uint8_t* map = bus->ewram_ptr() + (bg == 1 ? kMapBg1 : kMapBg2);
    const int x0 = world(u16(iw + kCameraX), u16(io + 0x10 + bg * 4) & 0x1FF);
    const int y0 = world(u16(iw + kCameraY), u16(io + 0x12 + bg * 4) & 0x1FF);
    if ((cnt & 0xC000) || x0 < 0 || y0 < 0) return 31 * 21;
    const int first_x = x0 >> 3, first_y = y0 >> 3;
    const int columns = (x0 & 7) ? 31 : 30, rows = (y0 & 7) ? 21 : 20;
    int different = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            const int tx = first_x + c, ty = first_y + r;
            if (tx >= 128 || ty >= 128) { ++different; continue; }
            different += u16(hw + ((ty & 31) * 32 + (tx & 31)) * 2) !=
                         u16(map + ty * kMapRowBytes + tx * 2);
        }
    }
    return different;
}

// ── HUD ─────────────────────────────────────────────────────────────────
// On a wide picture the HUD keeps its three groups but moves them to where
// they belong: the left group to the left edge, the right group to the right
// edge, the hearts stay in the middle. Columns of the 240-wide screen:
constexpr int kHudLeftEnd = 96, kHudRightStart = 192;      // pixels; tile columns 12 and 24
constexpr int kHudTopRows = 4, kHudBottomRow = 17;         // tile rows 0-3, and 17-19 at the right

// Whether a GBA's BG0 holds the plain HUD and nothing else this frame. Kept
// per GBA: the machines of a session take turns, so a single flag would mix
// them up.
struct HudState {
    const gba::GbaBus* bus = nullptr;
    bool plain = false;
};
std::array<HudState, 4> g_hud;

HudState& hud_state(const gba::GbaBus* bus) {
    for (HudState& state : g_hud) {
        if (state.bus == bus) return state;
    }
    for (HudState& state : g_hud) {
        if (!state.bus) { state.bus = bus; return state; }
    }
    g_hud[0] = {bus, false};
    return g_hud[0];
}

const std::uint8_t* bg0_map(const gba::GbaBus* bus) {
    return bus->vram_ptr() + ((u16(bus->io().raw() + 0x08) >> 8) & 0x1F) * 0x800;
}

// The HUD may be moved only when that cannot break anything: no windows, BG0
// a plain unscrolled 256x256 map, tiles only where the HUD has them, and no
// group reaching across a cut. Anything else (a text box, a menu, a fade
// through a window) keeps the original placement.
bool plain_hud(const gba::GbaBus* bus) {
    const std::uint8_t* io = bus->io().raw();
    if (u16(io) & 0xE000) return false;                                  // any window
    if (u16(io + 0x08) & 0xC000) return false;                           // BG0 larger than 256x256
    if ((u16(io + 0x10) & 0x1FF) || (u16(io + 0x12) & 0x1FF)) return false;
    const std::uint8_t* map = bg0_map(bus);
    const auto used = [map](int tx, int ty) { return u16(map + (ty * 32 + tx) * 2) != 0; };
    for (int ty = 0; ty < 20; ++ty) {
        for (int tx = 0; tx < 30; ++tx) {
            if (!used(tx, ty)) continue;
            const bool top = ty < kHudTopRows;
            const bool bottom_right = ty >= kHudBottomRow && tx >= kHudRightStart / 8;
            if (!top && !bottom_right) return false;
        }
        for (const int cut : {kHudLeftEnd / 8, kHudRightStart / 8}) {
            if (ty < kHudTopRows && used(cut - 1, ty) && used(cut, ty)) return false;
        }
    }
    return true;
}

// Canvas column of a native HUD column.
int hud_canvas_x(int x) {
    const int extra = static_cast<int>(g_ws_view_width) - 240;
    return x < kHudLeftEnd ? x : x >= kHudRightStart ? x + extra : x + extra / 2;
}

// BG0: which native column each canvas column shows (-1: none).
int hud_bg_x(int /*bg*/, int x, int /*y*/, int* out_hw_x) {
    const gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus || !hud_state(bus).plain) return 0;
    const int width = static_cast<int>(g_ws_view_width), extra = width - 240;
    int hw = -1;
    if (x < kHudLeftEnd) hw = x;
    else if (x >= width - (240 - kHudRightStart)) hw = x - extra;
    else if (x - extra / 2 >= kHudLeftEnd && x - extra / 2 < kHudRightStart) hw = x - extra / 2;
    if (hw < 0) return -1;
    *out_hw_x = hw;
    return 1;
}

// Sprites: a priority-0 sprite whose centre lies on a HUD tile moves with
// that tile. Everything else is a world sprite and is left alone.
int hud_obj_x(int /*index*/, std::uint16_t attr0, std::uint16_t attr1, std::uint16_t attr2, int* out_x) {
    if ((attr2 >> 10) & 3) return 0;
    const gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus || !hud_state(bus).plain) return 0;
    static constexpr int kSize[3][4][2] = {
        {{8, 8}, {16, 16}, {32, 32}, {64, 64}},
        {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
        {{8, 16}, {8, 32}, {16, 32}, {32, 64}}};
    const int shape = (attr0 >> 14) & 3;
    if (shape == 3 || (attr0 & 0x0100)) return 0;           // prohibited shape, or affine
    int x = attr1 & 0x1FF, y = attr0 & 0xFF;
    if (x & 0x100) x -= 0x200;
    if (y >= 160) y -= 256;
    const int cx = x + kSize[shape][(attr1 >> 14) & 3][0] / 2;
    const int cy = y + kSize[shape][(attr1 >> 14) & 3][1] / 2;
    if (cx < 0 || cx >= 240 || cy < 0 || cy >= 160) return 0;
    if (u16(bg0_map(bus) + ((cy >> 3) * 32 + (cx >> 3)) * 2) == 0) return 0;
    const int left = gba::g_ws_native_view_left >= 0 ? gba::g_ws_native_view_left
                                                     : static_cast<int>(g_ws_extra_left);
    *out_x = x + hud_canvas_x(cx) - cx - left;
    return 1;
}

// ── World sprites ───────────────────────────────────────────────────────
// The game draws every object whose left edge is between about -80 and 303
// on its 240-wide screen (measured over a 12,500-frame two-player session:
// no sprite has its 9-bit X between 304 and 415). OAM X has 9 bits, so an
// object at 256..303 is stored as it is and hardware reads it as -256..-209,
// far off the left: invisible on a GBA, and wrongly missing from a right
// margin that reaches past column 256. Read those as the positive values the
// game computed. Everything from 360 up keeps the hardware's meaning.
constexpr int kObjWrapEnd = 360;

int world_obj_x(int raw_x, int* out_x) {
    if (raw_x < 256 || raw_x >= kObjWrapEnd) return 0;
    *out_x = raw_x;
    return 1;
}

void on_init(std::uint32_t /*left*/, std::uint32_t /*right*/) {
    gba::g_ws_tilemap_provider = &stage_tile;
    gba::g_ws_obj_x_provider = &world_obj_x;
    gba::g_ws_bg_x_provider = &hud_bg_x;
    gba::g_ws_bg_x_provider_layers = 1u << 0;
    gba::g_ws_obj_attr_x_provider = &hud_obj_x;
}

void on_frame(const gbarecomp::ExtendedViewFrameInfo* info) {
    const gba::GbaBus* bus = gbarecomp::active_bus();
    const std::uint8_t* iw = bus ? bus->iwram_ptr() : nullptr;
    // Outside a stage (title, lobby, menus) the margins stay black.
    if (!iw || iw[kGameMode] != kModeStage) {
        if (bus) hud_state(bus).plain = false;
        gba::g_ws_pillarbox = 1;
        gba::g_ws_native_view_left = -1;
        return;
    }
    // Inside a stage the screen does not always show the stage: between two
    // areas it is black while the next one loads, and other screens may use
    // BG1 and BG2 for something else. The margins are drawn only while the
    // tiles on screen are the ones the buffers hold at the camera's position.
    // During play at most 44 of the ~600 tiles of a layer differ, for a single
    // frame, when the game changes part of the map; a screen that is not the
    // stage differs everywhere.
    if (screen_mismatches(bus, 1) > kMaxMismatches || screen_mismatches(bus, 2) > kMaxMismatches) {
        hud_state(bus).plain = false;
        gba::g_ws_pillarbox = 1;
        gba::g_ws_native_view_left = -1;
        return;
    }
    gba::g_ws_pillarbox = 0;
    hud_state(bus).plain = plain_hud(bus);
    // The game stops its camera where a 240-pixel screen meets the stage's
    // edge. There the wide picture slides instead of showing nothing: the
    // native 240 columns move toward that side of the canvas.
    const int extra = static_cast<int>(info->extra_left + info->extra_right);
    // The scroll register, not the camera variable, is what this frame is
    // drawn with (it follows the camera one frame later).
    const int camera = world(u16(iw + kCameraX), u16(bus->io().raw() + 0x18) & 0x1FF);
    const int width = std::min(u16(iw + kStageWidth), kMapPixels);
    int left = static_cast<int>(info->extra_left);
    left = std::max(left, camera + 240 + extra - width);   // right edge of the stage
    left = std::min(left, camera);                          // left edge of the stage
    gba::g_ws_native_view_left = std::clamp(left, 0, extra);
}

void on_reset() {
    gba::g_ws_tilemap_provider = nullptr;
    gba::g_ws_bg_x_provider = nullptr;
    gba::g_ws_obj_attr_x_provider = nullptr;
    gba::g_ws_obj_x_provider = nullptr;
    for (HudState& state : g_hud) {
        if (state.bus == gbarecomp::active_bus()) state = {};
    }
    gba::g_ws_pillarbox = 0;
    gba::g_ws_native_view_left = -1;
}

}  // namespace

namespace fsr {

int wide_screen_mismatches(const gba::GbaBus& bus, int bg) {
    return screen_mismatches(&bus, bg);
}

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
