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
constexpr int kHudBottomBand = 15;                         // rows 15-19 may hold the bottom right group
constexpr int kRows = 20, kColumns = 30;                   // BG0 cells on the 240x160 screen
enum : std::uint8_t { kNative = 0, kLeft, kCentre, kRight };

// Where a GBA's BG0 cells go this frame (classify_hud). Kept per GBA: the
// machines of a session take turns, so a single table would mix them up.
struct HudState {
    const gba::GbaBus* bus = nullptr;
    bool active = false;                                   // BG0 is the plain HUD layer
    bool bands = false;                                    // this frame is shown 3:2 with bands
    std::array<std::uint8_t, kRows * kColumns> kind{};     // per BG0 cell (classify_hud)
};
std::array<HudState, 4> g_hud;

HudState& hud_state(const gba::GbaBus* bus) {
    for (HudState& state : g_hud) {
        if (state.bus == bus) return state;
    }
    for (HudState& state : g_hud) {
        if (!state.bus) { state.bus = bus; return state; }
    }
    g_hud[0] = HudState{};
    g_hud[0].bus = bus;
    return g_hud[0];
}

const std::uint8_t* bg0_map(const gba::GbaBus* bus) {
    return bus->vram_ptr() + ((u16(bus->io().raw() + 0x08) >> 8) & 0x1F) * 0x800;
}

// Why BG0 does not hold just the HUD this frame (0 = it does). Diagnostic
// only (FourSwordsLinkProbe --map-log): 1 a window is on, 2 BG0 is larger than
// 256x256, 3 BG0 is scrolled, 4 a tile outside the HUD's places (a message, a
// menu), 5 a HUD row crosses a cut.
int hud_reason(const gba::GbaBus* bus) {
    const std::uint8_t* io = bus->io().raw();
    if (u16(io) & 0xE000) return 1;
    if (u16(io + 0x08) & 0xC000) return 2;
    if ((u16(io + 0x10) & 0x1FF) || (u16(io + 0x12) & 0x1FF)) return 3;
    const std::uint8_t* map = bg0_map(bus);
    const auto used = [map](int tx, int ty) { return u16(map + (ty * 32 + tx) * 2) != 0; };
    for (int ty = 0; ty < 20; ++ty) {
        for (int tx = 0; tx < 30; ++tx) {
            if (!used(tx, ty)) continue;
            const bool top = ty < kHudTopRows;
            const bool bottom_right = ty >= kHudBottomRow && tx >= kHudRightStart / 8;
            if (!top && !bottom_right) return 4;
        }
        for (const int cut : {kHudLeftEnd / 8, kHudRightStart / 8}) {
            if (ty < kHudTopRows && used(cut - 1, ty) && used(cut, ty)) return 5;
        }
    }
    return 0;
}

// Where each BG0 cell goes on the wide picture. Decided per cell, every
// frame, so a message or a menu never moves the HUD (19 Nov playtest: the
// whole HUD went back to its 240-wide place for every "+20" rupee popup,
// every message and the whole boss floor, whose rupee counter is wider):
//   left    the HUD's group at the top left           -> the picture's left edge
//   centre  the hearts, top middle                    -> the picture's middle
//   right   the top right group, and the bottom right
//           group (rupees, "+20" popups)              -> the picture's right edge
//   native  everything else (messages, menus)         -> with the 240 native
//           columns, so it stays lined up with the world and its sprites
// A group is a run of used cells in one row, with at most one empty cell
// inside. Only the top four and bottom five rows hold HUD groups.
void classify_hud(const gba::GbaBus* bus, HudState& state) {
    state.kind.fill(kNative);
    const std::uint8_t* io = bus->io().raw();
    state.active = !(u16(io + 0x08) & 0xC000) && !(u16(io + 0x10) & 0x1FF) && !(u16(io + 0x12) & 0x1FF);
    if (!state.active) return;   // BG0 is not the plain 256x256 HUD layer: leave it alone
    const std::uint8_t* map = bg0_map(bus);
    for (int ty = 0; ty < kRows; ++ty) {
        const bool top = ty < kHudTopRows, bottom = ty >= kHudBottomBand;
        if (!top && !bottom) continue;
        for (int tx = 0; tx < kColumns;) {
            if (!u16(map + (ty * 32 + tx) * 2)) { ++tx; continue; }
            int end = tx;   // last used cell of the run
            for (int next = tx + 1; next < kColumns && next <= end + 2; ++next) {
                if (u16(map + (ty * 32 + next) * 2)) end = next;
            }
            std::uint8_t kind = kNative;
            if (top && tx <= 3 && end < kHudLeftEnd / 8) kind = kLeft;
            else if (top && tx >= 8 && end < kHudRightStart / 8) kind = kCentre;
            else if (tx >= (top ? 22 : 20) && end >= 26) kind = kRight;
            for (int c = tx; c <= end; ++c) state.kind[ty * kColumns + c] = kind;
            tx = end + 1;
        }
    }
}

// The canvas column of native column 0 for a kind.
int kind_shift(std::uint8_t kind) {
    const int extra = static_cast<int>(g_ws_view_width) - 240;
    switch (kind) {
    case kLeft: return 0;
    case kCentre: return extra / 2;
    case kRight: return extra;
    default:
        return gba::g_ws_native_view_left >= 0 ? gba::g_ws_native_view_left : static_cast<int>(g_ws_extra_left);
    }
}

// BG0: which native column each canvas column shows (-1: none).
int hud_bg_x(int /*bg*/, int x, int y, int* out_hw_x) {
    const gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus || y < 0 || y >= 160) return 0;
    const HudState& state = hud_state(bus);
    if (!state.active) return 0;
    const std::uint8_t* map = bg0_map(bus);
    const int ty = y >> 3;
    // A moved HUD group first, then whatever stays with the native columns.
    for (const std::uint8_t kind : {kRight, kLeft, kCentre}) {
        const int hx = x - kind_shift(kind);
        if (hx < 0 || hx >= 240 || state.kind[ty * kColumns + (hx >> 3)] != kind) continue;
        if (!u16(map + (ty * 32 + (hx >> 3)) * 2)) continue;
        *out_hw_x = hx;
        return 1;
    }
    const int hx = x - kind_shift(kNative);
    if (hx < 0 || hx >= 240 || state.kind[ty * kColumns + (hx >> 3)] != kNative) return -1;
    *out_hw_x = hx;
    return 1;
}

// Sprites: a priority-0 sprite whose centre lies on a used BG0 cell moves
// with that cell (the item icon on its frame). Everything else is left alone.
int hud_obj_x(int /*index*/, std::uint16_t attr0, std::uint16_t attr1, std::uint16_t attr2, int* out_x) {
    if ((attr2 >> 10) & 3) return 0;
    const gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus) return 0;
    const HudState& state = hud_state(bus);
    if (!state.active) return 0;
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
    const std::uint8_t kind = state.kind[(cy >> 3) * kColumns + (cx >> 3)];
    if (kind == kNative) return 0;
    *out_x = x + kind_shift(kind) - kind_shift(kNative);
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
    // Outside a stage (title, lobby, menus) the picture is 3:2 with bands.
    if (!iw || iw[kGameMode] != kModeStage) {
        if (bus) {
            hud_state(bus).active = false;
            hud_state(bus).bands = true;
        }
        gba::g_ws_authored_margin_layers = 0;
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
        hud_state(bus).active = false;
        hud_state(bus).bands = true;
        gba::g_ws_authored_margin_layers = 0;
        gba::g_ws_pillarbox = 1;
        gba::g_ws_native_view_left = -1;
        return;
    }
    hud_state(bus).bands = false;
    gba::g_ws_pillarbox = 0;
    classify_hud(bus, hud_state(bus));
    // Graphic windows (the pause menu's box is WIN1) do not reach the
    // margins. Where windows are on, the engine draws a margin row black
    // unless it is told the margins lie outside every window; then WINOUT
    // decides what shows there, as it does beside the menu on the GBA. That
    // is only safe while WINOUT shows the stage (BG1 and BG2): the area
    // entry effect has WINOUT = 0, and its margins must stay black.
    {
        const std::uint8_t* io = bus->io().raw();
        gba::g_ws_authored_margin_layers = (u16(io) & 0xE000) && (u16(io + 0x4A) & 0x6) == 0x6 ? 1 : 0;
    }
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
    gba::g_ws_authored_margin_layers = 0;
    gba::g_ws_pillarbox = 0;
    gba::g_ws_native_view_left = -1;
}

}  // namespace

namespace fsr {

int wide_screen_mismatches(const gba::GbaBus& bus, int bg) {
    return screen_mismatches(&bus, bg);
}

int wide_hud_reason(const gba::GbaBus& bus) {
    return hud_reason(&bus);
}

bool wide_bands(const gba::GbaBus& bus) {
    for (const HudState& state : g_hud) {
        if (state.bus == &bus) return state.bands;
    }
    return false;
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
