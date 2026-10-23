# Local patches to submodules

*Versión en español: [README.es.md](README.es.md)*

Engine changes belong upstream in [GBARecomp](https://github.com/mstan/gbarecomp).
Until a change is merged there, it lives here as a patch, written as an
upstream pull request (subject, description, diff), and is applied to the
submodule's working tree. The submodule pointer itself stays on an upstream
commit, so clones keep working.

Apply after cloning or updating the submodule:

```sh
git -C gbarecomp apply ../patches/gbarecomp/0001-runtime-request-quit.patch
git -C gbarecomp apply ../patches/gbarecomp/0002-link-hub-transfer-time-override.patch
```

CMake stops with a message if a required patch is missing. Do not commit
inside `gbarecomp`; `git status` showing `m gbarecomp` is expected (the BIOS
step also changes a file there).

| Patch | Why | Upstream |
|---|---|---|
| `gbarecomp/0001-runtime-request-quit.patch` | `gbarecomp::request_quit()` ends `run_game()` cleanly from a hook, so quitting Four Swords returns to the launcher with the save flushed (docs/DESIGN.md §8.1) | PR pending |
| `gbarecomp/0002-link-hub-transfer-time-override.patch` | `GbaLinkHub::set_multiplayer_transfer_cycles()` lets a game shorten a cable transfer; Four Swords uses 5,243 cycles for two GBAs, or its lobby handshake never finishes (docs/DESIGN.md §9) | PR pending |

When a patch is merged upstream, update the submodule past it and delete the
patch file, its row here and the CMake check.
