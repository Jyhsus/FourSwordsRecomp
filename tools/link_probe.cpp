// link_probe.cpp — M3: two (to four) virtual GBAs on one GbaLinkHub cable,
// in one process, with no network (docs/DESIGN.md section 7, M3).
//
//   FourSwordsLinkProbe --rom R --bios B --out PREFIX --frames N
//       --save0 A.sav --save1 B.sav [--save2 .. --save3 ..]
//       [--slot 1-3] [--inputs FILE] [--shots F,F,..] [--trace K]
//       [--load PREFIX.state]
//
// Every machine runs the same generated code and the same fs_boot hooks as
// the game executable; --slot boots every machine into that file of its own
// save. --inputs lines are "frame buttons0 buttons1 ..." (pressed = 1, GBA
// bit order: A=1 B=2 Select=4 Start=8 Right=16 Left=32 Up=64 Down=128 R=256
// L=512), held until the next line. --shots writes PREFIX-fF-seatN.bmp.
// --trace K prints the mode bytes and SIO registers of every machine every K
// frames. At the end it writes PREFIX.state (resume with --load) and each
// machine's EEPROM to PREFIX-seatN.sav; the input saves are only read.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fs_boot.h"
#include "gba_bios.h"
#include "multiplayer_session.h"
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
void write_bmp(const std::string& path, const std::uint8_t* rgb) {
    constexpr int w = 240, h = 160, row = w * 3;
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
    unsigned frames = 0, trace = 0;
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
            std::ifstream exists(a.saves[id], std::ios::binary);
            if (exists) {
                const auto bytes = read_file(a.saves[id]);
                if (!m.bus.save().load_eeprom_bytes(bytes.data(), bytes.size()))
                    throw std::runtime_error("save does not fit: " + a.saves[id]);
                m.bus.save().clear_dirty();
            }
        }
        if (!a.load.empty() && !session.load_state(read_file(a.load), &error))
            throw std::runtime_error("cannot load " + a.load + ": " + error);

        const auto inputs = read_inputs(a, players);
        for (unsigned frame = 0; frame < a.frames; ++frame) {
            session.run_frame(std::span(inputs[frame].data(), players));
            session.discard_audio_output();
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
