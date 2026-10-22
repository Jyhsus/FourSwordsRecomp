# Base ROM and BIOS / ROM base y BIOS

This project ships **no ROM and no BIOS**. You need your own dumps.
Este proyecto **no incluye ROM ni BIOS**. Necesitas tus propios dumps.

| File / Archivo | Expected / Esperado |
|---|---|
| Cartridge / Cartucho | *The Legend of Zelda: A Link to the Past & Four Swords* (USA), game code `AZLE`, 8 MiB |
| ROM SHA-1 | `a272055abbbf6c26b0cd54c87395d01699589161` |
| ROM SHA-256 | `f328f8f07d736288a00c80d31cc1630f3aa02aaf20efdcba73d31dae832b5d76` |
| ROM MD5 | `3287ca66e5cc285a9fe3a922051e84c6` |
| ROM CRC32 | `8E91CD13` |
| GBA BIOS SHA-1 | `300c20df6731a33952ded8c436f7f186d25d3492` |

The ROM hashes were taken from the maintainer's own dump (header `GBAZELDA` /
`AZLE01`, revision 0). Cross-checking them against an independent catalog is
still pending; open an issue if your clean USA dump hashes differently.

Not accepted / No se aceptan: trimmed, patched (IPS/UPS/BPS), translated or
other-region ROMs. Europe/Japan can be added later with their own hash entry.

Optional, future / Opcional, futuro: a dump of *Four Swords Anniversary Edition*
(DSiWare) — only if the research in [docs/DESIGN.md](docs/DESIGN.md#5-anniversary-edition-content-) succeeds.
