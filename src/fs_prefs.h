#pragma once
// fs_prefs.h — what the player chose on the Local page, kept between runs in
// a small file next to the executable (docs/DESIGN.md section 10). SDL-free
// so tests/fs_prefs_test.cpp can check it.
//
//   version = 1
//   players = 2          2-4
//   pad1 = 1             1-16 or none; the same up to pad4
//   keyboard = 1         a player, 1 to `players`, or none
//   scale = fit          fit or integer
//   view = 3:2           3:2 or 16:9
//
// A controller is kept as its position in local_pads() ("Controller N" on the
// page), not as a device: if the order changes between runs, a player gets
// whichever controller is at that position then. The files each player plays
// and the .sav are not kept. The --local command line never reads or writes
// this file.

#include <filesystem>
#include <string>

#include "fs_local.h"

namespace fsr::prefs {

constexpr const char* kFileName = "fsr_local_settings.ini";
// Highest controller position kept; more than four can be connected.
constexpr int kMaxPadPosition = 16;

// The command line's --local-scale, --local-keyboard and --local-view, already
// checked by main(); "" when not given.
struct LocalFlags {
    std::string scale;      // fit or integer
    std::string keyboard;   // 1-4 or none
    std::string view;       // 3:2 or 16:9
};

// Reads the file into plan. A missing or unreadable file leaves plan as it is.
// A key that is absent or has a bad value leaves that field as it is, and
// unknown keys are ignored. Two players on one controller put all four
// controllers back; a keyboard player beyond the player count puts the
// keyboard back.
void load(const std::filesystem::path& file, LocalPlan& plan);

// Writes the file through a temporary one. False, with *error set, when it
// cannot be written; an older file then stays as it was.
bool save(const std::filesystem::path& file, const LocalPlan& plan, std::string* error = nullptr);

// Sets only the fields whose flag was given, so they win over the file.
void apply_flags(const LocalFlags& flags, LocalPlan& plan);

}  // namespace fsr::prefs
