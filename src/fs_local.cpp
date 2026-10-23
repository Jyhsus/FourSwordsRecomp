#include "fs_local.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "fs_boot.h"
#include "fs_save.h"
#include "host_window.h"
#include "multiplayer_launch.h"
#include "multiplayer_session.h"

namespace fsr {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kWidth = 240, kHeight = 160;
constexpr auto kFramePeriod = std::chrono::nanoseconds(280896LL * 1000000000LL / 16777216LL);
// The game writes a file as many EEPROM blocks over several frames; a GBA's
// file is merged once its EEPROM has been idle this long, never mid-write.
constexpr unsigned kSaveQuietFrames = 60;

LocalPlan g_plan;

// Testing aid: FSR_LOCAL_INPUTS names a FourSwordsLinkProbe input script
// ("frame buttons0 buttons1 ...", pressed = 1, held until the next line),
// added to the live controls. With --frames the session ends at that frame
// and FSR_LOCAL_DUMP, if set, receives the last split-screen frame as a BMP.
std::vector<std::array<std::uint16_t, kLocalMaxPlayers>> scripted_inputs(unsigned players) {
    std::vector<std::array<std::uint16_t, kLocalMaxPlayers>> rows;
    const char* path = std::getenv("FSR_LOCAL_INPUTS");
    if (!path || !*path) return rows;
    std::ifstream script(path);
    if (!script) throw std::runtime_error(std::string("cannot open ") + path);
    std::string line;
    while (std::getline(script, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        unsigned frame = 0;
        std::array<std::uint16_t, kLocalMaxPlayers> keys{};
        row >> std::setbase(0) >> frame;
        for (unsigned id = 0; id < players; ++id) {
            unsigned key = 0;
            row >> std::setbase(0) >> key;
            keys[id] = static_cast<std::uint16_t>(key & 0x3FF);
        }
        if (!row && !row.eof()) throw std::runtime_error("bad input line: " + line);
        if (rows.size() <= frame) {
            rows.resize(frame + 1, rows.empty() ? std::array<std::uint16_t, kLocalMaxPlayers>{} : rows.back());
        }
        rows[frame] = keys;
    }
    return rows;
}

void write_bmp(const char* path, const std::vector<std::uint8_t>& rgb, int w, int h) {
    const int row = w * 3;
    std::vector<std::uint8_t> out(54 + static_cast<std::size_t>(row) * h, 0);
    auto put32 = [&](std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    };
    out[0] = 'B'; out[1] = 'M';
    put32(2, static_cast<std::uint32_t>(out.size()));
    put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
    out[26] = 1; out[28] = 24;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::uint8_t* p = rgb.data() + (static_cast<std::size_t>(y) * w + x) * 3;
            std::uint8_t* q = out.data() + 54 + static_cast<std::size_t>(h - 1 - y) * row + x * 3;
            q[0] = p[2]; q[1] = p[1]; q[2] = p[0];
        }
    }
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(out.data()),
                                                static_cast<std::streamsize>(out.size()));
}

// Each GBA's EEPROM at power-on.
save::Image starting_image(const std::optional<save::Image>& shared, int file, int player,
                           const std::vector<std::uint8_t>& rom) {
    std::string error;
    if (file != kGuestFile) {
        if (!shared) throw std::runtime_error("no save file to play from");
        save::Image image = *shared;
        if (!image.set_current_file(file, &error)) throw std::runtime_error(error);
        return image;
    }
    std::optional<save::Image> guest = save::Image::blank(rom, &error);
    if (!guest || !guest->create(0, "Guest" + std::to_string(player + 1), &error) ||
        !guest->set_current_file(0, &error)) {
        throw std::runtime_error("guest file: " + error);
    }
    return *guest;
}

// Controllers in SDL device order. HostWindow already gives the first one to
// player 1 together with the keyboard; controller N plays player N.
class Pads {
public:
    ~Pads() { close(); }

    void refresh() {
        const int count = SDL_NumJoysticks();
        if (count == devices_) return;
        close();
        devices_ = count;
        for (int i = 0; i < count; ++i) {
            if (!SDL_IsGameController(i)) continue;
            if (SDL_GameController* pad = SDL_GameControllerOpen(i)) pads_.push_back(pad);
        }
    }

    std::size_t size() const { return pads_.size(); }

    // Pressed buttons, GBA bit order (pressed = 1), as HostWindow maps them.
    std::uint16_t buttons(std::size_t index) const {
        if (index >= pads_.size() || !SDL_GameControllerGetAttached(pads_[index])) return 0;
        static constexpr SDL_GameControllerButton kMap[10] = {
            SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
            SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START,
            SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
            SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
            SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_LEFTSHOULDER};
        std::uint16_t out = 0;
        for (int bit = 0; bit < 10; ++bit) {
            if (SDL_GameControllerGetButton(pads_[index], kMap[bit])) out |= 1u << bit;
        }
        return out;
    }

private:
    void close() {
        for (SDL_GameController* pad : pads_) SDL_GameControllerClose(pad);
        pads_.clear();
    }

    std::vector<SDL_GameController*> pads_;
    int devices_ = -1;
};

int run_local(const gbarecomp::GbaNetplayLaunch& launch, const gbarecomp::GbaNetplayBoot& boot) {
    using namespace gbarecomp;
    const LocalPlan plan = g_plan;
    const unsigned players = launch.player_count;
    if (!boot.bios || !boot.rom) throw std::runtime_error("local multiplayer needs the ROM and BIOS");

    std::optional<save::Image> shared;
    std::string error;
    std::error_code ec;
    if (std::filesystem::exists(plan.save_path, ec)) {
        shared = save::Image::load(plan.save_path, &error);
        if (!shared) throw std::runtime_error(error);
    }

    set_boot_slot(kBootSavedFile);

    GbaSessionConfig config;
    config.links = {{GbaLinkMedium::Cable, {}}};
    for (unsigned id = 0; id < players; ++id) {
        config.machines.push_back({id, launch.program_id, boot.expected_rom_sha1});
        config.links[0].machines.push_back(id);  // machine ID == cable port == player
        config.input_machines.push_back(id);
    }
    GbaMultiplayerSession session(config);
    session.set_native_slices(true);
    for (unsigned id = 0; id < players; ++id) {
        GbaInstance& m = session.machine(id);
        m.bus.set_bios(boot.bios);
        m.bus.set_rom(boot.rom->data(), boot.rom->size());
        m.bus.save().configure_eeprom(save::kImageSize);
        setup_netplay_instance(m);
        const auto bytes = starting_image(shared, plan.file[id], id, *boot.rom).sav_bytes();
        if (!m.bus.save().load_eeprom_bytes(bytes.data(), bytes.size())) {
            throw std::runtime_error("save does not fit the cartridge");
        }
        m.bus.save().clear_dirty();
    }

    // Merges the files of the GBAs in `ids` into the shared .sav.
    auto merge = [&](const std::vector<unsigned>& ids) {
        if (ids.empty() || !shared) return;
        for (unsigned id : ids) {
            auto image = save::Image::from_sav(session.machine(id).bus.save().eeprom_bytes(), &error);
            if (image) shared->copy_file(*image, plan.file[id]);
        }
        if (!shared->store(plan.save_path, &error)) {
            std::fprintf(stderr, "[fsr] local multiplayer: %s\n", error.c_str());
            return;
        }
        for (unsigned id : ids) {
            std::printf("[fsr] local multiplayer: player %u saved file %d\n", id + 1, plan.file[id] + 1);
        }
    };

    const int columns = 2, rows = players > 2 ? 2 : 1;
    const int surface_w = kWidth * columns, surface_h = kHeight * rows;
    const int scale = rows > 1 ? std::max(1, boot.scale - 1) : boot.scale;
    const std::string title = boot.title + " - Local multiplayer (" + std::to_string(players) + " players)";
    HostWindow window;
    if (!window.open(scale, surface_w, surface_h, title.c_str(),
                     boot.screen.empty() ? nullptr : boot.screen.c_str(),
                     boot.linear_filter, boot.sharp_filter, false, true)) {
        throw std::runtime_error("cannot open the local multiplayer window");
    }
    window.load_input_config(boot.config_directory.string().c_str(), false);
    window.set_fullscreen(boot.fullscreen);
    window.set_volume(boot.volume);
    window.set_fps_readout(boot.show_fps);

    Pads pads;
    const auto script = scripted_inputs(players);
    std::uint32_t tick = 0;
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(surface_w) * surface_h * 3, 0);
    std::array<std::uint16_t, kLocalMaxPlayers> buttons{};
    std::vector<std::int16_t> audio(4096);
    std::array<bool, kLocalMaxPlayers> pending{};
    std::array<unsigned, kLocalMaxPlayers> quiet{};
    bool paused = false;
    auto next = Clock::now();

    for (;;) {
        const auto events = window.pump();
        if (events.quit || events.terminating || quit_to_menu()) break;
        if (events.toggle_pause) paused = !paused;
        if (events.toggle_fullscreen) window.set_fullscreen(window.fullscreen() ? 0 : 1);
        if (events.window_bigger) window.adjust_scale(1);
        if (events.window_smaller) window.adjust_scale(-1);
        if (events.volume_up) window.set_volume(window.volume() + 5);
        if (events.volume_down) window.set_volume(window.volume() - 5);
        if (events.toggle_fps) window.set_fps_readout(!window.fps_readout());

        if (!paused) {
            pads.refresh();
            buttons[0] = static_cast<std::uint16_t>(~events.keyinput & 0x3FF);
            for (unsigned id = 1; id < players; ++id) buttons[id] = pads.buttons(id);
            if (!script.empty()) {
                const auto& row = script[std::min<std::size_t>(tick, script.size() - 1)];
                for (unsigned id = 0; id < players; ++id) buttons[id] |= row[id];
            }
            session.run_frame(std::span(buttons.data(), players));
            ++tick;

            auto& sound = session.machine(0).bus.audio();
            while (const std::size_t n = sound.drain_samples(audio.data(), audio.size())) {
                window.push_audio_samples(audio.data(), n);
            }
            for (unsigned id = 1; id < players; ++id) session.machine(id).bus.audio().discard_output();

            std::vector<unsigned> idle;
            for (unsigned id = 0; id < players; ++id) {
                if (plan.file[id] == kGuestFile) continue;
                auto& eeprom = session.machine(id).bus.save();
                if (eeprom.dirty()) {
                    eeprom.clear_dirty();
                    pending[id] = true;
                    quiet[id] = 0;
                } else if (pending[id] && ++quiet[id] >= kSaveQuietFrames) {
                    pending[id] = false;
                    idle.push_back(id);
                }
            }
            merge(idle);
        }

        for (unsigned id = 0; id < players; ++id) {
            const std::uint8_t* src = session.machine(id).ppu.latched_framebuffer();
            const int x0 = static_cast<int>(id % 2) * kWidth, y0 = static_cast<int>(id / 2) * kHeight;
            for (int y = 0; y < kHeight; ++y) {
                std::memcpy(frame.data() + (static_cast<std::size_t>(y0 + y) * surface_w + x0) * 3,
                            src + static_cast<std::size_t>(y) * kWidth * 3, kWidth * 3);
            }
        }
        window.present(frame.data());
        if (boot.finish_tick && tick >= boot.finish_tick) {
            if (const char* dump = std::getenv("FSR_LOCAL_DUMP"); dump && *dump) {
                write_bmp(dump, frame, surface_w, surface_h);
            }
            break;
        }

        next += kFramePeriod;
        const auto now = Clock::now();
        if (now > next + kFramePeriod * 4) {
            next = now;  // a long stall: drop the backlog
        } else {
            std::this_thread::sleep_until(next - std::chrono::milliseconds(1));
            while (Clock::now() < next) std::this_thread::yield();
        }
    }

    std::vector<unsigned> unsaved;
    for (unsigned id = 0; id < players; ++id) {
        if (pending[id] || session.machine(id).bus.save().dirty()) {
            if (plan.file[id] != kGuestFile) unsaved.push_back(id);
        }
    }
    merge(unsaved);
    return 0;
}

}  // namespace

bool valid_local_plan(const LocalPlan& plan, const std::array<save::Slot, save::kSlotCount>& slots) {
    if (plan.players < 2 || plan.players > kLocalMaxPlayers) return false;
    for (int a = 0; a < plan.players; ++a) {
        const int file = plan.file[a];
        if (file == kGuestFile) continue;
        if (file < 0 || file >= save::kSlotCount || !slots[file].used) return false;
        for (int b = a + 1; b < plan.players; ++b) {
            if (plan.file[b] == file) return false;
        }
    }
    return true;
}

std::shared_ptr<gbarecomp::GbaNetplayLaunch> make_local_launch(const LocalPlan& plan) {
    g_plan = plan;
    auto launch = std::make_shared<gbarecomp::GbaNetplayLaunch>();
    launch->enabled = true;
    launch->program_id = "four-swords-usa";
    launch->max_players = kLocalMaxPlayers;
    launch->player_count = static_cast<unsigned>(std::clamp(plan.players, 2, kLocalMaxPlayers));
    launch->setup_instance = &setup_netplay_instance;
    launch->run = &run_local;
    return launch;
}

}  // namespace fsr
