// fs_prefs_test.cpp — the Local page's settings file (src/fs_prefs.h,
// docs/DESIGN.md section 10). No ROM, no SDL. Works in a folder of its own
// under the system's temporary directory.
//
//   ctest --test-dir build -R fs_prefs

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "fs_prefs.h"

namespace {

namespace fs = std::filesystem;

int failures = 0;

void check(bool ok, const char* what, int line) {
    if (ok) return;
    std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
    ++failures;
}
#define CHECK(x) check((x), #x, __LINE__)

using Pads = std::array<int, fsr::kLocalMaxPlayers>;

// The fields the file holds, all different from LocalPlan's defaults.
fsr::LocalPlan chosen() {
    fsr::LocalPlan plan;
    plan.players = 4;
    plan.pad = {2, fsr::kNoPad, 0, 5};
    plan.keyboard = 3;
    plan.integer_scale = true;
    plan.wide = true;
    return plan;
}

bool same_choices(const fsr::LocalPlan& a, const fsr::LocalPlan& b) {
    return a.players == b.players && a.pad == b.pad && a.keyboard == b.keyboard &&
           a.integer_scale == b.integer_scale && a.wide == b.wide;
}

void write(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

fsr::LocalPlan loaded(const fs::path& file, const std::string& text) {
    write(file, text);
    fsr::LocalPlan plan;
    fsr::prefs::load(file, plan);
    return plan;
}

void missing_file_changes_nothing(const fs::path& dir) {
    fsr::LocalPlan plan = chosen();
    fsr::prefs::load(dir / "absent.ini", plan);
    CHECK(same_choices(plan, chosen()));
    // A folder where the file should be is not readable either.
    fsr::prefs::load(dir, plan);
    CHECK(same_choices(plan, chosen()));
}

void round_trip_and_overwrite(const fs::path& dir) {
    const fs::path file = dir / "round.ini";
    std::string error;
    CHECK(fsr::prefs::save(file, chosen(), &error));
    fsr::LocalPlan plan;
    plan.file = {2, 1, 0, fsr::kGuestFile};
    plan.save_path = "kept.sav";
    fsr::prefs::load(file, plan);
    CHECK(same_choices(plan, chosen()));
    // Files and the save path are not the settings file's business.
    CHECK((plan.file == std::array<int, 4>{2, 1, 0, fsr::kGuestFile}));
    CHECK(plan.save_path == "kept.sav");

    // Saving again replaces the file (std::filesystem::rename over it).
    fsr::LocalPlan other;
    other.keyboard = -1;
    other.pad = {fsr::kNoPad, fsr::kNoPad, fsr::kNoPad, fsr::kNoPad};
    CHECK(fsr::prefs::save(file, other, &error));
    fsr::LocalPlan again = chosen();
    fsr::prefs::load(file, again);
    CHECK(same_choices(again, other));
    CHECK(!fs::exists(dir / "round.ini.tmp"));
}

void bad_values_keep_what_was_there(const fs::path& dir) {
    const fs::path file = dir / "bad.ini";
    const fsr::LocalPlan defaults;
    CHECK(same_choices(loaded(file, ""), defaults));
    CHECK(same_choices(loaded(file, std::string("\0\xff garbage\n===\n[x]\n", 19)), defaults));
    CHECK(same_choices(loaded(file,
                              "players = 5\nkeyboard = 0\nscale = huge\nview = 4:3\n"
                              "pad1 = 0\npad2 = 17\npad3 = -1\npad4 = 99999999999\n"
                              "pad0 = 2\npad5 = 2\nunknown = 1\n"),
                       defaults));
    CHECK(same_choices(loaded(file, "players = 1\nplayers = 2x\nkeyboard =\n"), defaults));

    // One bad key does not spoil the good ones.
    const fsr::LocalPlan mixed = loaded(file, "players = nine\nview = 16:9\nfuture = yes\n");
    CHECK(mixed.players == 2 && mixed.wide);
}

void hand_edits_are_read(const fs::path& dir) {
    const fs::path file = dir / "hand.ini";
    // CRLF, spacing, comments, a section header and keys in any order.
    const fsr::LocalPlan plan = loaded(file,
                                       "[local]\r\n  keyboard=3   # third player\r\n"
                                       "; a comment\r\nplayers  =  3\r\nscale = integer\r\n");
    CHECK(plan.players == 3 && plan.keyboard == 2 && plan.integer_scale && !plan.wide);
}

void keyboard_beyond_the_players_is_dropped(const fs::path& dir) {
    const fs::path file = dir / "keyboard.ini";
    const fsr::LocalPlan plan = loaded(file, "players = 2\nkeyboard = 4\n");
    CHECK(plan.players == 2 && plan.keyboard == 0);
    CHECK(loaded(file, "players = 2\nkeyboard = none\n").keyboard == -1);
}

void duplicate_controllers_are_dropped(const fs::path& dir) {
    const fs::path file = dir / "pads.ini";
    const Pads defaults = fsr::LocalPlan{}.pad;
    // Players 3 and 4 count too: raising the player count must not give a plan
    // with one controller twice.
    CHECK(loaded(file, "players = 2\npad3 = 4\npad4 = 4\n").pad == defaults);
    // A partial file that collides with a default is dropped as well.
    CHECK(loaded(file, "pad1 = 2\n").pad == defaults);
    // "none" may repeat.
    CHECK((loaded(file, "pad1 = none\npad2 = none\npad3 = 1\npad4 = 16\n").pad ==
           Pads{fsr::kNoPad, fsr::kNoPad, 0, 15}));
}

void unwritable_target_fails(const fs::path& dir) {
    std::string error;
    CHECK(!fsr::prefs::save(dir / "no_such_folder" / "x.ini", chosen(), &error));
    CHECK(!error.empty());
    // A folder with something in it cannot be replaced by the file.
    const fs::path blocked = dir / "blocked.ini";
    fs::create_directories(blocked / "inside");
    error.clear();
    CHECK(!fsr::prefs::save(blocked, chosen(), &error));
    CHECK(!error.empty());
    CHECK(fs::is_directory(blocked / "inside"));
    CHECK(!fs::exists(dir / "blocked.ini.tmp"));
}

// Explicit flag, then file, then default.
void flags_win_over_the_file(const fs::path& dir) {
    const fs::path file = dir / "order.ini";
    CHECK(fsr::prefs::save(file, chosen()));
    fsr::LocalPlan plan;
    fsr::prefs::load(file, plan);
    fsr::prefs::apply_flags({}, plan);
    CHECK(same_choices(plan, chosen()));
    fsr::prefs::apply_flags({"fit", "2", "3:2"}, plan);
    CHECK(!plan.integer_scale && plan.keyboard == 1 && !plan.wide);
    CHECK(plan.players == 4 && plan.pad == chosen().pad);
    fsr::prefs::apply_flags({"", "none", ""}, plan);
    CHECK(plan.keyboard == -1 && !plan.integer_scale && !plan.wide);
    fsr::prefs::apply_flags({"integer", "", "16:9"}, plan);
    CHECK(plan.integer_scale && plan.wide && plan.keyboard == -1);
    // Without a file, only the flagged field leaves its default.
    fsr::LocalPlan fresh;
    fsr::prefs::apply_flags({"integer", "", ""}, fresh);
    CHECK(fresh.integer_scale && !fresh.wide && fresh.keyboard == 0 && fresh.players == 2);
}

}  // namespace

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("fs_prefs_test_" + std::to_string(stamp));
    fs::create_directories(dir);
    missing_file_changes_nothing(dir);
    round_trip_and_overwrite(dir);
    bad_values_keep_what_was_there(dir);
    hand_edits_are_read(dir);
    keyboard_beyond_the_players_is_dropped(dir);
    duplicate_controllers_are_dropped(dir);
    unwritable_target_fails(dir);
    flags_win_over_the_file(dir);
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("fs_prefs: ok\n");
    return 0;
}
