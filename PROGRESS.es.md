# Progreso

*English version: [PROGRESS.md](PROGRESS.md)*

Hitos, trabajo pendiente, problemas conocidos y resultados sin verificar. Enlaza
con la evidencia de [docs/DESIGN.es.md](docs/DESIGN.es.md) y no la repite. Rama,
commits y tags: consulta Git. Última revisión: 25 nov 2026.

## Hitos

| Hito | Estado | Detalle |
|---|---|---|
| M1 — arranque hasta la selección de archivo | ✅ | DESIGN §1 |
| M2 — arranque directo en Four Swords, lobby estable con una GBA (fotograma 40000) | ✅ falta la opción en el launcher (`FSR_STOCK_MENU=1` muestra la pantalla original) | DESIGN §2 |
| Antes de M3, DESIGN §8 en orden: 8.1 guardado en 3 ranuras | ✅ (modelo de guardado, `--slot`, pantalla de archivos del launcher, salir al launcher) | DESIGN §8.1 |
| 8.2 el jugador no necesita BIOS (HLE) | 📋 | DESIGN §8.2 |
| 8.3 interfaz al estilo Ship of Harkinian | 📋 | DESIGN §8.3 |
| M3 — dos GBA en un mismo cable | ✅ en un proceso; falta la vía netplay (2 procesos / launcher) | DESIGN §9 |
| Multijugador local en pantalla dividida, 2–4 jugadores | ✅ | DESIGN §10, [PLAYTEST](docs/PLAYTEST.es.md) |
| Pantalla panorámica 16:9 | 🔬 experimental, opcional | DESIGN §11 |
| M4 — un jugador al estilo Anniversary | 📋 | DESIGN §3 |
| M5 — online 2 jugadores, luego 3–4 | 📋 | DESIGN §4 |
| M6 — investigación del contenido Anniversary | 🔬 puede ser imposible | DESIGN §5 |

## Pendiente

- Enviar el parche `request_quit()` a upstream; los otros dos parches locales también dicen «PR pending» ([patches/README.es.md](patches/README.es.md)). Comprobar la salida real con dos GBA (DESIGN §8.1).
- Causa raíz de la diferencia de tiempo del cable (ajuste de 5.243 ciclos, parche 0002) y la misma comprobación con netplay real (DESIGN §9).
- El juego online nunca escribe el `.sav` del jugador (runner upstream); el progreso hecho online se pierde (DESIGN §10).
- Las opciones de la página Local (mandos, teclado, escala) no se guardan; los botones del mando son fijos, según la disposición estándar de SDL (DESIGN §10).
- Iris de entrada de área en 16:9: los márgenes se abren fila a fila; alternativas sin elegir (DESIGN §11).
- Panorámico: un sprite de 64 píxeles de ancho puede aparecer tarde por la izquierda; se desconoce si el juego crea o elimina objetos por distancia dentro de los márgenes (DESIGN §11).

## Problemas conocidos

- La salida que algunos probadores tuvieron con cuatro jugadores no se ha reproducido ni arreglado (DESIGN §10; evidencia en [PLAYTEST](docs/PLAYTEST.es.md) §6).
- Cuatro jugadores en 16:9 no mantienen la velocidad completa en la máquina de desarrollo (tabla de DESIGN §10).
- La cobertura estática de los caminos de Four Swords no está completa: las sesiones enlazadas no tienen respaldo del intérprete y un probador encontró una entrada ausente (`0x08009A08`); no está demostrado que no queden otras (DESIGN §10).
- Colores deslavados y sprites parpadeantes comunicados por un probador: no reproducidos, sin comparación con una referencia. El valor por defecto de `game.toml` es `screen = "raw"` desde el 16 nov 2026 (era `"frontlit"`); no se ha demostrado que sea la causa (DESIGN §10).
- Problemas del motor (upstream) vistos: overlays gcc del self-heal que fallan (`g_runtime_resume_pc`), `terminate called without an active exception` al final de ejecuciones con ventana y `--frames`, y el buscador de tablas de salto que las sobredimensiona ([skill debug-project](.claude/skills/debug-project/SKILL.md), DESIGN §2).

## Sin verificar

- La fusión en el `.sav` de escrituras reales de EEPROM: ninguna ejecución automática hizo que el juego escribiera su EEPROM, ni tras un jefe (DESIGN §10, [PLAYTEST](docs/PLAYTEST.es.md) §3 F).
- Mandos reales: orden, marca «pulsado», desconectar y volver a conectar; no había ningún mando conectado en la máquina de desarrollo (DESIGN §10).
- El flujo con ventana pulsado de punta a punta (launcher → archivos → Multijugador → Local) y una partida real de cuatro personas (DESIGN §10).
- La explicación del orden de mandos de los probadores (orden de dispositivos distinto de las luces de jugador) es la causa probable, no confirmada (DESIGN §10).
- Panorámico: un combate contra jefe, una fase terminada jugando, un HUD con más corazones o más rupias, juego con personas, máquinas más lentas; no se ha visto el mensaje «P1 is down!» (DESIGN §11).
- El parpadeo de la sombra de Link en un monitor de 144 Hz y los modos de escritorio de 60/120 Hz: sin probar (DESIGN §10, [PLAYTEST](docs/PLAYTEST.es.md) §7).

## Discrepancias conservadas (sin resolver)

Los valores son los que cada documento da, con su fecha. Ninguno se ha vuelto a medir para decidir entre ellos.

- Funciones generadas: 1.537 y 32 fragmentos (README, receta de compilación, 22 oct 2026) frente a unas 33.000 / 33.465 (DESIGN §2) frente a unas 47.150 (DESIGN §9) y 47.077 entradas Thumb (DESIGN §10).
- Memoria de `aot_scan`: unos 20 GB, ~490k funciones (CLAUDE.md anterior) frente a «más de 6 GB» (DESIGN §1).
- Tiempo de regeneración: ~1 min (CLAUDE.md anterior, DESIGN §2) frente a ~2 min (README).
- Rendimiento con cuatro GBA en la máquina de desarrollo: ~11 ms en 3:2 y ~14 ms en 16:9 (CLAUDE.md anterior) frente a 11,88 y 15,68 ms (tabla de DESIGN §10) frente a 12,0 y 15,2 ms (DESIGN §11, probe sin ventana).
- La tabla *Status* del README sigue mostrando M3 como planificado (📋) y 1.537 funciones, mientras DESIGN §9 registra M3 como hecho (23 oct 2026).
- Los pasos de Windows del README mencionan «~2 min» y VS 2022/2026; la receta anterior se verificó solo con VS 2026.
- DESIGN en inglés y en español difieren en longitud (1.104 frente a 1.175 líneas); no se ha comprobado su equivalencia.
