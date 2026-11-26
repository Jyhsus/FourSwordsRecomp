// fs_pads_test.cpp — controller assignment for local multiplayer
// (src/fs_pads.h, docs/DESIGN.md section 10), driven with SDL virtual
// controllers. Real controller drivers are switched off, so a pad plugged
// into the machine does not change the result. No ROM, no window.
//
//   ctest --test-dir build -R fs_pads

#define SDL_MAIN_HANDLED   // a plain console program; no SDL_main
#include <SDL.h>

#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "fs_pads.h"
#include "fs_prefs.h"

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
    if (ok) return;
    std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
    ++failures;
}
#define CHECK(x) check((x), #x, __LINE__)

constexpr std::uint16_t kA = 1, kB = 2, kRight = 16, kUp = 64;

// A virtual controller of a given model. Same name and IDs = same model
// (the same SDL GUID), like two pads of one brand.
struct Virtual {
    int instance = -1;
    SDL_Joystick* handle = nullptr;   // to press its buttons
};

Virtual attach(const char* name, Uint16 product) {
    SDL_VirtualJoystickDesc desc{};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.vendor_id = 0x1234;
    desc.product_id = product;
    desc.name = name;
    const int device = SDL_JoystickAttachVirtualEx(&desc);
    Virtual pad;
    if (device < 0) {
        std::fprintf(stderr, "attach failed: %s\n", SDL_GetError());
        return pad;
    }
    SDL_PumpEvents();
    pad.instance = SDL_JoystickGetDeviceInstanceID(device);
    pad.handle = SDL_JoystickOpen(device);
    return pad;
}

void detach(Virtual& pad) {
    for (int device = 0; device < SDL_NumJoysticks(); ++device) {
        if (SDL_JoystickGetDeviceInstanceID(device) != pad.instance) continue;
        if (pad.handle) SDL_JoystickClose(pad.handle);
        pad.handle = nullptr;
        SDL_JoystickDetachVirtual(device);
        SDL_PumpEvents();
        return;
    }
}

void press(const Virtual& pad, SDL_GameControllerButton button, bool down) {
    SDL_JoystickSetVirtualButton(pad.handle, button, down ? 1 : 0);
    SDL_JoystickUpdate();
}

// One frame of the runner, then enough frames for a scan.
void frames(fsr::PadSeats& seats, unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        SDL_PumpEvents();
        SDL_GameControllerUpdate();
        seats.poll();
    }
}
constexpr unsigned kScan = fsr::PadSeats::kScanInterval + 1;

bool no_device_twice(const fsr::PadSeats& seats, unsigned players) {
    std::set<int> seen;
    for (unsigned p = 0; p < players; ++p) {
        const int id = seats.instance(p);
        if (id >= 0 && !seen.insert(id).second) return false;
    }
    return true;
}

void listing_follows_lights() {
    Virtual a = attach("Pad A", 1), b = attach("Pad B", 2), c = attach("Pad C", 3);
    // Lights run backwards against connection order: C is light 1.
    SDL_JoystickSetPlayerIndex(c.handle, 0);
    SDL_JoystickSetPlayerIndex(a.handle, 1);
    SDL_JoystickSetPlayerIndex(b.handle, 2);
    const auto pads = fsr::local_pads();
    CHECK(pads.size() == 3);
    if (pads.size() == 3) {
        CHECK(pads[0].instance == c.instance && pads[1].instance == a.instance &&
              pads[2].instance == b.instance);
    }
    detach(a); detach(b); detach(c);
}

void assignment_and_buttons() {
    Virtual a = attach("Pad A", 1), b = attach("Pad B", 2), c = attach("Pad C", 3);
    // Player 1 gets the third listed controller, player 2 the first, and the
    // second controller belongs to nobody.
    fsr::PadSeats seats({2, 0, fsr::kNoPad, fsr::kNoPad}, 2, {});
    const auto pads = fsr::local_pads();
    CHECK(seats.instance(0) == pads[2].instance);
    CHECK(seats.instance(1) == pads[0].instance);
    CHECK(no_device_twice(seats, 2));

    // Each controller's buttons reach its own player and nobody else.
    const Virtual* of_p1 = seats.instance(0) == a.instance ? &a : seats.instance(0) == b.instance ? &b : &c;
    const Virtual* of_p2 = seats.instance(1) == a.instance ? &a : seats.instance(1) == b.instance ? &b : &c;
    const Virtual* nobody = (&a != of_p1 && &a != of_p2) ? &a : (&b != of_p1 && &b != of_p2) ? &b : &c;
    press(*of_p1, SDL_CONTROLLER_BUTTON_A, true);
    frames(seats, 1);
    CHECK(seats.buttons(0) == kA && seats.buttons(1) == 0);
    press(*of_p1, SDL_CONTROLLER_BUTTON_A, false);
    press(*of_p2, SDL_CONTROLLER_BUTTON_DPAD_UP, true);
    press(*of_p2, SDL_CONTROLLER_BUTTON_B, true);
    frames(seats, 1);
    CHECK(seats.buttons(0) == 0 && seats.buttons(1) == (kUp | kB));
    press(*of_p2, SDL_CONTROLLER_BUTTON_DPAD_UP, false);
    press(*of_p2, SDL_CONTROLLER_BUTTON_B, false);
    press(*nobody, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, true);
    frames(seats, 1);
    CHECK(seats.buttons(0) == 0 && seats.buttons(1) == 0);
    press(*nobody, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, false);
    // The left stick is the Control Pad too.
    SDL_JoystickSetVirtualAxis(of_p1->handle, SDL_CONTROLLER_AXIS_LEFTX, 30000);
    SDL_JoystickUpdate();
    frames(seats, 1);
    CHECK(seats.buttons(0) == kRight);
    SDL_JoystickSetVirtualAxis(of_p1->handle, SDL_CONTROLLER_AXIS_LEFTX, 0);
    detach(a); detach(b); detach(c);
}

void unplug_and_replug() {
    Virtual p1 = attach("Pad A", 1), p2 = attach("Pad B", 2), spare = attach("Pad C", 3);
    fsr::PadSeats seats({0, 1, fsr::kNoPad, fsr::kNoPad}, 2, {});
    CHECK(seats.instance(1) == p2.instance);

    // Player 2's controller goes; the spare one, connected from the start
    // and given to nobody, is never handed to player 2.
    detach(p2);
    frames(seats, 1);
    CHECK(seats.missing() == 1);
    frames(seats, kScan * 3);
    CHECK(seats.instance(1) < 0 && seats.missing() == 1);

    // A controller plugged in now takes the waiting place.
    Virtual back = attach("Pad B", 2);
    frames(seats, kScan);
    CHECK(seats.instance(1) == back.instance && seats.missing() < 0);
    CHECK(seats.instance(0) == p1.instance);
    CHECK(no_device_twice(seats, 2));
    detach(p1); detach(back); detach(spare);
}

void same_model_by_light() {
    // Two pads of the same model; each player's light is remembered.
    Virtual a = attach("Same Pad", 7), b = attach("Same Pad", 7);
    SDL_JoystickSetPlayerIndex(a.handle, 0);
    SDL_JoystickSetPlayerIndex(b.handle, 1);
    fsr::PadSeats seats({0, 1, fsr::kNoPad, fsr::kNoPad}, 2, {});
    CHECK(seats.instance(0) == a.instance && seats.instance(1) == b.instance);
    detach(a);
    detach(b);
    frames(seats, 1);
    CHECK(seats.instance(0) < 0 && seats.instance(1) < 0);

    // Player 2's pad comes back first, with player 2's light. A "same model"
    // rule alone would give it to player 1, the first waiting place.
    Virtual b2 = attach("Same Pad", 7);
    SDL_JoystickSetPlayerIndex(b2.handle, 1);
    frames(seats, kScan);
    CHECK(seats.instance(1) == b2.instance);
    CHECK(seats.instance(0) < 0);
    Virtual a2 = attach("Same Pad", 7);
    SDL_JoystickSetPlayerIndex(a2.handle, 0);
    frames(seats, kScan);
    CHECK(seats.instance(0) == a2.instance && seats.instance(1) == b2.instance);
    CHECK(no_device_twice(seats, 2));
    detach(a2); detach(b2);
}

void four_players_never_share() {
    std::vector<Virtual> pads;
    for (int i = 0; i < 4; ++i) pads.push_back(attach("Same Pad", 7));
    fsr::PadSeats seats({0, 1, 2, 3}, 4, {});
    CHECK(no_device_twice(seats, 4));
    for (unsigned p = 0; p < 4; ++p) CHECK(seats.instance(p) >= 0);
    // Unplug all, plug back one by one, scanning in between.
    for (Virtual& pad : pads) detach(pad);
    frames(seats, 1);
    for (int i = 0; i < 4; ++i) {
        pads[i] = attach("Same Pad", 7);
        frames(seats, kScan);
        CHECK(no_device_twice(seats, 4));
    }
    for (unsigned p = 0; p < 4; ++p) CHECK(seats.instance(p) >= 0);
    CHECK(seats.missing() < 0);
    for (Virtual& pad : pads) detach(pad);
}

void plan_without_pad_waits() {
    // Player 2 was given a controller that is not connected (or none at all).
    Virtual a = attach("Pad A", 1);
    fsr::PadSeats seats({0, 1, fsr::kNoPad, fsr::kNoPad}, 3, {});
    CHECK(seats.instance(0) == a.instance && seats.instance(1) < 0 && seats.instance(2) < 0);
    CHECK(seats.missing() < 0);   // never had one: not "disconnected"
    Virtual b = attach("Pad B", 2);
    frames(seats, kScan);
    CHECK(seats.instance(1) == b.instance);
    CHECK(seats.instance(2) < 0);   // player 3 chose no controller
    detach(a); detach(b);
}

// The plan kept between runs (fs_prefs.h) names positions, so after a restart
// each player gets whatever is at that position, and a position with nothing
// there waits.
void plan_from_settings_file() {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() /
        ("fs_pads_test_" + std::to_string(SDL_GetPerformanceCounter()) + ".ini");
    fsr::LocalPlan saved;
    saved.players = 3;
    saved.pad = {1, 0, 2, fsr::kNoPad};
    CHECK(fsr::prefs::save(file, saved));
    fsr::LocalPlan plan;
    fsr::prefs::load(file, plan);
    std::error_code ec;
    std::filesystem::remove(file, ec);

    // Only two controllers this time; connected anew, with new instance IDs.
    Virtual a = attach("Pad A", 1), b = attach("Pad B", 2);
    const auto pads = fsr::local_pads();
    CHECK(pads.size() == 2);
    if (pads.size() == 2) {
        fsr::PadSeats seats(plan.pad, static_cast<unsigned>(plan.players), {});
        CHECK(seats.instance(0) == pads[1].instance);
        CHECK(seats.instance(1) == pads[0].instance);
        CHECK(seats.instance(2) < 0 && seats.missing() < 0);
        Virtual c = attach("Pad C", 3);
        frames(seats, kScan);
        CHECK(seats.instance(2) == c.instance);
        CHECK(no_device_twice(seats, 3));
        detach(c);
    }
    detach(a); detach(b);
}

}  // namespace

int main(int, char**) {
    SDL_SetMainReady();
    // Only virtual controllers: no real driver may add or reorder devices.
    for (const char* hint : {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_XINPUT_ENABLED, SDL_HINT_JOYSTICK_RAWINPUT,
                             SDL_HINT_DIRECTINPUT_ENABLED, SDL_HINT_JOYSTICK_WGI}) {
        SDL_SetHint(hint, "0");
    }
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_PumpEvents();
    if (SDL_NumJoysticks() != 0) {
        std::fprintf(stderr, "a real controller is still visible; the test needs none\n");
        SDL_Quit();
        return 1;
    }
    listing_follows_lights();
    assignment_and_buttons();
    unplug_and_replug();
    same_model_by_light();
    four_players_never_share();
    plan_without_pad_waits();
    plan_from_settings_file();
    SDL_Quit();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("fs_pads: ok\n");
    return 0;
}
