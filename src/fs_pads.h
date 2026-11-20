#pragma once
// fs_pads.h — game controllers for local multiplayer (docs/DESIGN.md section 10).
//
// Kept apart from the runner so tests/fs_pads_test.cpp can drive it with SDL
// virtual controllers. Needs SDL's game controller subsystem.

#include <array>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

struct _SDL_GameController;

namespace fsr {

constexpr int kPadPlayers = 4;
constexpr int kNoPad = -1;

// A connected game controller.
struct LocalPad {
    int instance = -1;   // SDL joystick instance ID (valid until SDL's controller subsystem quits)
    int light = -1;      // SDL player index: the number its player light shows, 0-based; -1 unknown
    std::string name;
    std::string guid;    // the model: two controllers of the same model share it
};

// The connected controllers, in the order players count them: by player light
// (the XInput slot on Xbox pads, the LED on others) and, without one, in the
// order they were connected. SDL's own device order is not used: it depends
// on the driver and can run backwards.
std::vector<LocalPad> local_pads();

// Every player's controller for one session. `plan` names each player's
// controller by its place in local_pads() when the session starts (kNoPad:
// none). From then on:
//
// - A player keeps that device. No device ever drives two players.
// - A controller that was connected at the start but given to nobody is
//   never handed to anyone: someone else may be holding it.
// - If a player's controller is unplugged, their place waits. A controller
//   plugged in later takes a waiting place: first the place whose controller
//   had the same player light, then one whose controller was the same model,
//   then the first waiting place.
class PadSeats {
public:
    using Log = std::function<void(const std::string&)>;

    PadSeats(const std::array<int, kPadPlayers>& plan, unsigned players, Log log);
    ~PadSeats();
    PadSeats(const PadSeats&) = delete;
    PadSeats& operator=(const PadSeats&) = delete;

    // Call once per frame. True when a controller came or went.
    bool poll();

    // A player who had a controller during the session and lost it, or -1.
    int missing() const;

    // Pressed buttons, GBA bit order (pressed = 1): the mapping HostWindow
    // uses, plus the left stick as the Control Pad.
    std::uint16_t buttons(unsigned player) const;

    // The SDL instance ID a player's controller has now, or -1.
    int instance(unsigned player) const;

    // Frames between scans for a new controller while a place is waiting.
    static constexpr unsigned kScanInterval = 30;

private:
    struct Seat {
        _SDL_GameController* pad = nullptr;
        int instance = -1;
        int light = -1;        // of the last controller this place had
        std::string guid;      // of the last controller this place had
        bool wanted = false;   // the plan gives this player a controller
        bool lost = false;     // had one during the session
    };

    bool bound(int instance) const;
    bool open(unsigned player, int instance);
    void say(const std::string& text) const;

    std::array<Seat, kPadPlayers> seats_{};
    std::set<int> ignored_;    // connected at the start, given to nobody
    unsigned players_;
    unsigned since_scan_ = 0;
    Log log_;
};

}  // namespace fsr
