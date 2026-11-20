// link_probe.cpp — M3: two (to four) virtual GBAs on one GbaLinkHub cable,
// in one process, with no network (docs/DESIGN.md section 7, M3).
//
//   FourSwordsLinkProbe --rom R --bios B --out PREFIX --frames N
//       --save0 A.sav --save1 B.sav [--save2 .. --save3 ..]
//       [--slot 1-3] [--inputs FILE] [--shots F,F,..] [--trace K]
//       [--load PREFIX.state] [--wide W [--room 1] [--no-raster 1]] [--dump 1] [--oam-log F] [--map-log F] [--poke F:ADDR:BYTE,...]
//
// Every machine runs the same generated code and the same fs_boot hooks as
// the game executable; --slot boots every machine into that file of its own
// save. --inputs lines are "frame buttons0 buttons1 ..." (pressed = 1, GBA
// bit order: A=1 B=2 Select=4 Start=8 Right=16 Left=32 Up=64 Down=128 R=256
// L=512), held until the next line. --shots writes PREFIX-fF-seatN.bmp.
// --wide W also renders each machine W columns wide through a host-only
// mirror (PREFIX-fF-seatN-wide.bmp); --dump 1 writes each machine's memory
// at every shot (PREFIX-fF-seatN.mem: EWRAM, IWRAM, VRAM, palette, OAM, IO).
// A save named "guest" is the one-file save local multiplayer gives a guest.
// --oam-log F writes PREFIX-oam-seatN.txt from frame F: camera and sprites per frame.
// --map-log F writes PREFIX-map-seatN.txt from frame F: mode, camera, stage size and how
// many on-screen tiles differ from the stage buffers the wide view reads.
// --poke writes a byte to every machine's RAM before frame F (experiments only).
// --trace K prints the mode bytes and SIO registers of every machine every K
// frames. At the end it writes PREFIX.state (resume with --load) and each
// machine's EEPROM to PREFIX-seatN.sav; the input saves are only read.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fs_boot.h"
#include "fs_save.h"
#include "fs_wide.h"
#include "gba_bios.h"
#include "multiplayer_session.h"
#include "multiplayer_view.h"
#include "sha1.h"

namespace {

constexpr std::size_t kEepromSize = 8192;
constexpr const char* kProgram = "four-swords-usa";

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    const auto n = f.tellg();
    if (!f || n <= 0) throw std::runtime_error("cannot read " + path);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(n));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes.data()), n);
    return bytes;
}

void write_file(const std::string& path, const std::uint8_t* data, std::size_t size) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!f) throw std::runtime_error("cannot write " + path);
}

// 24-bit bottom-up BMP from the PPU's RGB888 framebuffer.
void write_bmp(const std::string& path, const std::uint8_t* rgb, int w = 240) {
    const int h = 160, row = (w * 3 + 3) & ~3;
    std::vector<std::uint8_t> out(54 + row * h, 0);
    auto put32 = [&](std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    };
    out[0] = 'B'; out[1] = 'M';
    put32(2, static_cast<std::uint32_t>(out.size()));
    put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
    out[26] = 1; out[28] = 24;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::uint8_t* p = rgb + (y * w + x) * 3;
            std::uint8_t* q = out.data() + 54 + (h - 1 - y) * row + x * 3;
            q[0] = p[2]; q[1] = p[1]; q[2] = p[0];
        }
    }
    write_file(path, out.data(), out.size());
}

struct Args {
    std::string rom, bios, out, inputs, load;
    std::vector<std::string> saves;
    unsigned frames = 0, trace = 0, wide = 0;
    bool dump = false, room = false, no_raster = false;
    unsigned oam_from = 0;   // 0 = no OAM log
    unsigned map_from = 0;   // 0 = no map log
    std::vector<std::array<std::uint32_t, 3>> pokes;   // frame, address, byte
    int slot = -1;
    std::set<unsigned> shots;
};

Args parse(int argc, char** argv) {
    Args a;
    a.saves.resize(4);
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + k);
        const std::string v = argv[++i];
        if (k == "--rom") a.rom = v;
        else if (k == "--bios") a.bios = v;
        else if (k == "--out") a.out = v;
        else if (k == "--inputs") a.inputs = v;
        else if (k == "--load") a.load = v;
        else if (k == "--frames") a.frames = std::stoul(v);
        else if (k == "--trace") a.trace = std::stoul(v);
        else if (k == "--wide") a.wide = std::stoul(v);
        else if (k == "--dump") a.dump = v != "0";
        else if (k == "--room") a.room = v != "0";
        else if (k == "--no-raster") a.no_raster = v != "0";
        else if (k == "--oam-log") a.oam_from = std::stoul(v);
        else if (k == "--map-log") a.map_from = std::stoul(v);
        else if (k == "--poke") {
            // FRAME:ADDR:BYTE[,FRAME:ADDR:BYTE...], numbers in any base.
            std::istringstream list(v);
            std::string item;
            while (std::getline(list, item, ',')) {
                std::array<std::uint32_t, 3> poke{};
                std::istringstream fields(item);
                std::string field;
                for (auto& value : poke) {
                    if (!std::getline(fields, field, ':')) throw std::runtime_error("bad --poke " + item);
                    value = static_cast<std::uint32_t>(std::stoul(field, nullptr, 0));
                }
                a.pokes.push_back(poke);
            }
        }
        else if (k == "--slot") a.slot = std::stoi(v) - 1;
        else if (k.rfind("--save", 0) == 0 && k.size() == 7 && k[6] >= '0' && k[6] <= '3')
            a.saves[k[6] - '0'] = v;
        else if (k == "--shots") {
            std::istringstream s(v);
            std::string f;
            while (std::getline(s, f, ',')) a.shots.insert(std::stoul(f));
        } else throw std::runtime_error("unknown option " + k);
    }
    while (!a.saves.empty() && a.saves.back().empty()) a.saves.pop_back();
    if (a.rom.empty() || a.bios.empty() || a.out.empty() || !a.frames || a.saves.size() < 2)
        throw std::runtime_error("usage: see the header of tools/link_probe.cpp");
    return a;
}

// inputs[frame][machine], each line held until the next.
std::vector<std::array<std::uint16_t, 4>> read_inputs(const Args& a, unsigned players) {
    std::vector<std::array<std::uint16_t, 4>> inputs(a.frames);
    if (a.inputs.empty()) return inputs;
    std::ifstream script(a.inputs);
    if (!script) throw std::runtime_error("cannot open " + a.inputs);
    std::string line;
    while (std::getline(script, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        unsigned frame;
        std::array<std::uint16_t, 4> keys{};
        if (!(row >> std::setbase(0) >> frame)) throw std::runtime_error("bad input line: " + line);
        for (unsigned id = 0; id < players; ++id) {
            unsigned key;
            if (!(row >> std::setbase(0) >> key) || key > 1023)
                throw std::runtime_error("bad buttons: " + line);
            keys[id] = static_cast<std::uint16_t>(key);
        }
        for (unsigned f = frame; f < a.frames; ++f) inputs[f] = keys;
    }
    return inputs;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        using namespace gbarecomp;
        const Args a = parse(argc, argv);
        const unsigned players = static_cast<unsigned>(a.saves.size());
        const auto rom = read_file(a.rom);
        const auto rom_sha1 = gba::sha1(rom.data(), rom.size()).hex();
        gba::GbaBios bios;
        std::string error;
        if (!bios.load_from_file(a.bios.c_str(), gba::GbaBios::kExpectedSha1, &error))
            throw std::runtime_error(error);

        fsr::set_boot_slot(a.slot);

        GbaSessionConfig config;
        config.links = {{GbaLinkMedium::Cable, {}}};
        for (unsigned id = 0; id < players; ++id) {
            config.machines.push_back({id, kProgram, rom_sha1});
            config.links[0].machines.push_back(id);  // machine ID == cable port
            config.input_machines.push_back(id);
        }
        GbaMultiplayerSession session(config);
        session.set_native_slices(true);
        for (unsigned id = 0; id < players; ++id) {
            auto& m = session.machine(id);
            m.bus.set_bios(&bios);
            m.bus.set_rom(rom.data(), rom.size());
            m.bus.save().configure_eeprom(kEepromSize);
            fsr::setup_netplay_instance(m);
            if (a.saves[id] == "guest") {
                // The one-file first-boot save local multiplayer gives a guest.
                auto guest = fsr::save::Image::blank(rom, &error);
                if (!guest || !guest->create(0, "Guest" + std::to_string(id + 1), &error) ||
                    !guest->set_current_file(0, &error))
                    throw std::runtime_error("guest save: " + error);
                const auto bytes = guest->sav_bytes();
                if (!m.bus.save().load_eeprom_bytes(bytes.data(), bytes.size()))
                    throw std::runtime_error("guest save does not fit");
                m.bus.save().clear_dirty();
                continue;
            }
            std::ifstream exists(a.saves[id], std::ios::binary);
            if (exists) {
                const auto bytes = read_file(a.saves[id]);
                if (!m.bus.save().load_eeprom_bytes(bytes.data(), bytes.size()))
                    throw std::runtime_error("save does not fit: " + a.saves[id]);
                m.bus.save().clear_dirty();
            }
        }
        // --wide W: a host-only presentation mirror per machine renders W
        // columns (W > 240) with no game provider, to see what the hardware
        // tilemaps and OAM hold beyond the native view (DESIGN section 11).
        std::vector<std::unique_ptr<GbaNetplayPresentation>> wide;
        if (a.wide > 240) {
            // --room 1: the game's own policy (src/fs_wide.cpp) fills the
            // margins from the stage's tile map; without it there is no provider.
            GbaNetplayViewPolicy policy;
            policy.supported = true;
            if (a.room) policy = fsr::wide_view_policy();
            for (unsigned id = 0; id < players; ++id) {
                wide.push_back(std::make_unique<GbaNetplayPresentation>(policy));
                wide.back()->request_width(a.wide);
                session.machine(id).ppu.set_presentation_observer(wide.back().get());
                // --no-raster 1: only the mirror draws, as in the game's 16:9 view.
                if (a.no_raster) session.machine(id).ppu.set_rasterization_enabled(false);
            }
        }
        if (!a.load.empty() && !session.load_state(read_file(a.load), &error))
            throw std::runtime_error("cannot load " + a.load + ": " + error);

        const auto inputs = read_inputs(a, players);
        for (unsigned frame = 0; frame < a.frames; ++frame) {
            // Experiments only: the same byte written to every machine's RAM
            // before the frame runs, so the linked consoles stay in step.
            for (const auto& poke : a.pokes) {
                if (poke[0] != frame + 1) continue;
                for (unsigned id = 0; id < players; ++id) {
                    auto& bus = session.machine(id).bus;
                    if (poke[1] >= 0x03000000 && poke[1] < 0x03008000)
                        bus.iwram_ptr()[poke[1] - 0x03000000] = static_cast<std::uint8_t>(poke[2]);
                    else if (poke[1] >= 0x02000000 && poke[1] < 0x02040000)
                        bus.ewram_ptr()[poke[1] - 0x02000000] = static_cast<std::uint8_t>(poke[2]);
                    else throw std::runtime_error("--poke address is not in EWRAM or IWRAM");
                }
            }
            session.run_frame(std::span(inputs[frame].data(), players));
            session.discard_audio_output();
            // The quit hook (src/fs_boot.cpp) ends a launcher session here; the
            // probe keeps running and only reports it.
            static bool quit_seen = false;
            if (!quit_seen && fsr::quit_to_menu()) {
                quit_seen = true;
                std::printf("quit to the launcher requested in frame %u\n", frame + 1);
                std::fflush(stdout);
            }
            if (a.map_from && frame + 1 >= a.map_from) {
                // One line per machine and frame: mode bytes, camera, stage
                // size, screen-vs-buffer mismatches (BG1, BG2), DISPCNT, the
                // BG1/BG2 scroll registers, the blend registers and 16 bytes
                // around the camera.
                for (unsigned id = 0; id < players; ++id) {
                    static std::array<std::ofstream, 4> logs;
                    if (!logs[id].is_open()) logs[id].open(a.out + "-map-seat" + std::to_string(id) + ".txt");
                    auto& bus = session.machine(id).bus;
                    const std::uint8_t* iw = bus.iwram_ptr();
                    const std::uint8_t* io = bus.io().raw();
                    const auto w = [](const std::uint8_t* p) { return p[0] | (p[1] << 8); };
                    logs[id] << frame + 1 << ' ' << int(iw[0x6D10]) << ' ' << int(iw[0x5050]) << ' '
                             << int(iw[0x0BFC]) << ' ' << w(iw + 0x470) << ' ' << w(iw + 0x472) << ' '
                             << w(iw + 0x478) << ' ' << w(iw + 0x47A) << ' '
                             << fsr::wide_screen_mismatches(bus, 1) << ' '
                             << fsr::wide_screen_mismatches(bus, 2) << std::hex << ' ' << w(io) << ' '
                             << w(io + 0x14) << ' ' << w(io + 0x16) << ' ' << w(io + 0x18) << ' '
                             << w(io + 0x1A) << ' ' << w(io + 0x50) << ' ' << w(io + 0x52) << ' '
                             << w(io + 0x54);
                    for (unsigned i = 0x460; i < 0x470; i += 2) logs[id] << ' ' << w(iw + i);
                    // Last two fields: stage (0x03006C87) and floor (0x03006C84).
                    logs[id] << std::dec << ' ' << int(iw[0x6C87]) << ' ' << int(iw[0x6C84]) << '\n';
                }
            }
            if (a.oam_from && frame + 1 >= a.oam_from) {
                // One line per machine and frame: camera, then every enabled
                // sprite as attr0:attr1:attr2 (hex).
                for (unsigned id = 0; id < players; ++id) {
                    static std::array<std::ofstream, 4> logs;
                    if (!logs[id].is_open()) logs[id].open(a.out + "-oam-seat" + std::to_string(id) + ".txt");
                    auto& bus = session.machine(id).bus;
                    const std::uint8_t* iw = bus.iwram_ptr();
                    const std::uint8_t* oam = bus.oam_ptr();
                    logs[id] << frame + 1 << ' ' << int(iw[0x6D10]) << ' ' << (iw[0x470] | (iw[0x471] << 8)) << ' '
                             << (iw[0x472] | (iw[0x473] << 8)) << std::hex;
                    for (unsigned i = 0; i < 128; ++i) {
                        const unsigned a0 = oam[i * 8] | (oam[i * 8 + 1] << 8);
                        if ((a0 & 0x300) == 0x200) continue;
                        logs[id] << ' ' << a0 << ':' << (oam[i * 8 + 2] | (oam[i * 8 + 3] << 8)) << ':'
                                 << (oam[i * 8 + 4] | (oam[i * 8 + 5] << 8));
                    }
                    logs[id] << std::dec << '\n';
                }
            }
            const unsigned shown = frame + 1;
            if (a.trace && shown % a.trace == 0) {
                std::printf("frame=%u", shown);
                for (unsigned id = 0; id < players; ++id) {
                    auto& m = session.machine(id);
                    const std::uint8_t* iw = m.bus.iwram_ptr();
                    auto io16 = [&m](std::uint32_t addr) {
                        return m.bus.read8(addr) | (m.bus.read8(addr + 1) << 8);
                    };
                    std::printf(" | m%u mode=%u sub=%u aux=%u siocnt=%04x rcnt=%04x ie=%04x if=%04x"
                                " multi=%04x,%04x send=%04x tm=%04x/%04x", id,
                                iw[0x6D10], iw[0x5050], iw[0x0BFC], io16(0x04000128),
                                io16(0x04000134), io16(0x04000200), io16(0x04000202),
                                io16(0x04000120), io16(0x04000122), io16(0x0400012A),
                                io16(0x0400010C), io16(0x0400010E));
                }
                std::printf("\n");
                std::fflush(stdout);
            }
            if (a.shots.count(shown)) {
                for (unsigned id = 0; id < players; ++id) {
                    write_bmp(a.out + "-f" + std::to_string(shown) + "-seat" + std::to_string(id) + ".bmp",
                              session.machine(id).ppu.latched_framebuffer());
                    if (a.dump) {
                        // EWRAM, IWRAM, VRAM, palette, OAM and the IO page, in that order.
                        auto& bus = session.machine(id).bus;
                        std::ofstream f(a.out + "-f" + std::to_string(shown) + "-seat" + std::to_string(id) + ".mem",
                                        std::ios::binary);
                        const auto put = [&f](const std::uint8_t* p, std::size_t n) {
                            f.write(reinterpret_cast<const char*>(p), static_cast<std::streamsize>(n));
                        };
                        put(bus.ewram_ptr(), 0x40000); put(bus.iwram_ptr(), 0x8000);
                        put(bus.vram_ptr(), 0x18000); put(bus.pal_ptr(), 0x400);
                        put(bus.oam_ptr(), 0x400); put(bus.io().raw(), 0x400);
                    }
                    if (id < wide.size() && wide[id]->pixels()) {
                        write_bmp(a.out + "-f" + std::to_string(shown) + "-seat" + std::to_string(id) + "-wide.bmp",
                                  wide[id]->pixels(), static_cast<int>(wide[id]->width()));
                        auto& m = session.machine(id);
                        std::printf("regs frame=%u m%u dispcnt=%04x", shown, id,
                                    m.bus.read8(0x04000000) | (m.bus.read8(0x04000001) << 8));
                        for (unsigned bg = 0; bg < 4; ++bg) {
                            std::printf(" bg%ucnt=%04x", bg, m.bus.read8(0x04000008 + bg * 2) |
                                                             (m.bus.read8(0x04000009 + bg * 2) << 8));
                        }
                        std::printf("\n");
                    }
                }
            }
        }

        const auto state = session.save_state();
        write_file(a.out + ".state", state.data(), state.size());
        for (unsigned id = 0; id < players; ++id) {
            const auto bytes = session.machine(id).bus.save().eeprom_bytes();
            write_file(a.out + "-seat" + std::to_string(id) + ".sav", bytes.data(), bytes.size());
        }
        std::printf("done players=%u frames=%u state_bytes=%zu hash=%08x\n", players, a.frames,
                    state.size(), session.state_hash());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "link probe: %s\n", e.what());
        return 1;
    }
}
