// fs_save_test.cpp — checks src/fs_save.cpp against saves the game wrote.
//
// The expected checksums come from files created on the "ENTER A NAME"
// screen of the recompiled game (name "ZZ" -> 0xEAD7, "AAA" -> 0xD22F).
// Optional: FSR_TEST_ROM=<rom> FSR_TEST_FRESH_SAV=<sav> also compares
// Image::blank() with a save the game wrote on its first boot.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fs_save.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::filesystem::path erased_sav() {
    auto path = std::filesystem::temp_directory_path() / "fsr_save_test.sav";
    std::ofstream(path, std::ios::binary)
        .write(std::string(fsr::save::kImageSize, '\xFF').data(), fsr::save::kImageSize);
    return path;
}

bool header_is(const std::vector<std::uint8_t>& b, std::size_t at, std::uint16_t sum, const char* sig) {
    const std::uint16_t neg = static_cast<std::uint16_t>(-sum);
    return b[at] == (sum & 0xFF) && b[at + 1] == (sum >> 8) && b[at + 2] == (neg & 0xFF) &&
           b[at + 3] == (neg >> 8) && std::string(b.begin() + at + 4, b.begin() + at + 8) == sig;
}

bool marker_is(const std::vector<std::uint8_t>& b, std::size_t at, const char* sig) {
    return b[at] == 0xFF && b[at + 1] == 0xFF && b[at + 2] == 0xFF && b[at + 3] == 0xFF &&
           std::string(b.begin() + at + 4, b.begin() + at + 8) == sig;
}

}  // namespace

int main() {
    using namespace fsr::save;

    check(valid_name("ZZ"), "ZZ is valid");
    check(valid_name("Zelda"), "Zelda is valid");
    check(valid_name("Link!?"), "Link!? is valid");
    check(valid_name("\xC3\xA9l\xC3\xA8ve"), "\"eleve\" with accents is valid");
    check(valid_name("\xC5\x92uvre"), "OE ligature is valid");
    check(!valid_name(""), "empty name is invalid");
    check(!valid_name("Linkkkk"), "7 characters are invalid");
    check(!valid_name("Stra\xC3\x9F" "e"), "sharp s is not on the grid");
    check(!valid_name("\xC3\x81"), "A acute is not on the grid");
    check(!valid_name("a_b"), "underscore is not on the grid");
    check(!valid_name("\xC3"), "truncated UTF-8 is invalid");

    const auto& grid = name_characters();
    check(grid.size() == 112, "the name grid has 112 characters");
    bool grid_valid = true;
    for (const auto& c : grid) grid_valid = grid_valid && valid_name(c);
    check(grid_valid, "every grid character is a valid name");

    const auto path = erased_sav();
    auto image = Image::load(path);
    check(image.has_value(), "load an erased 8 KiB save");
    if (!image) return 1;

    check(image->create(0, "ZZ"), "create slot 1");
    const auto& b = image->bytes();
    for (std::size_t base : {std::size_t{0}, std::size_t{0x1000}}) {
        check(header_is(b, base + 0x70, 0xEAD7, "S4FT"), "ZZ header matches the game");
        check(b[base + 0x584] == 0x11 && b[base + 0x588] == 'Z' && b[base + 0x589] == 'Z' &&
                  b[base + 0x58A] == 0 && b[base + 0x5A4] == 1 && b[base + 0x5A5] == 1 &&
                  b[base + 0x5BF] == 3,
              "ZZ data matches the game");
        check(marker_is(b, base + 0x40, "INIT"),
              "new file resets the A Link to the Past part to INIT");
    }

    check(image->rename(0, "AAA"), "rename slot 1");
    check(header_is(image->bytes(), 0x70, 0xD22F, "S4FT"), "AAA header matches the game");
    check(!image->rename(1, "AAA"), "renaming an empty slot fails");
    check(!image->create(3, "AAA"), "slot 4 does not exist");

    check(image->store(path), "store");
    auto again = Image::load(path);
    check(again && again->bytes() == image->bytes(), "store/load round trip");
    if (again) {
        const auto slots = again->slots();
        check(slots[0].used && slots[0].name == "AAA", "slot 1 is AAA");
        check(!slots[1].used && !slots[2].used, "slots 2 and 3 are empty");
        check(again->create(2, "\xC5\x93uf"), "create slot 3 with a ligature");
        check(again->slots()[2].name == "\xC5\x93uf", "ligature round trip");
        check(again->erase(0), "erase slot 1");
        check(!again->slots()[0].used, "slot 1 is empty after erase");
        check(marker_is(again->bytes(), 0x1070, "DelF") && marker_is(again->bytes(), 0x40, "DelF"),
              "erase marks both parts DelF in both mirrors");
    }
    std::filesystem::remove(path);

    const char* rom_path = std::getenv("FSR_TEST_ROM");
    const char* fresh_path = std::getenv("FSR_TEST_FRESH_SAV");
    if (rom_path && fresh_path) {
        auto blank = Image::blank(read_file(rom_path));
        auto fresh = Image::load(fresh_path);
        check(blank && fresh && blank->bytes() == fresh->bytes(),
              "blank() matches the game's first-boot save");
    }

    if (g_failures == 0) std::printf("fs_save_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
