# FourSwordsRecomp — Diseño y hoja de ruta

*English version: [DESIGN.md](DESIGN.md)*

Este documento explica **cómo** se piensa resolver cada objetivo del proyecto,
qué aporta ya [GBARecomp](https://github.com/mstan/gbarecomp) y qué falta
construir. Estados: ✅ hecho · 🔧 en curso · 📋 planeado · 🔬 investigación (puede resultar imposible).

---

## 0. Reglas básicas

* **Ningún dato de Nintendo en este repositorio.** Ni ROM, ni BIOS, ni código
  generado (deriva de la ROM), ni dump de DSi, ni recursos extraídos, ni carátulas.
  Cada jugador aporta sus propios dumps y el launcher los verifica por hash.
* **La imagen del cartucho nunca se modifica.** "Quitar" A Link to the Past
  significa *no arrancarlo nunca*, no recortarlo de la ROM (ambos juegos comparten
  un mismo binario y motor; recortarlo solo rompería cosas).
* **El código generado nunca se edita a mano.** Las correcciones van en
  `game.toml`, en los archivos de símbolos o en hooks propios en `src/`, y luego se regenera.

## 1. La base: recompilación estática con GBARecomp ✅/🔧

| Paso | Estado |
|---|---|
| ROM identificada: USA *A Link to the Past & Four Swords*, código `AZLE`, SHA-1 `a272055a…9161` | ✅ |
| BIOS verificada (SHA-1 `300c20df…3492`, la que espera GBARecomp) | ✅ |
| Tipo de guardado: EEPROM de 64 Kbit (`EEPROM_V122` en `0x08427298`) | ✅ |
| `game.toml` escrito. El código está en `0x08000000–0x0813FFFF`; el resto son datos. El análisis previo de tablas de punteros está desactivado por ahora: sobre esta ROM necesita más de 6 GB de RAM | ✅ |
| Generar la ROM completa y compilarla en un ejecutable nativo | ✅ |
| Arranque en frío → BIOS → logos de Nintendo/Capcom → selección de archivo (frame ~900) | ✅ |
| Cobertura estática de todo Four Swords (sin recurrir al intérprete) | 📋 |

GBARecomp traduce el código ARM7TDMI a C++ y lo enlaza con una GBA modelada
(PPU, audio, DMA, temporizadores, EEPROM, puerto serie). El código que el
analizador no encuentra se interpreta la primera vez y se guarda compilado, así
que el juego es jugable mientras se completa la cobertura.

Referencia útil: *The Minish Cap* lo hizo el mismo estudio (Capcom / Flagship)
justo después de Four Swords y comparte mucho del diseño del motor. La
decompilación [zeldaret/tmc](https://github.com/zeldaret/tmc) y
[MinishCapRecomp](https://github.com/mstan/MinishCapRecomp) son las referencias
más cercanas para nombrar funciones y estructuras. Solo se importan *nombres y
direcciones*, nunca código decompilado.

## 2. Arrancar directamente en Four Swords ✅ (falta la opción del launcher)

En el cartucho, el jugador elige primero un archivo de guardado ("CHOOSE A FILE")
y después entre *A Link to the Past* y *Four Swords*.

**Cómo lo hace el cartucho** (encontrado con el depurador TCP, 22 oct 2026):

| Dirección | Qué es |
|---|---|
| `0x03006D10` (u8) | Modo de juego actual; el bucle principal en `0x0800B1B2` llama cada frame a `game_mode_handlers[modo]` (tabla `0x0842731C`) |
| `0x03005050` (u8) | Subestado del modo actual |
| `0x0800B5EC` | `set_game_mode(r0)`: escribe el modo y pone a cero los subestados |
| modo 8 / 6 / 9 / 13 | Selección de archivo / "CHOOSE A GAME" (`0x0803074C`) / Four Swords / A Link to the Past |
| `0x02016D62` (u8) | Cursor de "CHOOSE A GAME": 0 = LttP, 1 = Four Swords |
| `0x08030E0C` | `start_four_swords`: inicializa Four Swords y llama a `set_game_mode(9)` |

Tras elegir archivo, la selección de archivo llama a `set_game_mode(6)`. En
"CHOOSE A GAME", elegir Four Swords llama a `start_four_swords` si hay más de
una GBA conectada; con una sola muestra antes el aviso "This is a Multi-Pak
multiplayer game", y pulsando A se llega igualmente a la intro de Four Swords.

**Implementación:** `src/fs_boot.cpp` engancha el manejador del modo 6
`0x0803074C` (declarado como `[[mod_function_hook]]` en `game.toml`). En su
primer frame, el manejador se convierte en una llamada de cola a
`start_four_swords`, así que al elegir archivo se va directo a la intro de Four
Swords en todas las GBA virtuales. `FSR_STOCK_MENU=1` desactiva el hook para
ver la pantalla original; falta la opción en el launcher.

La primera versión enganchaba `set_game_mode`. Una revisión antes de fusionar
encontró que se le escapaba una entrada al modo 6: `AgbMain` (`0x0800B084`)
llama a `0x08068108` al arrancar. Si IWRAM contiene una marca de traspaso
(`0x54463453` en `0x03000000` más una cadena de 7 bytes), que deja un reinicio
por software, escribe el modo 6 en línea (`0x0803072C`) sin llamar a
`set_game_mode`. Un arranque en frío nunca tiene la marca. Enganchar el
manejador cubre todas las rutas; la entrada en línea se comprobó escribiendo
modo 6 / subestado 0 por TCP en la selección de archivo, y empezó la intro de
Four Swords.

**Cobertura estática necesaria:** a las tablas de modos solo se llega por
punteros a función, así que se declaran como `[[jump_table]]` en `game.toml`.
Al sembrarlas apareció un problema del recompilador: las tablas de saltos
auto-detectadas sin cota ("BX walked") se medían más allá de su final y
sembraban "funciones" ARM en datos que acababan en el relleno `0xFF` de la ROM
(~490.000 funciones, ~20 GB de RAM). `tools/audit_jump_tables.py` las recorta;
su salida es el overlay `symbols/AZLE_symbols.toml`. Resultado: unas 33.000
funciones (33.465 con todas las semillas del M2), generadas en ~1 min. Conviene corregirlo también upstream.

**¿Es segura la llamada de cola?** `start_four_swords` no lee `r0`–`r3`: su
primera instrucción tras `push {lr}` es `bl 0x0800BED8`, que escribe `r0` y
`r1` antes de leerlos. Así que da igual que el hook entre con lo que deje en
`r0` el bucle principal en vez del número de consolas que deja ahí el camino
original. Estas rutas entran en el modo 6, y el hook las cambia todas:

| Entrada | Contexto | Efecto del hook |
|---|---|---|
| `0x0800F87A` `set_game_mode(6)` | Selección de archivo, tras elegir partida | El buscado: directo a Four Swords |
| `0x0803003E` `set_game_mode(6)` | Subestado del modo 5 (tabla `0x084278DC`), reinicializa y vuelve a "CHOOSE A GAME" | Vuelve a Four Swords en su lugar (correcto en una build solo de Four Swords) |
| `0x080310A6` `set_game_mode(6)` | Subestado del aviso Multi-Pak (tabla `0x08427980`), justo tras su propia llamada a `start_four_swords` | Inalcanzable con el hook activo: los subestados del modo 6 nunca se ejecutan |
| `0x0803072C` escritura en línea | `AgbMain` tras un reinicio por software con la marca de traspaso en IWRAM | Directo a Four Swords |

**Lobby alcanzado y estable** (22 oct 2026, Windows, sin ventana). Las
entradas se reprodujeron con `GBARECOMP_INPUT_REPLAY` (crear partida, elegirla,
pulsar A cada 40 frames hasta el frame 20000) y luego el juego quedó en reposo
hasta el frame 40000. Tras la intro completa (Zelda, Vaati, los cuatro Links)
llega al título de Four Swords con "Linking with other systems… Please wait a
moment." en el modo 9, subestado 2, y se queda ahí: sin aborts, con la pila de
retornos a profundidad 0 en los frames 3000, 8000, 20000 y 40000, y pulsar
A/B/START en el lobby no cambia nada.

**El aviso "evicted 511 host-less call-return entries"** era una fuga real
que acababa en `generated call-return stack overflow` (abort) durante la intro.
El runtime apila una dirección de retorno en cada `BL` y la saca al volver.
Los savestates mostraban la pila llenándose de `0x08134A66` y `0x08134A7A`,
las direcciones de retorno de dos trampolines de m4a que llaman a
`*(0x02031AB8)` / `*(0x02031ABC)`. Son las entradas 34/35 (contando desde 0)
de la tabla de saltos del driver de sonido m4a, cuya copia en RAM empieza en
`0x02031A30`, así que el buscador nunca vio los destinos. Todo el driver se ejecutaba en el puente de self-heal,
mezclado con trampolines nativos, y esos retornos nunca se sacaban. El PC del
aviso (`0x08009476`, luego `0x08034FE8`) solo es la llamada que se estaba
apilando cuando la pila se llenó. Esa misma mezcla hacía abortar el puente con
la rutina ARM de IWRAM `0x03005AE0` (copiada desde `0x080008A8`) en cuanto
sonaban efectos. La fuga es anterior al hook: los savestates de la build
original ya tenían 80 entradas.

Arreglo en `game.toml`: la tabla de saltos de m4a (`0x083C2654`, 36
entradas), la tabla de manejadores de IRQ (`0x08427360`, 12 entradas, copiada
por DMA a `0x03000B70` en `0x0800B6D0`; la de VBlank, justo antes, apunta a
código en EWRAM y se deja fuera), `m4aSoundMain` (`0x081346FC`), `SoundMain`
(`0x081339D4`), `MPlayMain` (`0x08134074`), `0x0800B7FC`, un `[[code_copy]]`
más `[[extra_func]]` para `0x03005AE0`, y el grupo de callbacks de
`0x0842757C` (3 entradas). Después de eso ya no se interpreta ninguna
dirección de la ROM en la ruta de Four Swords (comprobado con un registro de
misses limpio durante la ejecución de 40000 frames). La ruta original de A Link
to the Past sigue interpretando muchas funciones de la ROM; ese juego queda
fuera del alcance.

**Nota sobre pruebas:** avanzar frame a frame por TCP (`run_frames`) destapaba
antes estos problemas del puente que la ejecución libre, porque cada paso
termina en un PC arbitrario. Para ejecuciones largas es mejor `--frames N` con
`GBARECOMP_INPUT_REPLAY` (líneas `frame,0xKEYINPUT`) y `--dump-png` /
`--dump-state`.

**Pendiente para el M3:** el código Thumb en IWRAM `0x03006D18`–`0x03006E96`
sigue pasando por el puente (candidato a tabla de saltos, probablemente el
código del enlace serie; aún no se ha encontrado de dónde se copia). No hace
falta con una sola GBA.

## 3. Un jugador como en la Anniversary Edition 📋

**Qué hace la Anniversary Edition de DSi:** dos Links en lugar de cuatro, sobre
los mapas de dos jugadores. El segundo Link sigue al líder; L/R cambia qué Link
controlas (el otro se sienta y se puede levantar); un silbato (X) llama al otro
Link y devuelve el control al verde. Los puzles de bloques que requieren varios
Links se resuelven con ayuda automática.

**Por qué el juego de GBA no puede hacerlo solo:** cada jugador de Four Swords es
una GBA distinta con su propia copia del juego, sincronizadas por el cable Link.
El juego no deja empezar una aventura con una sola consola.

**Plan — un "grupo virtual" local:**

1. Iniciar una sesión *local* con **dos GBAs virtuales unidas por el cable
   emulado**, en el mismo proceso. GBARecomp ya ejecuta varias `GbaInstance`
   sobre una línea de tiempo común con un hub de cable (`GbaLinkHub`) para el
   juego en red; el modo de un jugador reutiliza eso sin red.
2. Mostrar solo la pantalla del Link que controlas.
3. Un **enrutador de entradas** decide qué recibe cada GBA virtual en cada frame:
   * el Link controlado recibe tu mando;
   * el otro recibe un *controlador de seguimiento*: entradas calculadas a partir
     de las posiciones de ambos Links leídas de la RAM (caminar hacia el líder,
     detenerse al estar cerca).
4. **Cambio (L/R):** intercambiar quién recibe el mando real y qué pantalla se ve.
   **Silbato:** llamada/teletransporte del otro Link, hecho guiando al
   seguidor o, si hace falta, con un hook pequeño y propio.
5. Ayuda en puzles (empujar bloques entre dos, Nokkun/Hikkun): detectar la
   situación por el estado del juego y hacer que el seguidor imite el empuje.

Los pasos 1–4 solo necesitan las direcciones en RAM de los objetos de jugador,
trabajo normal de ingeniería inversa. El paso 5 es el más difícil y va al final.

## 4. Multijugador en línea, de 2 a 4 jugadores 🔧

El netplay de GBARecomp ya admite **2–4 puestos**, con sincronización por
retardo o rollback (retardo por defecto con 3–4 jugadores), LAN o Internet
mediante un relé UDP, y puntos de control de sesión. `src/main.cpp` fija
`max_players = 4`.

**Por qué esto no es "un cable Link por Internet":** mandar las transferencias
del cable por la red haría que cada intercambio esperara al ping, y el juego se
trabaría o desincronizaría. En su lugar, **el PC de cada jugador ejecuta todas las
GBAs virtuales** de la partida, unidas por un cable emulado *localmente*. Por la
red solo viajan las entradas de los mandos. Todos los PCs calculan exactamente el
mismo resultado, así que el código multijugador original funciona sin cambios y
la latencia se maneja con retardo de entrada o rollback.

| Paso | Estado |
|---|---|
| Netplay compilado, 4 puestos permitidos | ✅ (código) |
| Verificar el protocolo serie de Four Swords en `GbaLinkHub` (2 consolas, local) | 📋 |
| 3 y 4 consolas, local | 📋 |
| En línea 2 jugadores, luego 3–4 | 📋 |
| Cada GBA virtual arranca directo en Four Swords (sección 2) | 📋 |

Cada jugador necesita su propia ROM y BIOS verificadas, igual que en 2002 cada
jugador necesitaba su propio cartucho.

## 5. Contenido de la Anniversary Edition 🔬

La Anniversary Edition añadió el **Reino de los Recuerdos** (zonas basadas en
*The Legend of Zelda*, *A Link to the Past* y *Link's Awakening*) y la
**Prueba del Héroe** (versiones más difíciles de las fases).

**Estado honesto: puede que no sea posible.** La Anniversary Edition es un
programa DSiWare para las CPUs ARM9/ARM7 de la DS, no código de GBA, así que
GBARecomp no puede recompilarlo y no se puede reutilizar su código. Lo que *quizá*
sí se pueda:

1. Investigar, con el dump de DSi del propio jugador, si las fases nuevas se
   guardan como datos (salas, tilesets, objetos) en un formato parecido al de GBA.
2. Si es así: un conversor que se ejecute **en el equipo del jugador al
   iniciar**, lea su dump de DSi y construya las fases extra como mod (`.gbamod`)
   para el motor de GBA. Nada de la versión de DSi se distribuiría nunca.
3. La Prueba del Héroe son sobre todo versiones más duras de fases existentes;
   parte podría rehacerse con ajustes de datos incluso sin el dump de DSi.

Si la investigación muestra que los formatos no tienen relación, este objetivo
se descarta y se documenta así.

## 6. Launcher 🔧

El launcher es el **recomp-ui** compartido de GBARecomp (Dear ImGui), ya
enlazado en `CMakeLists.txt`:

* pide la **ROM USA** y la **BIOS de GBA**, comprueba el SHA-1 y las recuerda;
* ajustes (vídeo, audio, controles) y pestaña **Netplay** (crear/unirse, 2–4 jugadores);
* 📋 elección de modo "Un jugador (estilo Anniversary)" / "Multijugador";
* 🔬 un campo opcional para la **Anniversary Edition de DSi**, solo si la sección 5 resulta viable.

## 7. Hitos

1. **M1 — Arranca:** el ejecutable nativo llega a la pantalla de selección del cartucho.
2. **M2 — Arranque directo en Four Swords** y lobby estable con una consola.
3. **M3 — Sesión local de 2 consolas** por el cable emulado (prueba el protocolo).
4. **M4 — Un jugador estilo Anniversary** (seguir, cambiar, silbato).
5. **M5 — En línea 2 jugadores**, luego **3–4**.
6. **M6 — Investigación del contenido Anniversary** (viable / no viable).
