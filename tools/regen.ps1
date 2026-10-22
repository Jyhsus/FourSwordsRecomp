param(
    [string]$Rom = (Join-Path $PSScriptRoot '..\roms\zelda_lttp_four_swords_usa.gba'),
    [string]$GbarecompRoot = (Join-Path $PSScriptRoot '..\gbarecomp'),
    [int]$MaxFunctions = 65536
)
# Regenerates generated/ from YOUR ROM. Run after building gba_recompile:
#   cmake -S gbarecomp -B gbarecomp\build ; cmake --build gbarecomp\build --config Release --target gba_recompile
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$romPath = (Resolve-Path $Rom).Path
$engine = (Resolve-Path $GbarecompRoot).Path
$tool = @("$engine\build\Release\gba_recompile.exe", "$engine\build\gba_recompile.exe") |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $tool) { throw "gba_recompile not found under $engine\build. Build it first." }

$expected = 'a272055abbbf6c26b0cd54c87395d01699589161'
$actual = (Get-FileHash -LiteralPath $romPath -Algorithm SHA1).Hash.ToLowerInvariant()
if ($actual -ne $expected) {
    throw "ROM SHA-1 mismatch: got $actual, expected $expected (USA 'A Link to the Past & Four Swords', AZLE). See baserom.md."
}

$args = @('--rom', $romPath, '--config', (Join-Path $root 'game.toml'))
$overlay = Join-Path $root 'symbols\AZLE_symbols.toml'
if (Test-Path -LiteralPath $overlay) { $args += @('--config', $overlay) }
$funcSyms = Join-Path $root 'symbols\imported_symbols.tsv'
if (Test-Path -LiteralPath $funcSyms) { $args += @('--symbols', $funcSyms) }
$args += @('--out', (Join-Path $root 'generated'), '--max-functions', $MaxFunctions)

Get-ChildItem (Join-Path $root 'generated') -Include *.cpp,*.h,*.json -Recurse -ErrorAction SilentlyContinue | Remove-Item
& $tool @args
exit $LASTEXITCODE
