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

## 2. Arrancar directamente en Four Swords 📋

En el cartucho, el jugador elige primero un archivo de guardado ("CHOOSE A FILE")
y después entre *A Link to the Past* y *Four Swords*. El plan:

1. Con el depurador TCP (`--tcp`) y las trazas de ejecución, encontrar la rutina
   que lee esa selección y salta a la entrada de Four Swords.
2. Añadir un hook propio (en `src/fs_boot.cpp`, registrado con
   `[[mod_function_hook]]` en `game.toml`) que elija Four Swords automáticamente
   en cada GBA virtual.
3. Mantener una opción del launcher para ver la pantalla original al validar.

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
