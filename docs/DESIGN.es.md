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

Entre el M2 y el M3 van los puntos de la sección 8: partidas en 3 ranuras, sin
BIOS para el usuario e interfaz estilo Ship of Harkinian.

## 8. Plan de trabajo después del M2 (por orden de prioridad)

Estos tres puntos van antes del M3, en este orden.

### 8.1 Partidas en 3 ranuras 🔧 (prioridad)

**Problema:** al salir de Four Swords el juego vuelve al modo 6 ("CHOOSE A
GAME"), y el hook de arranque directo (sección 2, ahora en el manejador del
modo 6, `0x0803074C`) lo devuelve a Four Swords. No hay forma de salir.

**Modelo decidido:** un solo `.sav`. Las 3 ranuras originales del cartucho son
las 3 partidas del launcher. No hay formato de guardado propio, y el `.sav`
sigue sirviendo en cualquier emulador.

Ingeniería inversa necesaria:

1. Estructura de las ranuras en la EEPROM (64 Kbit, 8 KiB): posición y tamaño
   de cada ranura, el nombre, la marca de "ranura en uso" y el checksum
   (algoritmo y rango que cubre).
2. La rutina que carga una ranura en la selección de archivo (modo 8), para
   aplicar la elección del launcher sin mostrar "CHOOSE A FILE".
3. El modo que pide Four Swords al salir, para que "Salir al menú" vuelva al
   launcher en vez de entrar en bucle.
4. El nombre de la partida: longitud máxima y caracteres permitidos (la tabla
   de caracteres del propio juego).

Resultado:

* El launcher muestra las 3 ranuras con su nombre y permite crearlas,
  renombrarlas y borrarlas. El nombre del launcher **es** el nombre de la
  partida dentro del juego.
* Al elegir una ranura el juego entra directo a Four Swords, sin "CHOOSE A FILE".
* "Salir al menú" vuelve al launcher.

**Hallazgos (23 oct 2026):**

*Estructura de la EEPROM.* El `.sav` guarda cada bloque de 8 bytes de la
EEPROM con los bytes invertidos; los offsets de abajo están en el orden del
juego. Una tabla de descriptores en `0x0814C654` (8 entradas de 12 bytes:
tamaño, cabecera A, cabecera B, datos A, datos B) gobierna todos los accesos.
Cada copia se escribe en su offset y otra vez en `+0x1000`; la copia B solo se
usa si falla la escritura de la A.

| Entrada | Tamaño | Cabecera A / B | Datos A / B | Contenido |
|---|---|---|---|---|
| 0–2 | `0x480` | `0x40`, `0x50`, `0x60` | `0x100`, `0x600`, `0xB00` | parte de A Link to the Past de las partidas 1–3 |
| 3 | `0x20` | — | `0x000` | texto de firma del cartucho |
| 4–6 | `0x40` | `0x70/0x80`, `0x90/0xA0`, `0xB0/0xC0` | `0x580/0x5C0`, `0xA80/0xAC0`, `0xF80/0xFC0` | cabeceras de partida de Four Swords |
| 7 | `0x10` | `0x20/0x30` | `0x0E0/0x0F0` | bloque de sistema, se carga en `0x03000000` |

Una cabecera de estado es `u16 suma, u16 -suma, u32 firma`: `S4FT` = válida,
`INIT` = vacía, `DelF` = borrada (estas dos con `0xFFFF 0xFFFF`). El checksum
(`0x080679B0`) suma cada `u16` XOR el número de bytes que quedan, primero
sobre la firma y luego sobre los datos. Cabecera de partida: `+0x04` = `0x11`,
`+0x08` nombre (6 bytes, rellenos con ceros), `+0x24`/`+0x25` = 1 en una
partida nueva, `+0x3F` = 3. Una ranura está en uso si carga y su nombre no
está vacío (`0x080111F8`). Crear una partida deja la parte de A Link to the
Past en `INIT`; borrarla escribe `DelF` en las dos partes y no toca los datos.
Los dos juegos comparten las 3 partidas, así que borrar una también borra su
progreso de A Link to the Past, igual que en el cartucho. Código:
`0x08067940`–`0x080681D0` (EEPROM), `0x080111F8`–`0x08011476` (ranuras; copia
en RAM de las cabeceras en `0x0200B250 + i*0x40`).

*Nombre.* Como máximo 6 caracteres, en Windows-1252: `A–Z a–z 0–9`, espacio,
`' - : ! ? , .`, `À Â Ä Æ Ç È É Ê Ë Ì Î Ï Ð Ñ Ò Ô Ö Œ Ù Û Ü` y sus minúsculas
(sacado tecleando cada casilla de "ENTER A NAME").

*Bloque de sistema (`0x03000000`, entrada 7).* `S4FT`, `[4]` = partida
actual, `[5]`–`[8]` opciones, `[9]` longitud del nombre y `[10..15]` el nombre
que usa Four Swords. Tras un reinicio por software con `GoGameS` en `[9]`,
`AgbMain` (`0x08068108`) se salta la selección de archivo y entra directamente
en el modo 6 (`0x0803072C`). El sub-estado 0 del modo 6 (`0x080308F8`) recarga
la partida `[4]` (`0x08011418`) y copia su nombre al bloque (`0x080681D0`).

*Implementado.* `src/fs_save.{h,cpp}` lee y edita el `.sav` igual que el juego
(probado con `tests/fs_save_test.cpp` contra partidas escritas por el juego;
`Image::blank()` coincide byte a byte con un `.sav` de primer arranque y toma
los textos fijos de la ROM del jugador). `--slot 1-3` arranca una partida
directamente: un hook en el reinicio de pantalla `0x0800BED8`, filtrado a la
llamada de AgbMain, pone `[4]` y los mismos bytes de modo que `0x0803072C`. El
hook del modo 6 ahora deja correr el sub-estado 0 antes de iniciar Four
Swords; hasta ahora, si se saltaba la selección de archivo, Four Swords
arrancaba con el nombre por defecto "LINK". Comprobado: `--slot 2` llega a la
intro de Four Swords en el frame 300 (por la selección de archivo tardaba
~900) con el nombre correcto en el bloque de sistema, y al lobby en el frame
20000 con la pila de retornos a 0, tanto con `--slot` como por la selección de
archivo.

*Salida.* Four Swords sale por el sub-estado 7 (`0x08030018`) de la tabla de
`0x084278C0` (usada en `0x0802FB20`; el sub-estado 6 vuelve al modo 9): espera
al fundido, reinicia la pantalla y llama a `set_game_mode(6)` (`0x0803003E`).
Con una sola GBA no se puede salir del lobby, así que este camino necesita el
M3 para probarse. Volver al launcher requiere una llamada del motor que
termine `run_game()` de forma limpia (el `.sav` se escribe cuando paran las
escrituras y al cerrar). GBARecomp aún no la ofrece: trabajo para upstream.

**Falta:** la pantalla del launcher (lista, crear, renombrar, borrar, jugar),
el hook de salida y la petición de cierre en upstream.

### 8.2 Sin BIOS para el usuario 📋

Ahora el jugador tiene que aportar la BIOS de GBA. GBARecomp ya tiene HLE
opcional de las SWI de cálculo, copia y descompresión
(`src/runtime/bios_hle.cpp`) y un salto del arranque (`bios_hle_boot_skip`),
pero `IntrWait` (`0x04`), `VBlankIntrWait` (`0x05`) y el vector de
interrupciones (`0x18`) siguen ejecutándose en la BIOS recompilada, así que
todavía hay que enlazarla.

Plan:

1. HLE de `IntrWait` (`0x04`) y `VBlankIntrWait` (`0x05`): esperar hasta que
   aparezcan los bits pedidos en `0x03007FF8` y luego borrarlos, como hace la
   BIOS real.
2. HLE del vector de interrupciones: guardar registros, llamar al manejador de
   `0x03007FFC` y volver, sin código de la BIOS.
3. HLE del arranque (el salto que ya existe) y poder iniciar **sin ningún
   archivo de BIOS**.
4. Dos versiones, elegidas al compilar (política de HLE de GBARecomp):
   **desarrollo** con la BIOS real como referencia, y **pública** solo con HLE.
5. Validar frame a frame contra la BIOS real (comparando savestates y
   framebuffer en el arranque y en el lobby de Four Swords).
6. Prepararlo como contribución a GBARecomp: son servicios del motor, no
   código propio del juego.

### 8.3 Interfaz estilo Ship of Harkinian 📋

* Una pantalla propia al abrir: **Jugar / Multijugador / Opciones / Salir**.
* Una barra de menú dentro del juego que se abre con **F1**.
* Se mantiene la lista de salas; el inicio de sesión con Discord sigue siendo opcional.
* Solo arte propio (nada de recursos de Nintendo, ver sección 0).
