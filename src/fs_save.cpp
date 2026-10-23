#include "fs_save.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>

// Reverse-engineered from the save code at 0x08067940-0x080681D0 and the
// file-select helpers at 0x080111F8-0x08011476; see docs/DESIGN.md section 8.1.

namespace fsr::save {
namespace {

constexpr std::uint16_t kNone = 0xFFFF;
constexpr std::size_t kMirror = 0x1000;
constexpr std::size_t kHeaderSize = 8;

struct Descriptor {
    std::uint16_t size, header_a, header_b, data_a, data_b;
};

// ROM table at 0x0814C654, 12 bytes per entry (last u16 unused).
constexpr Descriptor kDescriptors[8] = {
    {0x480, 0x40, kNone, 0x100, kNone},   // 0-2: A Link to the Past part of files 1-3
    {0x480, 0x50, kNone, 0x600, kNone},
    {0x480, 0x60, kNone, 0xB00, kNone},
    {0x020, kNone, kNone, 0x000, kNone},  // 3: cartridge signature string
    {0x040, 0x70, 0x80, 0x580, 0x5C0},    // 4-6: Four Swords file headers
    {0x040, 0x90, 0xA0, 0xA80, 0xAC0},
    {0x040, 0xB0, 0xC0, 0xF80, 0xFC0},
    {0x010, 0x20, 0x30, 0x0E0, 0x0F0},    // 7: system block, loaded to 0x03000000
};
constexpr int kFirstFileDescriptor = 4;
constexpr int kSystemDescriptor = 7;

// Signatures as stored (little-endian u32 at header + 4).
constexpr std::uint32_t kSigValid = 0x54463453u;  // "S4FT"
constexpr std::uint8_t kSigInit[4] = {'I', 'N', 'I', 'T'};
constexpr std::uint8_t kSigDeleted[4] = {'D', 'e', 'l', 'F'};

// Four Swords file header (0x40 bytes) fields written by file select.
constexpr std::size_t kVersionOffset = 0x04;  // 0x11
constexpr std::size_t kNameOffset = 0x08;     // 6 chars, zero-padded to 8
constexpr std::size_t kNameField = 8;
constexpr std::size_t kNewFileFlagsOffset = 0x24;  // 1, 1 on a new file
constexpr std::size_t kStatusOffset = 0x3F;   // 3 when written by the game

// System block byte 4: the file AgbMain and mode 6 load.
constexpr std::size_t kCurrentFileOffset = 4;

// Player-ROM offsets of the fixed data a first boot writes (blank()).
constexpr std::size_t kRomSignature = 0x14C630;   // 0x20 bytes, descriptor 3
constexpr std::size_t kRomDefaultSystem = 0x14C6BC;  // 0x10 bytes, descriptor 7

std::uint16_t read16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

// 0x080679B0: sum of each u16 XOR the number of bytes left, before it.
std::uint16_t checksum(const std::uint8_t* data, std::size_t size) {
    std::uint32_t sum = 0;
    for (std::size_t left = size; left > 0; left -= 2, data += 2) {
        sum += read16(data) ^ static_cast<std::uint32_t>(left);
    }
    return static_cast<std::uint16_t>(sum);
}

void set_error(std::string* error, const std::string& text) {
    if (error) *error = text;
}

bool slot_in_range(int slot, std::string* error) {
    if (slot >= 0 && slot < kSlotCount) return true;
    set_error(error, "slot must be 1, 2 or 3");
    return false;
}

// cp1252 byte for a code point on the name-entry grid, or 0.
std::uint8_t grid_byte(char32_t c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        return static_cast<std::uint8_t>(c);
    }
    switch (c) {
    case ' ': case '!': case '\'': case ',': case '-': case '.': case ':': case '?':
        return static_cast<std::uint8_t>(c);
    case U'Œ': return 0x8C;  // Œ
    case U'œ': return 0x9C;  // œ
    }
    // Accented letters, identical in Latin-1 and cp1252. Upper case only has
    // the ones on the grid; lower case mirrors them at +0x20.
    static constexpr char32_t kUpper[] = {
        U'À', U'Â', U'Ä', U'Æ', U'Ç', U'È', U'É', U'Ê', U'Ë', U'Ì', U'Î', U'Ï',
        U'Ð', U'Ñ', U'Ò', U'Ô', U'Ö', U'Ù', U'Û', U'Ü'};
    for (char32_t u : kUpper) {
        if (c == u || c == u + 0x20) return static_cast<std::uint8_t>(c);
    }
    return 0;
}

char32_t grid_char(std::uint8_t b) {
    if (b == 0x8C) return U'Œ';
    if (b == 0x9C) return U'œ';
    return b;  // Latin-1 for everything else on the grid
}

// Minimal UTF-8 decoder; returns false on malformed input.
bool decode_utf8(std::string_view s, std::u32string& out) {
    out.clear();
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : -1;
        if (extra < 0 || s.size() - i <= static_cast<std::size_t>(extra)) return false;
        char32_t cp = extra == 0 ? c : c & (0x3F >> extra);
        for (int k = 1; k <= extra; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return true;
}

void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

bool encode_name(std::string_view utf8, std::uint8_t (&out)[kNameField], std::string* error) {
    std::u32string chars;
    if (!decode_utf8(utf8, chars) || chars.empty() || chars.size() > kNameMax) {
        set_error(error, "a name has 1 to 6 characters");
        return false;
    }
    std::memset(out, 0, sizeof(out));
    for (std::size_t i = 0; i < chars.size(); ++i) {
        out[i] = grid_byte(chars[i]);
        if (out[i] == 0) {
            set_error(error, "the name uses a character the game cannot show");
            return false;
        }
    }
    return true;
}

// The game's check (0x08067E18): stored checksum, its negation, signature.
bool copy_valid(const std::vector<std::uint8_t>& image, std::size_t header,
                std::size_t data, std::size_t size) {
    const std::uint8_t* h = image.data() + header;
    std::uint32_t sig;
    std::memcpy(&sig, h + 4, 4);
    if (sig != kSigValid) return false;
    const std::uint16_t want =
        static_cast<std::uint16_t>(checksum(h + 4, 4) + checksum(image.data() + data, size));
    return read16(h) == want && static_cast<std::uint16_t>(read16(h + 2) + want) == 0;
}

// Same order the game reads in (0x08067CBC): A, B, then the mirror's A, B.
const std::uint8_t* find_valid(const std::vector<std::uint8_t>& image, int index) {
    const Descriptor& d = kDescriptors[index];
    for (std::size_t base : {std::size_t{0}, kMirror}) {
        if (d.header_a != kNone && copy_valid(image, base + d.header_a, base + d.data_a, d.size)) {
            return image.data() + base + d.data_a;
        }
        if (d.header_b != kNone && copy_valid(image, base + d.header_b, base + d.data_b, d.size)) {
            return image.data() + base + d.data_b;
        }
    }
    return nullptr;
}

// Writes copy A at both mirrors with a valid header, like 0x08067BBC when the
// EEPROM write succeeds.
void write_valid(std::vector<std::uint8_t>& image, int index, const std::uint8_t* data) {
    const Descriptor& d = kDescriptors[index];
    std::uint8_t header[kHeaderSize];
    std::memcpy(header + 4, &kSigValid, 4);
    const std::uint16_t sum =
        static_cast<std::uint16_t>(checksum(header + 4, 4) + checksum(data, d.size));
    const std::uint16_t neg = static_cast<std::uint16_t>(-sum);
    header[0] = sum & 0xFF; header[1] = sum >> 8;
    header[2] = neg & 0xFF; header[3] = neg >> 8;
    for (std::size_t base : {std::size_t{0}, kMirror}) {
        std::memmove(image.data() + base + d.data_a, data, d.size);
        std::memcpy(image.data() + base + d.header_a, header, kHeaderSize);
    }
}

// INIT / DelF headers carry 0xFFFF in both checksum fields (0x0814C618).
void write_marker(std::vector<std::uint8_t>& image, int index, const std::uint8_t (&sig)[4]) {
    const Descriptor& d = kDescriptors[index];
    for (std::size_t base : {std::size_t{0}, kMirror}) {
        std::uint8_t* h = image.data() + base + d.header_a;
        std::memset(h, 0xFF, 4);
        std::memcpy(h + 4, sig, 4);
    }
}

}  // namespace

bool valid_name(std::string_view utf8) {
    std::uint8_t encoded[kNameField];
    return encode_name(utf8, encoded, nullptr);
}

const std::vector<std::string>& name_characters() {
    static const std::vector<std::string> chars = [] {
        std::vector<std::string> out;
        auto add = [&out](char32_t c) {
            std::string s;
            append_utf8(s, c);
            out.push_back(std::move(s));
        };
        for (char32_t c = 'A'; c <= 'Z'; ++c) add(c);
        for (char32_t c = 'a'; c <= 'z'; ++c) add(c);
        for (char32_t c = '0'; c <= '9'; ++c) add(c);
        for (char32_t c : std::u32string_view(U" '-:!?,.")) add(c);
        for (char32_t lower : {char32_t{0}, char32_t{0x20}}) {
            for (char32_t c = 0xC0; c <= 0xDC; ++c) {
                if (grid_byte(c + lower)) add(c + lower);
            }
            add(lower ? U'œ' : U'Œ');
        }
        return out;
    }();
    return chars;
}

std::optional<Image> Image::load(const std::filesystem::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        set_error(error, "cannot open " + path.string());
        return std::nullopt;
    }
    std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (raw.size() != kImageSize) {
        set_error(error, path.string() + " is not an 8 KiB EEPROM save");
        return std::nullopt;
    }
    return from_sav(raw, error);
}

std::optional<Image> Image::from_sav(const std::vector<std::uint8_t>& sav, std::string* error) {
    if (sav.size() != kImageSize) {
        set_error(error, "not an 8 KiB EEPROM save");
        return std::nullopt;
    }
    std::vector<std::uint8_t> raw = sav;
    for (std::size_t i = 0; i < raw.size(); i += 8) std::reverse(raw.begin() + i, raw.begin() + i + 8);
    return Image(std::move(raw));
}

std::optional<Image> Image::blank(const std::vector<std::uint8_t>& rom, std::string* error) {
    if (rom.size() < kRomDefaultSystem + kDescriptors[kSystemDescriptor].size) {
        set_error(error, "ROM too small");
        return std::nullopt;
    }
    Image image(std::vector<std::uint8_t>(kImageSize, 0xFF));
    const Descriptor& sig = kDescriptors[3];
    for (std::size_t base : {std::size_t{0}, kMirror}) {
        std::memcpy(image.bytes_.data() + base + sig.data_a, rom.data() + kRomSignature, sig.size);
    }
    write_valid(image.bytes_, kSystemDescriptor, rom.data() + kRomDefaultSystem);
    for (int i = 0; i < kSlotCount; ++i) {
        write_marker(image.bytes_, i, kSigInit);
        write_marker(image.bytes_, kFirstFileDescriptor + i, kSigInit);
    }
    return image;
}

std::vector<std::uint8_t> Image::sav_bytes() const {
    std::vector<std::uint8_t> raw = bytes_;
    for (std::size_t i = 0; i < raw.size(); i += 8) std::reverse(raw.begin() + i, raw.begin() + i + 8);
    return raw;
}

bool Image::store(const std::filesystem::path& path, std::string* error) const {
    const std::vector<std::uint8_t> raw = sav_bytes();
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
        if (!out) {
            set_error(error, "cannot write " + tmp.string());
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        set_error(error, "cannot replace " + path.string() + ": " + ec.message());
        return false;
    }
    return true;
}

std::array<Slot, kSlotCount> Image::slots() const {
    std::array<Slot, kSlotCount> out{};
    for (int i = 0; i < kSlotCount; ++i) {
        const std::uint8_t* data = find_valid(bytes_, kFirstFileDescriptor + i);
        // In use = loaded and named, as file select checks it (0x080111F8).
        if (!data || data[kNameOffset] == 0) continue;
        out[i].used = true;
        for (std::size_t k = 0; k < kNameMax && data[kNameOffset + k]; ++k) {
            append_utf8(out[i].name, grid_char(data[kNameOffset + k]));
        }
    }
    return out;
}

bool Image::create(int slot, std::string_view utf8_name, std::string* error) {
    std::uint8_t name[kNameField];
    if (!slot_in_range(slot, error) || !encode_name(utf8_name, name, error)) return false;
    std::uint8_t data[0x40] = {};
    data[kVersionOffset] = 0x11;
    std::memcpy(data + kNameOffset, name, kNameField);
    data[kNewFileFlagsOffset] = 1;
    data[kNewFileFlagsOffset + 1] = 1;
    data[kStatusOffset] = 3;
    write_marker(bytes_, slot, kSigInit);
    write_valid(bytes_, kFirstFileDescriptor + slot, data);
    return true;
}

bool Image::rename(int slot, std::string_view utf8_name, std::string* error) {
    std::uint8_t name[kNameField];
    if (!slot_in_range(slot, error) || !encode_name(utf8_name, name, error)) return false;
    const std::uint8_t* current = find_valid(bytes_, kFirstFileDescriptor + slot);
    if (!current || current[kNameOffset] == 0) {
        set_error(error, "that slot is empty");
        return false;
    }
    std::uint8_t data[0x40];
    std::memcpy(data, current, sizeof(data));
    std::memcpy(data + kNameOffset, name, kNameField);
    write_valid(bytes_, kFirstFileDescriptor + slot, data);
    return true;
}

bool Image::erase(int slot, std::string* error) {
    if (!slot_in_range(slot, error)) return false;
    write_marker(bytes_, kFirstFileDescriptor + slot, kSigDeleted);
    write_marker(bytes_, slot, kSigDeleted);
    return true;
}

bool Image::set_current_file(int slot, std::string* error) {
    if (!slot_in_range(slot, error)) return false;
    const std::uint8_t* current = find_valid(bytes_, kSystemDescriptor);
    if (!current) {
        set_error(error, "the save has no valid system block");
        return false;
    }
    std::uint8_t data[0x10];
    std::memcpy(data, current, sizeof(data));
    data[kCurrentFileOffset] = static_cast<std::uint8_t>(slot);
    write_valid(bytes_, kSystemDescriptor, data);
    return true;
}

void Image::copy_file(const Image& from, int slot) {
    if (slot < 0 || slot >= kSlotCount) return;
    for (int index : {slot, kFirstFileDescriptor + slot}) {
        const Descriptor& d = kDescriptors[index];
        for (std::size_t base : {std::size_t{0}, kMirror}) {
            const auto copy = [&](std::uint16_t offset, std::size_t size) {
                if (offset == kNone) return;
                std::memcpy(bytes_.data() + base + offset, from.bytes_.data() + base + offset, size);
            };
            copy(d.header_a, kHeaderSize);
            copy(d.header_b, kHeaderSize);
            copy(d.data_a, d.size);
            copy(d.data_b, d.size);
        }
    }
}

}  // namespace fsr::save
