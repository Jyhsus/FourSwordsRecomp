#include "fs_pads.h"

#include <SDL.h>

#include <algorithm>
#include <climits>
#include <cstdio>

namespace fsr {
namespace {

constexpr Sint16 kStickDeadzone = 16000;

std::string device_guid(int device) {
    char text[40] = "";
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device), text, sizeof(text));
    return text;
}

int device_of(int instance) {
    for (int device = 0; device < SDL_NumJoysticks(); ++device) {
        if (SDL_JoystickGetDeviceInstanceID(device) == instance) return device;
    }
    return -1;
}

}  // namespace

std::vector<LocalPad> local_pads() {
    std::vector<LocalPad> pads;
    for (int device = 0; device < SDL_NumJoysticks(); ++device) {
        if (!SDL_IsGameController(device)) continue;
        LocalPad pad;
        pad.instance = static_cast<int>(SDL_JoystickGetDeviceInstanceID(device));
        pad.light = SDL_JoystickGetDevicePlayerIndex(device);
        const char* name = SDL_GameControllerNameForIndex(device);
        pad.name = name ? name : "Controller";
        pad.guid = device_guid(device);
        pads.push_back(std::move(pad));
    }
    std::stable_sort(pads.begin(), pads.end(), [](const LocalPad& a, const LocalPad& b) {
        const int la = a.light < 0 ? INT_MAX : a.light, lb = b.light < 0 ? INT_MAX : b.light;
        return la != lb ? la < lb : a.instance < b.instance;
    });
    return pads;
}

PadSeats::PadSeats(const std::array<int, kPadPlayers>& plan, unsigned players, Log log)
    : players_(std::min<unsigned>(players, kPadPlayers)), log_(std::move(log)) {
    const std::vector<LocalPad> pads = local_pads();
    say(std::to_string(pads.size()) + " controller(s) connected");
    for (std::size_t i = 0; i < pads.size(); ++i) {
        say("  controller " + std::to_string(i + 1) + ": \"" + pads[i].name + "\" light=" +
            std::to_string(pads[i].light < 0 ? 0 : pads[i].light + 1) +
            " instance=" + std::to_string(pads[i].instance));
    }
    for (unsigned p = 0; p < players_; ++p) {
        seats_[p].wanted = plan[p] != kNoPad;
        if (plan[p] >= 0 && plan[p] < static_cast<int>(pads.size())) {
            open(p, pads[plan[p]].instance);
        } else if (seats_[p].wanted) {
            say("player " + std::to_string(p + 1) + ": no controller yet");
        }
    }
    for (const LocalPad& pad : pads) {
        if (!bound(pad.instance)) {
            ignored_.insert(pad.instance);
            say("controller \"" + pad.name + "\" (instance " + std::to_string(pad.instance) +
                ") belongs to nobody and is left alone");
        }
    }
}

PadSeats::~PadSeats() {
    for (Seat& seat : seats_) {
        if (seat.pad) SDL_GameControllerClose(seat.pad);
    }
}

void PadSeats::say(const std::string& text) const {
    if (log_) log_(text);
}

bool PadSeats::bound(int instance) const {
    for (const Seat& seat : seats_) {
        if (seat.pad && seat.instance == instance) return true;
    }
    return false;
}

bool PadSeats::open(unsigned player, int instance) {
    const int device = device_of(instance);
    SDL_GameController* pad = device >= 0 ? SDL_GameControllerOpen(device) : nullptr;
    if (!pad) {
        say("player " + std::to_string(player + 1) + ": cannot open controller instance " +
            std::to_string(instance) + ": " + SDL_GetError());
        return false;
    }
    Seat& seat = seats_[player];
    seat.pad = pad;
    seat.instance = instance;
    seat.guid = device_guid(device);
    seat.light = SDL_GameControllerGetPlayerIndex(pad);
    const char* name = SDL_GameControllerName(pad);
    say("player " + std::to_string(player + 1) + ": controller \"" + (name ? name : "unknown") +
        "\" (light " + std::to_string(seat.light + 1) + ", instance " + std::to_string(instance) + ")");
    return true;
}

bool PadSeats::poll() {
    bool changed = false;
    bool waiting = false;
    for (unsigned p = 0; p < players_; ++p) {
        Seat& seat = seats_[p];
        if (seat.pad && !SDL_GameControllerGetAttached(seat.pad)) {
            SDL_GameControllerClose(seat.pad);
            seat.pad = nullptr;
            seat.instance = -1;
            seat.lost = true;
            changed = true;
            say("player " + std::to_string(p + 1) + ": controller disconnected");
        }
        waiting |= seat.wanted && !seat.pad;
    }
    if (!waiting || ++since_scan_ < kScanInterval) return changed;
    since_scan_ = 0;
    for (int device = 0; device < SDL_NumJoysticks(); ++device) {
        if (!SDL_IsGameController(device)) continue;
        const int instance = static_cast<int>(SDL_JoystickGetDeviceInstanceID(device));
        if (bound(instance) || ignored_.count(instance)) continue;
        const int light = SDL_JoystickGetDevicePlayerIndex(device);
        const std::string guid = device_guid(device);
        int target = -1;
        const auto pick = [&](auto&& matches) {
            for (unsigned p = 0; p < players_ && target < 0; ++p) {
                const Seat& seat = seats_[p];
                if (seat.wanted && !seat.pad && matches(seat)) target = static_cast<int>(p);
            }
        };
        if (light >= 0) pick([&](const Seat& seat) { return seat.light == light; });
        pick([&](const Seat& seat) { return !seat.guid.empty() && seat.guid == guid; });
        pick([](const Seat&) { return true; });
        if (target < 0) break;
        changed |= open(static_cast<unsigned>(target), instance);
    }
    return changed;
}

int PadSeats::missing() const {
    for (unsigned p = 0; p < players_; ++p) {
        if (seats_[p].lost && !seats_[p].pad) return static_cast<int>(p);
    }
    return -1;
}

int PadSeats::instance(unsigned player) const {
    return player < players_ && seats_[player].pad ? seats_[player].instance : -1;
}

std::uint16_t PadSeats::buttons(unsigned player) const {
    if (player >= players_) return 0;
    SDL_GameController* pad = seats_[player].pad;
    if (!pad) return 0;
    static constexpr SDL_GameControllerButton kMap[10] = {
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_LEFTSHOULDER};
    std::uint16_t out = 0;
    for (int bit = 0; bit < 10; ++bit) {
        if (SDL_GameControllerGetButton(pad, kMap[bit])) out |= 1u << bit;
    }
    const Sint16 x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    const Sint16 y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (x > kStickDeadzone) out |= 1u << 4;
    if (x < -kStickDeadzone) out |= 1u << 5;
    if (y < -kStickDeadzone) out |= 1u << 6;
    if (y > kStickDeadzone) out |= 1u << 7;
    return out;
}

}  // namespace fsr
