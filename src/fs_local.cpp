#include "fs_local.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdarg>
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
#include "fsr_revision.h"
#include "fs_layout.h"
#include "fs_pads.h"
#include "fs_save.h"
#if defined(FSR_WIDE_VIEW)
#include "fs_wide.h"
#endif
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

// What a session did, kept next to the executable so an unexpected exit can
// be reported and replayed (docs/DESIGN.md section 10):
//   fsr_local_last.log     settings, controllers, events and the error
//   fsr_local_last.inputs  every player's buttons, in FSR_LOCAL_INPUTS format
//   fsr_local_last.sav     the shared save as it was when the session began
// Each session overwrites the previous one's files.
class SessionLog {
public:
    explicit SessionLog(const std::filesystem::path& directory) : directory_(directory) {
        log_.open(directory / "fsr_local_last.log", std::ios::trunc);
    }

    void line(const char* format, ...) {
        char text[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        std::fprintf(stderr, "[fsr] local: %s\n", text);
        std::fflush(stderr);
        if (log_) log_ << text << std::endl;
    }

    void keep_save(const std::filesystem::path& save) {
        std::error_code ec;
        std::filesystem::copy_file(save, directory_ / "fsr_local_last.sav",
                                   std::filesystem::copy_options::overwrite_existing, ec);
    }

    void open_inputs(unsigned players) {
        inputs_.open(directory_ / "fsr_local_last.inputs", std::ios::trunc);
        inputs_ << "# FourSwordsRecomp local multiplayer, " << players
                << " players: frame, then each player's buttons (pressed = 1).\n"
                << "# Replay: FSR_LOCAL_INPUTS=<this file> with the same --local list and\n"
                << "# --save-path <a copy of fsr_local_last.sav>.\n";
    }

    void inputs(std::uint32_t tick, std::span<const std::uint16_t> buttons) {
        if (!inputs_ || (tick && std::equal(buttons.begin(), buttons.end(), last_.begin()))) return;
        inputs_ << tick;
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            inputs_ << ' ' << buttons[i];
            last_[i] = buttons[i];
        }
        inputs_ << std::endl;
    }

private:
    std::filesystem::path directory_;
    std::ofstream log_, inputs_;
    std::array<std::uint16_t, kLocalMaxPlayers> last_{};
};

// Testing aid: FSR_LOCAL_INPUTS names a FourSwordsLinkProbe input script
// ("frame buttons0 buttons1 ...", pressed = 1, held until the next line),
// added to the live controls. With --frames the session ends at that frame
// and FSR_LOCAL_DUMP, if set, receives the last window frame as a BMP.
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
    const int row = (w * 3 + 3) & ~3;
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

int run_session(const gbarecomp::GbaNetplayLaunch& launch, const gbarecomp::GbaNetplayBoot& boot,
                SessionLog& log) {
    using namespace gbarecomp;
    const LocalPlan plan = g_plan;
    const unsigned players = launch.player_count;
    if (!boot.bios || !boot.rom) throw std::runtime_error("local multiplayer needs the ROM and BIOS");

    {
        std::string files;
        for (unsigned id = 0; id < players; ++id) {
            files += (id ? "," : "") +
                     (plan.file[id] == kGuestFile ? std::string("guest") : std::to_string(plan.file[id] + 1));
        }
        log.line("FourSwordsRecomp v%s, commit %s%s, engine %s, patches %s", FSR_VERSION, FSR_GIT_COMMIT,
                 FSR_GIT_DIRTY ? " (with local changes)" : "", FSR_ENGINE_COMMIT, FSR_ENGINE_PATCHES);
        log.line("%u players, files %s, keyboard: %s, view %s, scale %d (%s), fullscreen %d, "
                 "screen \"%s\", frames %u, save %s",
                 players, files.c_str(),
                 plan.keyboard >= 0 ? ("player " + std::to_string(plan.keyboard + 1)).c_str() : "nobody",
                 plan.wide ? "16:9" : "3:2", boot.scale, plan.integer_scale ? "integer" : "fit",
                 boot.fullscreen, boot.screen.c_str(), boot.finish_tick, plan.save_path.string().c_str());
    }

    std::optional<save::Image> shared;
    std::string error;
    std::error_code ec;
    if (std::filesystem::exists(plan.save_path, ec)) {
        shared = save::Image::load(plan.save_path, &error);
        if (!shared) throw std::runtime_error(error);
        log.keep_save(plan.save_path);
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
            log.line("save: %s", error.c_str());
            return;
        }
        for (unsigned id : ids) log.line("player %u saved file %d", id + 1, plan.file[id] + 1);
    };

    // 16:9: each GBA gets a host-only mirror PPU that draws 284 columns, the
    // extra ones from the stage's own map (fs_wide.cpp). The GBAs themselves
    // stay 240 wide, so the game and the link are the same in both views.
    int view_w = kWidth;
#if defined(FSR_WIDE_VIEW)
    std::vector<std::unique_ptr<GbaNetplayPresentation>> mirrors;
    struct Detach {
        GbaMultiplayerSession& session;
        unsigned players;
        ~Detach() {
            for (unsigned id = 0; id < players; ++id) session.machine(id).ppu.set_presentation_observer(nullptr);
        }
    } detach{session, players};
    if (plan.wide) {
        view_w = static_cast<int>(kWideViewWidth);
        for (unsigned id = 0; id < players; ++id) {
            mirrors.push_back(std::make_unique<GbaNetplayPresentation>(wide_view_policy()));
            mirrors.back()->request_width(kWideViewWidth);
            session.machine(id).ppu.set_presentation_observer(mirrors.back().get());
        }
        log.line("view: 16:9, %d columns", view_w);
    }
#else
    if (plan.wide) log.line("view: 16:9 is not in this build; using 3:2");
#endif

    // The window starts at the views' own size times the launcher's scale and
    // can be resized freely; the picture is redrawn for whatever size it has.
    const int columns = 2, rows = players > 2 ? 2 : 1;
    const int native_w = view_w * columns + kViewGap * (columns - 1);
    const int native_h = kHeight * rows + kViewGap * (rows - 1);
    const int scale = rows > 1 ? std::max(1, boot.scale - 1) : boot.scale;
    const std::string title = boot.title + " - Local multiplayer (" + std::to_string(players) + " players)";
    HostWindow window;
    if (!window.open(scale, native_w, native_h, title.c_str(),
                     boot.screen.empty() ? nullptr : boot.screen.c_str(),
                     false, false, true, true)) {
        throw std::runtime_error("cannot open the local multiplayer window");
    }
    window.load_input_config(boot.config_directory.string().c_str(), false);
    // Exclusive fullscreen would switch the monitor to the mode nearest the
    // window (1024x768 for four views at 2x on a 1080p panel), which the
    // monitor then rescales. Fullscreen here is always the desktop's own mode.
    window.set_fullscreen(boot.fullscreen ? 1 : 0);
    window.set_volume(boot.volume);
    window.set_fps_readout(boot.show_fps);
    window.set_gamepad_enabled(false);   // PadSeats (fs_pads.h) owns every controller

    PadSeats seats(plan.pad, players, [&log](const std::string& text) { log.line("%s", text.c_str()); });
    const auto script = scripted_inputs(players);
    log.open_inputs(players);
    std::uint32_t tick = 0;
    std::vector<std::uint8_t> canvas;
    int canvas_w = 0, canvas_h = 0;
    LocalLayout layout;
    ViewScaler scaler;
    std::array<std::uint16_t, kLocalMaxPlayers> buttons{};
    std::vector<std::int16_t> audio(4096);
    std::array<bool, kLocalMaxPlayers> pending{};
    std::array<unsigned, kLocalMaxPlayers> quiet{};
    bool paused = false, waiting_for_pad = false;
    auto next = Clock::now();

    for (;;) {
        const auto events = window.pump();
        if (events.quit || events.terminating || quit_to_menu()) break;
        if (events.toggle_pause) {
            paused = !paused;
            waiting_for_pad = false;
        }
        if (events.toggle_fullscreen) window.set_fullscreen(window.fullscreen() ? 0 : 1);
        if (events.window_bigger || events.window_smaller) {
            window.set_surface_size(native_w, native_h);   // the scale steps are in views
            window.adjust_scale(events.window_bigger ? 1 : -1);
        }
        if (events.volume_up) window.set_volume(window.volume() + 5);
        if (events.volume_down) window.set_volume(window.volume() - 5);
        if (events.toggle_fps) window.set_fps_readout(!window.fps_readout());

        // A player whose controller dropped out waits with the game paused
        // until it is back; the pause key overrides that either way.
        if (seats.poll()) {
            const int missing = seats.missing();
            if (missing >= 0) {
                if (!paused) waiting_for_pad = paused = true;
                window.set_title((title + " - Player " + std::to_string(missing + 1) +
                                  ": reconnect the controller").c_str());
            } else {
                if (waiting_for_pad) waiting_for_pad = paused = false;
                window.set_title(title.c_str());
            }
        }

        if (!paused) {
            for (unsigned id = 0; id < players; ++id) buttons[id] = seats.buttons(id);
            if (plan.keyboard >= 0 && plan.keyboard < static_cast<int>(players)) {
                buttons[plan.keyboard] |= static_cast<std::uint16_t>(~events.keyinput & 0x3FF);
            }
            if (!script.empty()) {
                const auto& row = script[std::min<std::size_t>(tick, script.size() - 1)];
                for (unsigned id = 0; id < players; ++id) buttons[id] |= row[id];
            }
            log.inputs(tick, std::span<const std::uint16_t>(buttons.data(), players));
            try {
                session.run_frame(std::span(buttons.data(), players));
            } catch (...) {
                log.line("stopped in frame %u", tick);
                for (unsigned id = 0; id < players; ++id) {
                    const std::uint8_t* iw = session.machine(id).bus.iwram_ptr();
                    log.line("  player %u: mode=%u sub=%u step=%u", id + 1, iw[0x6D10], iw[0x5050], iw[0x0BFC]);
                }
                throw;
            }
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

        // The canvas is the window's own pixels; each view is scaled into its
        // place (fs_layout.h), so nothing rescales the picture afterwards.
        int drawable_w = 0, drawable_h = 0;
        if (window.drawable_size(&drawable_w, &drawable_h) && drawable_w >= 16 && drawable_h >= 16) {
            if (drawable_w != canvas_w || drawable_h != canvas_h) {
                canvas_w = drawable_w;
                canvas_h = drawable_h;
                canvas.assign(static_cast<std::size_t>(canvas_w) * canvas_h * 3, 0);
                layout = local_layout(static_cast<int>(players), canvas_w, canvas_h, view_w, kHeight,
                                      plan.integer_scale);
                scaler.resize(view_w, kHeight, layout.view[0].w, layout.view[0].h);
                log.line("window %dx%d, each view %dx%d (%.3fx)", canvas_w, canvas_h, layout.view[0].w,
                         layout.view[0].h, static_cast<double>(layout.view[0].w) / view_w);
            }
            window.set_surface_size(canvas_w, canvas_h);
            const std::size_t stride = static_cast<std::size_t>(canvas_w) * 3;
            for (unsigned id = 0; id < players; ++id) {
                const ViewRect& view = layout.view[id];
                if (view.w < 1 || view.h < 1) continue;
                const std::uint8_t* pixels = nullptr;
#if defined(FSR_WIDE_VIEW)
                // The mirror has no picture until its first whole frame.
                if (id < mirrors.size() && mirrors[id]->width() == static_cast<unsigned>(view_w)) {
                    pixels = mirrors[id]->pixels();
                }
                if (id < mirrors.size() && !pixels) continue;
#endif
                if (!pixels) pixels = session.machine(id).ppu.latched_framebuffer();
                scaler.blit(pixels, canvas.data() + view.y * stride + static_cast<std::size_t>(view.x) * 3,
                            stride);
            }
            window.present(canvas.data());
        }
        if (boot.finish_tick && tick >= boot.finish_tick) {
            if (const char* dump = std::getenv("FSR_LOCAL_DUMP"); dump && *dump && !canvas.empty()) {
                write_bmp(dump, canvas, canvas_w, canvas_h);
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
    log.line("session ended at frame %u", tick);
    return 0;
}

// Any error here used to close the program with nothing on screen (the
// runtime only prints it). It is now logged, and shown unless the run is
// scripted.
int run_local(const gbarecomp::GbaNetplayLaunch& launch, const gbarecomp::GbaNetplayBoot& boot) {
    SessionLog log(boot.config_directory);
    try {
        return run_session(launch, boot, log);
    } catch (const std::exception& e) {
        log.line("error: %s", e.what());
        if (!boot.finish_tick) {
            const std::string text = std::string("Local multiplayer stopped:\n\n") + e.what() +
                                     "\n\nDetails: fsr_local_last.log, next to the program.";
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "FourSwordsRecomp", text.c_str(), nullptr);
        }
        throw;
    }
}

}  // namespace

bool valid_local_plan(const LocalPlan& plan, const std::array<save::Slot, save::kSlotCount>& slots) {
    if (plan.players < 2 || plan.players > kLocalMaxPlayers) return false;
    if (plan.keyboard < -1 || plan.keyboard >= plan.players) return false;
    for (int a = 0; a < plan.players; ++a) {
        const int file = plan.file[a];
        if (file != kGuestFile && (file < 0 || file >= save::kSlotCount || !slots[file].used)) return false;
        if (plan.pad[a] < kNoPad) return false;
        for (int b = a + 1; b < plan.players; ++b) {
            if (file != kGuestFile && plan.file[b] == file) return false;
            if (plan.pad[a] != kNoPad && plan.pad[b] == plan.pad[a]) return false;
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
