#include "fs_local.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cmath>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
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
#include "fs_sysinfo.h"
#if defined(FSR_WIDE_VIEW)
#include "fs_wide.h"
#endif
#include "host_platform.h"
#include "host_window.h"
#include "multiplayer_launch.h"
#include "multiplayer_session.h"

namespace fsr {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kWidth = 240, kHeight = 160;
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
//
// Nothing here writes to disk on the frame loop's thread: lines and input
// rows are queued and a writer thread appends and flushes them. On the
// project's 5,400 rpm disk a flushed line every 600 frames took 20-140 ms
// while another program was writing to the same disk (24 Nov 2026 session),
// a visible stall each time. The destructor writes whatever is still queued.
class SessionLog {
public:
    // Every session writes its evidence twice: fsr_local_last.* next to the
    // executable (overwritten by the next session) and a folder of its own,
    // sessions/YYYY-MM-DD_HH-MM-SS/, that is never overwritten:
    //   session.log, inputs.inputs, start.sav, config.ini, keybinds.ini
    explicit SessionLog(const std::filesystem::path& directory) : directory_(directory) {
        log_[0].open(directory / "fsr_local_last.log", std::ios::trunc);
        const std::time_t now = std::time(nullptr);
        char stamp[32] = "session";
        if (const std::tm* local = std::localtime(&now)) {
            std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", local);
        }
        std::error_code ec;
        folder_ = directory / "sessions" / stamp;
        for (int n = 2; std::filesystem::exists(folder_, ec); ++n) {
            folder_ = directory / "sessions" / (std::string(stamp) + "_" + std::to_string(n));
        }
        if (std::filesystem::create_directories(folder_, ec)) {
            log_[1].open(folder_ / "session.log", std::ios::trunc);
            for (const char* name : {"config.ini", "keybinds.ini"}) {
                std::filesystem::copy_file(directory / name, folder_ / name,
                                           std::filesystem::copy_options::overwrite_existing, ec);
            }
        } else {
            folder_.clear();
        }
        writer_ = std::thread([this] { write_queued(); });
    }

    ~SessionLog() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_one();
        if (writer_.joinable()) writer_.join();
    }

    SessionLog(const SessionLog&) = delete;
    SessionLog& operator=(const SessionLog&) = delete;

    const std::filesystem::path& folder() const { return folder_; }

    void line(const char* format, ...) {
        char text[1024];
        va_list args;
        va_start(args, format);
        std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        queue(kLine, text);
    }

    void keep_save(const std::filesystem::path& save) {
        std::error_code ec;
        std::filesystem::copy_file(save, directory_ / "fsr_local_last.sav",
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (!folder_.empty()) {
            std::filesystem::copy_file(save, folder_ / "start.sav",
                                       std::filesystem::copy_options::overwrite_existing, ec);
        }
    }

    void open_inputs(unsigned players) {
        // Opened here; from now on only the writer thread touches them.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            inputs_[0].open(directory_ / "fsr_local_last.inputs", std::ios::trunc);
            if (!folder_.empty()) inputs_[1].open(folder_ / "inputs.inputs", std::ios::trunc);
        }
        queue(kInputs, "# FourSwordsRecomp local multiplayer, " + std::to_string(players) +
                           " players: frame, then each player's buttons (pressed = 1).\n"
                           "# Replay: FSR_LOCAL_INPUTS=<this file> with the same --local list and\n"
                           "# --save-path <a copy of the session's start save>.");
    }

    void inputs(std::uint32_t tick, std::span<const std::uint16_t> buttons) {
        if (tick && std::equal(buttons.begin(), buttons.end(), last_.begin())) return;
        for (std::size_t i = 0; i < buttons.size(); ++i) last_[i] = buttons[i];
        std::string row = std::to_string(tick);
        for (std::size_t i = 0; i < buttons.size(); ++i) row += ' ' + std::to_string(buttons[i]);
        queue(kInputs, std::move(row));
    }

private:
    enum Target { kLine, kInputs };
    struct Item {
        Target target;
        std::string text;
    };

    void queue(Target target, std::string text) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back({target, std::move(text)});
        }
        wake_.notify_one();
    }

    // The writer thread: takes everything queued, writes it, flushes once.
    void write_queued() {
        std::deque<Item> batch;
        for (;;) {
            bool stop = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return stop_ || !pending_.empty(); });
                batch.swap(pending_);
                stop = stop_ && batch.empty();
            }
            if (stop) return;
            bool lines = false, rows = false;
            for (const Item& item : batch) {
                if (item.target == kLine) {
                    std::fprintf(stderr, "[fsr] local: %s\n", item.text.c_str());
                    for (auto& log : log_) {
                        if (log) log << item.text << '\n';
                    }
                    lines = true;
                } else {
                    for (auto& inputs : inputs_) {
                        if (inputs) inputs << item.text << '\n';
                    }
                    rows = true;
                }
            }
            batch.clear();
            if (lines) {
                std::fflush(stderr);
                for (auto& log : log_) log.flush();
            }
            if (rows) {
                for (auto& inputs : inputs_) inputs.flush();
            }
        }
    }

    std::filesystem::path directory_, folder_;
    std::array<std::ofstream, 2> log_, inputs_;
    std::array<std::uint16_t, kLocalMaxPlayers> last_{};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Item> pending_;
    bool stop_ = false;
    std::thread writer_;   // last: started once everything above exists
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
        if (!log.folder().empty()) log.line("evidence folder: %s", log.folder().string().c_str());
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
    std::vector<std::uint8_t> banded;   // a mirror picture with its bands filled
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
            // Only the mirror's picture is shown. The GBA's own PPU keeps its
            // timing and every register the game can read, and stops drawing
            // a 240-wide picture nobody sees: with four GBAs that drawing was
            // what kept 16:9 from running at full speed (DESIGN section 11).
            session.machine(id).ppu.set_rasterization_enabled(false);
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
    // The engine's frame limiter, as in single player: a high-resolution
    // waitable timer on Windows, and a late frame is never paid back with a
    // short one. The plain sleep_until used before woke on Windows' ~15.6 ms
    // tick: 16.74 +- 4.5 ms between presents (24 Nov 2026 measurements).
    gbarecomp::FramePacer pacer;
    // Work per loop (emulating every GBA, drawing and presenting), against
    // the GBA's 16.74 ms frame: logged every 600 frames and at the end, so a
    // session shows whether this machine kept up (docs/PLAYTEST.md).
    // Split into emulation (every GBA), drawing (scaling the views) and
    // presenting (upload and show), so a slow session shows which part.
    struct Timing {
        std::uint32_t frames = 0, late = 0, stalls = 0;
        double total_ms = 0, worst_ms = 0, emu_ms = 0, draw_ms = 0, present_ms = 0;
        void add(double ms, double emu, double draw, double present) {
            ++frames; total_ms += ms; worst_ms = std::max(worst_ms, ms);
            emu_ms += emu; draw_ms += draw; present_ms += present;
            late += ms > 16.743;
        }
    } timing, all_timing;
    double emu_ms = 0, draw_ms = 0, present_ms = 0, events_ms = 0, save_ms = 0, log_ms = 0, audio_ms = 0;
    // Frames whose work takes more than two GBA frames are listed one by one
    // (at most kSlowFramesListed per session) with each part and the page
    // faults they caused, to tell game work from memory brought in from disk.
    constexpr double kSlowFrameMs = 33.5;
    constexpr unsigned kSlowFramesListed = 300;
    unsigned slow_listed = 0;
    std::uint64_t faults_before = process_page_faults();
    // Time between consecutive presents: how evenly frames reach the screen.
    std::array<std::uint32_t, 6> interval_counts{};   // <12, 12-15, 15-18.5, 18.5-25, 25-35, >=35 ms
    double interval_sum = 0, interval_sq = 0;
    std::uint32_t intervals = 0;
    Clock::time_point last_present{};
    log.line("vsync requested: %s (GBARECOMP_VSYNC)",
             std::getenv("GBARECOMP_VSYNC") && *std::getenv("GBARECOMP_VSYNC") &&
                     *std::getenv("GBARECOMP_VSYNC") != '0' ? "yes" : "no");
    const auto since = [](Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    };
    const auto report = [&log](const char* what, const Timing& t) {
        if (!t.frames) return;
        log.line("%s: %u frames, work %.2f ms average (emulation %.2f, drawing %.2f, presenting %.2f), "
                 "%.2f ms worst, %u over 16.74 ms, %u stalls",
                 what, t.frames, t.total_ms / t.frames, t.emu_ms / t.frames, t.draw_ms / t.frames,
                 t.present_ms / t.frames, t.worst_ms, t.late, t.stalls);
    };

    for (;;) {
        const auto work_start = Clock::now();
        const auto events = window.pump();
        events_ms = std::chrono::duration<double, std::milli>(Clock::now() - work_start).count();
        save_ms = log_ms = audio_ms = emu_ms = draw_ms = present_ms = 0;
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
            const auto log_start = Clock::now();
            log.inputs(tick, std::span<const std::uint16_t>(buttons.data(), players));
            log_ms = since(log_start);
            const auto emu_start = Clock::now();
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
            emu_ms = since(emu_start);
            ++tick;
            if (tick % 600 == 0) {
                // Mode, stage state, stage, floor and camera of every GBA:
                // where each player was if something goes wrong later.
                std::string where = "frame " + std::to_string(tick) + ":";
                for (unsigned id = 0; id < players; ++id) {
                    const std::uint8_t* iw = session.machine(id).bus.iwram_ptr();
                    char part[96];
                    std::snprintf(part, sizeof(part), " P%u mode=%u state=%u stage=%u floor=%u camera=%u,%u;",
                                  id + 1, iw[0x6D10], iw[0x5050], iw[0x6C87], iw[0x6C84],
                                  iw[0x470] | (iw[0x471] << 8), iw[0x472] | (iw[0x473] << 8));
                    where += part;
                }
                where.pop_back();
                const auto line_start = Clock::now();
                log.line("%s", where.c_str());
                log_ms += since(line_start);
            }

            const auto audio_start = Clock::now();
            auto& sound = session.machine(0).bus.audio();
            while (const std::size_t n = sound.drain_samples(audio.data(), audio.size())) {
                window.push_audio_samples(audio.data(), n);
            }
            for (unsigned id = 1; id < players; ++id) session.machine(id).bus.audio().discard_output();
            audio_ms = since(audio_start);

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
            const auto save_start = Clock::now();
            merge(idle);
            save_ms = std::chrono::duration<double, std::milli>(Clock::now() - save_start).count();
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
            const auto draw_start = Clock::now();
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
                // A 3:2 screen inside the 16:9 view: fixed-colour bands
                // (fs_wide.h) instead of the mirror's black margins.
                if (pixels && id < mirrors.size() && wide_bands(session.machine(id).bus)) {
                    const int side = (view_w - kWidth) / 2;
                    banded.assign(pixels, pixels + static_cast<std::size_t>(view_w) * kHeight * 3);
                    for (int y = 0; y < kHeight; ++y) {
                        std::uint8_t* row = banded.data() + static_cast<std::size_t>(y) * view_w * 3;
                        for (int x = 0; x < view_w; ++x) {
                            if (x >= side && x < side + kWidth) continue;
                            std::memcpy(row + x * 3, kWideBandColour, 3);
                        }
                    }
                    pixels = banded.data();
                }
#endif
                if (!pixels) pixels = session.machine(id).ppu.latched_framebuffer();
                scaler.blit(pixels, canvas.data() + view.y * stride + static_cast<std::size_t>(view.x) * 3,
                            stride);
            }
            draw_ms = since(draw_start);
            const auto present_start = Clock::now();
            window.present(canvas.data());
            present_ms = since(present_start);
            const auto presented = Clock::now();
            if (last_present != Clock::time_point{}) {
                const double gap = std::chrono::duration<double, std::milli>(presented - last_present).count();
                ++interval_counts[gap < 12 ? 0 : gap < 15 ? 1 : gap < 18.5 ? 2 : gap < 25 ? 3 : gap < 35 ? 4 : 5];
                if (gap >= 35) {   // more than two GBA frames between pictures
                    ++timing.stalls;
                    ++all_timing.stalls;
                }
                interval_sum += gap;
                interval_sq += gap * gap;
                ++intervals;
            }
            last_present = presented;
        }
        if (boot.finish_tick && tick >= boot.finish_tick) {
            if (const char* dump = std::getenv("FSR_LOCAL_DUMP"); dump && *dump && !canvas.empty()) {
                write_bmp(dump, canvas, canvas_w, canvas_h);
            }
            break;
        }

        if (!paused) {
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - work_start).count();
            timing.add(ms, emu_ms, draw_ms, present_ms);
            all_timing.add(ms, emu_ms, draw_ms, present_ms);
            const std::uint64_t faults = process_page_faults();
            if (ms > kSlowFrameMs && slow_listed < kSlowFramesListed) {
                std::string where;
                for (unsigned id = 0; id < players; ++id) {
                    const std::uint8_t* iw = session.machine(id).bus.iwram_ptr();
                    char part[64];
                    std::snprintf(part, sizeof(part), " P%u mode=%u state=%u stage=%u floor=%u", id + 1,
                                  iw[0x6D10], iw[0x5050], iw[0x6C87], iw[0x6C84]);
                    where += part;
                }
                log.line("slow frame %u: %.1f ms (events %.1f, logging %.1f, emulation %.1f, audio %.1f, "
                         "saving %.1f, drawing %.1f, presenting %.1f), page faults +%llu;%s",
                         tick, ms, events_ms, log_ms, emu_ms, audio_ms, save_ms, draw_ms, present_ms,
                         static_cast<unsigned long long>(faults - faults_before), where.c_str());
                if (++slow_listed == kSlowFramesListed) log.line("further slow frames are not listed");
            }
            faults_before = faults;
            if (timing.frames == 600) {
                report(("frames " + std::to_string(tick - 599) + "-" + std::to_string(tick)).c_str(), timing);
                timing = {};
            }
        }
        pacer.wait_for_next_frame();
    }

    std::vector<unsigned> unsaved;
    for (unsigned id = 0; id < players; ++id) {
        if (pending[id] || session.machine(id).bus.save().dirty()) {
            if (plan.file[id] != kGuestFile) unsaved.push_back(id);
        }
    }
    merge(unsaved);
    report("whole session", all_timing);
    if (intervals) {
        const double mean = interval_sum / intervals;
        const double sd = std::sqrt(std::max(0.0, interval_sq / intervals - mean * mean));
        log.line("time between presents: %.2f ms average, %.2f ms standard deviation; <12 ms %u, 12-15 %u, "
                 "15-18.5 %u, 18.5-25 %u, 25-35 %u, >=35 %u",
                 mean, sd, interval_counts[0], interval_counts[1], interval_counts[2], interval_counts[3],
                 interval_counts[4], interval_counts[5]);
    }
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
