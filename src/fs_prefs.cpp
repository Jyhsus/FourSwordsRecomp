#include "fs_prefs.h"

#include <fstream>
#include <system_error>

namespace fsr::prefs {
namespace {

std::string trim(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

// A decimal number in [low, high], or false.
bool number(const std::string& text, int low, int high, int& out) {
    if (text.empty() || text.size() > 3) return false;
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    if (value < low || value > high) return false;
    out = value;
    return true;
}

// "none" gives -1 (kNoPad, and "nobody" for the keyboard); N in 1..high gives N - 1.
bool position(const std::string& text, int high, int& out) {
    if (text == "none") {
        out = -1;
        return true;
    }
    int value = 0;
    if (!number(text, 1, high, value)) return false;
    out = value - 1;
    return true;
}

std::string position_text(int value) {
    return value < 0 ? std::string("none") : std::to_string(value + 1);
}

}  // namespace

void load(const std::filesystem::path& file, LocalPlan& plan) {
    std::ifstream in(file);
    if (!in) return;
    const auto pads_before = plan.pad;
    const int keyboard_before = plan.keyboard;
    std::string line;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find_first_of("#;"));
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if (key == "players") {
            number(value, 2, kLocalMaxPlayers, plan.players);
        } else if (key == "keyboard") {
            position(value, kLocalMaxPlayers, plan.keyboard);
        } else if (key == "scale") {
            if (value == "fit" || value == "integer") plan.integer_scale = value == "integer";
        } else if (key == "view") {
            if (value == "3:2" || value == "16:9") plan.wide = value == "16:9";
        } else if (key.size() == 4 && key.compare(0, 3, "pad") == 0 && key[3] >= '1' &&
                   key[3] < '1' + kLocalMaxPlayers) {
            position(value, kMaxPadPosition, plan.pad[key[3] - '1']);
        }
    }
    // Checked once everything is read: the keys may come in any order.
    if (plan.keyboard >= plan.players) plan.keyboard = keyboard_before;
    for (int a = 0; a < kLocalMaxPlayers; ++a) {
        for (int b = a + 1; b < kLocalMaxPlayers; ++b) {
            if (plan.pad[a] != kNoPad && plan.pad[a] == plan.pad[b]) {
                plan.pad = pads_before;
                return;
            }
        }
    }
}

bool save(const std::filesystem::path& file, const LocalPlan& plan, std::string* error) {
    std::filesystem::path temporary = file;
    temporary += ".tmp";
    std::error_code ec;
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << "# FourSwordsRecomp: choices of the Multiplayer > Local page.\n"
               "# Delete this file to go back to the defaults.\n"
               "version = 1\n"
            << "players = " << plan.players << '\n';
        for (int p = 0; p < kLocalMaxPlayers; ++p) {
            out << "pad" << p + 1 << " = " << position_text(plan.pad[p]) << '\n';
        }
        out << "keyboard = " << position_text(plan.keyboard) << '\n'
            << "scale = " << (plan.integer_scale ? "integer" : "fit") << '\n'
            << "view = " << (plan.wide ? "16:9" : "3:2") << '\n';
        out.close();
        if (!out) {
            if (error) *error = "cannot write " + temporary.string();
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }
    std::filesystem::rename(temporary, file, ec);
    if (ec) {
        if (error) *error = "cannot replace " + file.string() + ": " + ec.message();
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

void apply_flags(const LocalFlags& flags, LocalPlan& plan) {
    if (!flags.scale.empty()) plan.integer_scale = flags.scale == "integer";
    if (!flags.view.empty()) plan.wide = flags.view == "16:9";
    if (!flags.keyboard.empty()) position(flags.keyboard, kLocalMaxPlayers, plan.keyboard);
}

}  // namespace fsr::prefs
