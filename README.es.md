# FourSwordsRecomp

**The Legend of Zelda: Four Swords (GBA), recompilado estáticamente a PC nativo — solo, sin A Link to the Past.**

*🇬🇧 Read in English: [README.md](README.md)*

> **Construido sobre [GBARecomp](https://github.com/mstan/gbarecomp), de Matthew Stan.**
> Este proyecto no existiría sin él: GBARecomp aporta el recompilador ARM7TDMI,
> el hardware de GBA modelado, el launcher, el juego en red por cable Link y el
> runtime que se autocorrige. Si te gusta este proyecto, por favor dale ⭐ y apoya a
> [GBARecomp](https://github.com/mstan/gbarecomp) y sus otros ports, y reporta
> allí los errores del framework.

> ⚠️ **Desarrollo temprano.** Todavía no es una versión jugable. Mira el
> [Estado](#estado) y el [diseño y hoja de ruta](docs/DESIGN.es.md).

---

## ¿Qué es esto?

En 2002, *Four Swords* salió en el mismo cartucho de Game Boy Advance que
*A Link to the Past*. Requería de 2 a 4 jugadores, cada uno con su GBA, su
cartucho y un cable Link. En 2011 Nintendo publicó la *Anniversary Edition* para
DSi/3DS, que añadió un modo para un jugador y fases nuevas, pero solo estuvo
disponible gratis por tiempo limitado.

FourSwordsRecomp busca que el juego original de GBA se pueda jugar de forma nativa en PC:

| Objetivo | Cómo | Estado |
|---|---|---|
| **Solo Four Swords** | Arrancar directamente en Four Swords; *A Link to the Past* nunca se inicia | 📋 planeado |
| **Un jugador, estilo Anniversary** | Dos GBAs virtuales en local; controlas un Link y el otro te sigue; cambias con L/R | 📋 planeado |
| **Multijugador en línea, 2–4 jugadores** | Cada PC ejecuta todas las GBAs virtuales; por la red solo viajan las entradas (netcode con retardo o rollback) | 🔧 framework listo, juego sin probar |
| **Contenido de la Anniversary Edition** | Solo investigación: la versión de DSi es otro código; quizá convertir *datos* de fases desde el dump del propio jugador | 🔬 puede ser imposible |
| **Launcher** | El launcher de GBARecomp: pide la ROM USA y la BIOS, las verifica, lobby de netplay | 🔧 conectado, sin probar |

El razonamiento detallado de cada objetivo está en **[docs/DESIGN.es.md](docs/DESIGN.es.md)**.

### Qué significa "recompilación estática"

El código máquina ARM del juego se traduce **por adelantado** a C++ y se compila
como un ejecutable normal. La lógica del juego corre como código nativo en vez de
emularse instrucción por instrucción. El hardware a su alrededor (gráficos,
sonido, temporizadores, chip de guardado, puerto Link) lo modela el runtime de
GBARecomp. Eso permite cosas difíciles para un emulador, como engancharse a las
funciones del propio juego para añadir un modo de un jugador.

## Qué necesitas

* Tu **propio dump** de *The Legend of Zelda: A Link to the Past & Four Swords*
  (**USA**, `AZLE`). SHA-1 y detalles en [baserom.md](baserom.md).
* Tu **propio dump de la BIOS** de GBA (SHA-1 `300c20df6731a33952ded8c436f7f186d25d3492`).

**No se incluye ni se distribuirá nunca ROM, BIOS, código generado ni recursos del
juego.** El C++ generado deriva de la ROM, así que se produce en tu equipo y está
en `.gitignore`.

## Compilar (Windows)

Igual que en la guía de Windows de GBARecomp, el recompilador se compila con
MSVC de Visual Studio y el juego jugable con MinGW-w64 de MSYS2.

Requisitos: Git, Visual Studio 2022 o 2026 (bastan las Build Tools) con
*Desarrollo para el escritorio con C++*, CMake, y [MSYS2](https://www.msys2.org/)
con el toolchain MinGW64 y SDL2:

```sh
# en la consola MSYS2 MINGW64
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2
```

```powershell
git clone --recurse-submodules https://github.com/<tu-usuario>/FourSwordsRecomp.git
cd FourSwordsRecomp

# 1. Compilar el recompilador (MSVC; usa "Visual Studio 17 2022" con VS 2022)
cmake -S gbarecomp -B gbarecomp\build -G "Visual Studio 18 2026" -A x64
cmake --build gbarecomp\build --config Release --target gba_recompile

# 2. Colocar tus archivos (nunca se suben)
mkdir roms
copy "D:\ruta\a\tu\AZLE.gba" roms\zelda_lttp_four_swords_usa.gba
copy "D:\ruta\a\tu\gba_bios.bin" gbarecomp\bios\gba_bios.bin

# 3. Recompilar tu BIOS una vez y generar C++ desde tu ROM (~2 min)
cd gbarecomp; .\build\Release\gba_recompile.exe --bios bios\gba_bios.bin --config bios\gba_bios.toml; cd ..
powershell -ExecutionPolicy Bypass -File tools\regen.ps1

# 4. Compilar el juego (MinGW64 + SDL2, con ventana y launcher)
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target FourSwordsRecomp

# 5. Ejecutar (C:\msys64\mingw64\bin debe estar en el PATH por SDL2.dll y el runtime de C++)
.\build\FourSwordsRecomp.exe
```

El paso 3 modifica un archivo de relleno versionado dentro del submódulo
`gbarecomp` (`src/runtime/generated_bios/`). Ese archivo deriva de tu BIOS:
nunca lo subas.

En Linux/macOS usa `tools/regen.sh` y una compilación normal de CMake. La
[guía de inicio de GBARecomp](https://github.com/mstan/gbarecomp/blob/main/docs/GETTING_STARTED.md)
cubre los problemas de herramientas.

## Estado

| Hito | Estado |
|---|---|
| ROM, BIOS y tipo de guardado identificados; `game.toml` escrito | ✅ |
| ROM completa generada a C++ (1.537 funciones; lo que falta se interpreta y se guarda compilado) | ✅ |
| M1 — el ejecutable nativo pasa por la BIOS y los logos de Nintendo/Capcom hasta la pantalla de selección de archivo | ✅ (Linux sin ventana, 22 oct 2026) |
| M2 — arranque directo en Four Swords: al elegir partida se salta "CHOOSE A GAME" y empieza la intro de Four Swords (`FSR_STOCK_MENU=1` recupera la pantalla original) | ✅ (Windows, 22 oct 2026; llega al lobby con una GBA y aguanta estable 40.000 frames; falta la opción del launcher) |
| M3 — dos consolas enlazadas en local por el cable emulado | 📋 |
| M4 — un jugador estilo Anniversary | 📋 |
| M5 — en línea 2 jugadores, luego 3–4 | 📋 |
| M6 — investigación del contenido Anniversary (viable / no viable) | 🔬 |

## Estructura del repositorio

| Ruta | Contenido |
|---|---|
| `gbarecomp/` | Framework GBARecomp (submódulo git) |
| `recomp-ui/` | Interfaz del launcher compartida (submódulo git) |
| `game.toml` | Configuración del recompilador y del runtime para `AZLE` |
| `src/` | Código propio: punto de entrada, hooks de Four Swords |
| `tools/` | `regen.ps1` / `regen.sh` — regenerar C++ desde tu ROM |
| `symbols/` | Nombres de funciones/datos descubiertos con ingeniería inversa |
| `generated/` | C++ generado localmente (ignorado por git) |
| `docs/` | Diseño, hoja de ruta, notas de investigación (inglés y español) |

## Contribuir

Toda ayuda es bienvenida, sobre todo en ingeniería inversa (encontrar la rutina de
selección de juego, las estructuras de los jugadores en RAM, el protocolo Link) y
en pruebas. Los problemas del motor van al
[issue tracker de GBARecomp](https://github.com/mstan/gbarecomp/issues); los
específicos de Four Swords, aquí.

## Créditos

* **[GBARecomp](https://github.com/mstan/gbarecomp)** — Matthew Stan y colaboradores: recompilador, runtime, launcher ([recomp-ui](https://github.com/mstan/recomp-ui)) y netplay.
* **[zeldaret/tmc](https://github.com/zeldaret/tmc)** — decompilación de *The Minish Cap*, referencia para el motor compartido de Capcom/Flagship.
* **[recomp-net](https://github.com/RetroPortingToolKit/recomp-net)** y **[rbengine](https://github.com/RetroPortingToolKit/rbengine)** — red y rollback que usa GBARecomp.

## Licencia

[PolyForm Noncommercial 1.0.0](LICENSE), la misma licencia que GBARecomp, sobre
el que se construye este proyecto. Solo uso no comercial.

*The Legend of Zelda*, *Four Swords* y *A Link to the Past* son marcas de
Nintendo. Este proyecto no está afiliado ni respaldado por Nintendo ni por los
autores de GBARecomp.
