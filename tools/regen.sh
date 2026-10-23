#!/usr/bin/env bash
# Regenerates generated/ from YOUR ROM (Linux/macOS). Build gba_recompile first:
#   cmake -S gbarecomp -B gbarecomp/build -DCMAKE_BUILD_TYPE=Release && cmake --build gbarecomp/build --target gba_recompile
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
rom="${1:-$root/roms/zelda_lttp_four_swords_usa.gba}"
tool="$root/gbarecomp/build/gba_recompile"
expected=a272055abbbf6c26b0cd54c87395d01699589161
actual=$(sha1sum "$rom" | cut -d' ' -f1)
[ "$actual" = "$expected" ] || { echo "ROM SHA-1 mismatch: $actual (expected $expected). See baserom.md." >&2; exit 1; }
args=(--rom "$rom" --config "$root/game.toml")
for overlay in AZLE_symbols.toml AZLE_data_pointers.toml; do
  [ -f "$root/symbols/$overlay" ] && args+=(--config "$root/symbols/$overlay")
done
[ -f "$root/symbols/imported_symbols.tsv" ] && args+=(--symbols "$root/symbols/imported_symbols.tsv")
find "$root/generated" -maxdepth 1 \( -name '*.cpp' -o -name '*.h' -o -name '*.json' \) -delete
exec "$tool" "${args[@]}" --out "$root/generated" --max-functions 65536
