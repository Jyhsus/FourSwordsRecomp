#pragma once
// fs_boot.h — Four Swords-specific runtime hooks.
//
// Phase 1 goal: boot straight into Four Swords, skipping the cartridge's
// game-select screen (A Link to the Past is never shown or modified).
// The hook addresses are still being located; until then these are no-ops
// and the stock title/select flow runs. See docs/DESIGN.md and docs/ROADMAP.md.

namespace gbarecomp { struct GbaInstance; }

namespace fsr {
// Called for every virtual GBA a netplay/local-co-op session creates.
void setup_netplay_instance(gbarecomp::GbaInstance& instance);
}
