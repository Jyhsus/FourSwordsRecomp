#pragma once
// fs_save.h — host-side view of the AZLE save (64 Kbit EEPROM, 8 KiB .sav).
//
// The cartridge's 3 file slots are the launcher's 3 saves (docs/DESIGN.md
// section 8.1). This reads and edits them the way the game does: same
// checksum, same signatures, same two mirrors. Nothing here runs guest code.
//
// Layout (offsets in game byte order; the .sav stores every 8-byte EEPROM
// block reversed): a table of 8 descriptors at 0x0814C654 gives, per entry,
// the size, two 8-byte status headers (A, B) and two data copies (A, B).
// Each is written at its offset and again at offset + 0x1000. Copy B is only a
// fallback for a failed EEPROM write. Entries 4-6 are the Four Swords file
// headers (0x40 bytes, name at +8); entries 0-2 are the A Link to the Past
// data of the same files, which is shared with this one.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsr::save {

constexpr int kSlotCount = 3;
constexpr std::size_t kNameMax = 6;       // characters, as on "ENTER A NAME"
constexpr std::size_t kImageSize = 0x2000;

struct Slot {
    bool used = false;
    std::string name;  // UTF-8
};

// True when every character is on the game's name-entry grid and the name
// has 1 to kNameMax characters.
bool valid_name(std::string_view utf8);

// Every character on the name-entry grid, one UTF-8 string each: upper case,
// lower case, digits, punctuation, then the accented letters.
const std::vector<std::string>& name_characters();

class Image {
public:
    // Reads a .sav. Fails on a missing file or a size other than kImageSize.
    static std::optional<Image> load(const std::filesystem::path& path,
                                     std::string* error = nullptr);
    // The state the game writes on its first boot with an erased EEPROM. The
    // fixed strings come from the player's ROM, never from this repository.
    static std::optional<Image> blank(const std::vector<std::uint8_t>& rom,
                                      std::string* error = nullptr);

    bool store(const std::filesystem::path& path, std::string* error = nullptr) const;

    std::array<Slot, kSlotCount> slots() const;

    // Same writes as the game's "ENTER A NAME" / "ERASE": a new file resets
    // the slot's A Link to the Past part to INIT; erasing marks both parts
    // deleted ("DelF") and leaves the old data in place.
    bool create(int slot, std::string_view utf8_name, std::string* error = nullptr);
    bool rename(int slot, std::string_view utf8_name, std::string* error = nullptr);
    bool erase(int slot, std::string* error = nullptr);

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }  // game order

private:
    explicit Image(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

    std::vector<std::uint8_t> bytes_;
};

}  // namespace fsr::save
