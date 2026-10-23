# Parches locales a los submódulos

*English version: [README.md](README.md)*

Los cambios del motor van en [GBARecomp](https://github.com/mstan/gbarecomp).
Hasta que se aceptan allí, viven aquí como parche, redactado como un pull
request para upstream (asunto, descripción y diff), y se aplican al árbol de
trabajo del submódulo. El puntero del submódulo sigue en un commit de
upstream, así que los clones funcionan.

Aplícalos después de clonar o de actualizar el submódulo:

```sh
git -C gbarecomp apply ../patches/gbarecomp/0001-runtime-request-quit.patch
git -C gbarecomp apply ../patches/gbarecomp/0002-link-hub-transfer-time-override.patch
```

CMake se detiene con un mensaje si falta un parche necesario. No hagas commits
dentro de `gbarecomp`; es normal que `git status` muestre `m gbarecomp` (el paso
de la BIOS también cambia un archivo allí).

| Parche | Motivo | Upstream |
|---|---|---|
| `gbarecomp/0001-runtime-request-quit.patch` | `gbarecomp::request_quit()` termina `run_game()` de forma limpia desde un hook, para que salir de Four Swords vuelva al launcher con la partida guardada en disco (docs/DESIGN.es.md §8.1) | PR pendiente |
| `gbarecomp/0002-link-hub-transfer-time-override.patch` | `GbaLinkHub::set_multiplayer_transfer_cycles()` permite a un juego acortar una transferencia por cable; Four Swords usa 5.243 ciclos con dos GBAs, o el saludo del lobby nunca termina (docs/DESIGN.es.md §9) | PR pendiente |

Cuando upstream acepte un parche, actualiza el submódulo a una versión que lo
incluya y borra el archivo del parche, su fila aquí y la comprobación de CMake.
