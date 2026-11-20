# Prueba con personas: multijugador local en pantalla dividida

*English version: [PLAYTEST.md](PLAYTEST.md)*

Guía para probar FourSwordsRecomp con 2 a 4 personas en un mismo PC. Lo que
ya se ha comprobado sin personas, y lo que no, está en
[DESIGN.es.md](DESIGN.es.md), secciones 10 y 11.

## 1. Antes de empezar

### Qué ejecutable

- `build\FourSwordsRecomp.exe`, compilado desde la etiqueta `playtest-rc1`
  de la rama `widescreen`.
- Ábrelo siempre con **`tools\play.cmd`** (doble clic): se abre el launcher.
  El ejecutable necesita las DLL de MSYS2 MinGW64 (SDL2 y la biblioteca de
  C++), que no están en el PATH de Windows. Un doble clic directo en el
  `.exe` falla, y otras copias de esas DLL (las de Git, por ejemplo) no son
  compatibles.
- Para comprobar la versión, en una consola `cmd` dentro de la carpeta del
  proyecto: `tools\play.cmd --version`. Debe mostrar el commit de la
  etiqueta y **no** debe decir `(with local changes)`. Anota lo que muestra.

### Protege tus partidas

1. Cierra el juego.
2. Copia `saves\four_swords_usa.sav` a otro sitio, por ejemplo
   `saves\four_swords_usa.antes-de-la-prueba.sav`.
3. Si quieres, crea en la pantalla de partidas un archivo nuevo solo para la
   prueba ("New file"). Hay 3 archivos; cada archivo lo juega como mucho una
   persona y el resto entra como invitado (Guest), que nunca se guarda.

Si algo sale mal con el guardado, vuelve a poner la copia en su sitio con el
juego cerrado.

### Configuración

- Launcher: **Screen = raw** (los colores exactos de la consola; las demás
  opciones simulan pantallas de GBA y cambian los colores a propósito).
- Pantalla completa: cualquiera de las dos opciones vale; en multijugador
  local siempre se usa la resolución del escritorio. También puedes jugar en
  ventana y cambiar su tamaño.
- Play → pantalla de partidas → **Multiplayer → Local**:
  - **Players**: 2, 3 o 4.
  - Por cada jugador, un archivo o **Guest**, un **mando** y, para uno
    solo, **Keyboard**.
  - **Whole-number scale**: desmarcado (la imagen ocupa más pantalla) o
    marcado (todos los píxeles del mismo tamaño, con bordes más grandes).
    Elige uno y no lo cambies durante la prueba.
  - **16:9 view (experimental)**: desmarcado para la primera parte.

## 2. Asignar e identificar cada mando

1. Conecta todos los mandos **antes** de abrir la página Local.
2. En la página Local, que cada persona **mantenga pulsado un botón** de su
   mando: junto a la fila de ese mando aparece **pressed**. Así se sabe qué
   entrada de la lista es el mando que cada uno tiene en la mano.
3. Asigna a cada jugador su mando. Elegir el mando de otro jugador
   intercambia los dos, así que un mando nunca queda en dos jugadores.
4. Un mando conectado que no se asigna a nadie no hace nada durante la
   partida, ni siquiera si se desconecta el de otro jugador.
5. Apunta qué persona es J1, J2, J3 y J4, con qué mando y qué archivo.

Correspondencias que deben cumplirse durante toda la partida:

| Jugador | Insignia en el lobby | Color de Link | Vista con 2 | Vista con 3 | Vista con 4 |
|---|---|---|---|---|---|
| J1 | P1 | verde | izquierda | arriba izquierda | arriba izquierda |
| J2 | P2 | rojo | derecha | arriba derecha | arriba derecha |
| J3 | P3 | azul | — | abajo, centrada | abajo izquierda |
| J4 | P4 | morado | — | — | abajo derecha |

En la pantalla de enlace ("PLEASE WAIT", con las insignias P1–P4), cada vista
muestra **YOU** sobre la insignia de su jugador. Dentro de la fase, cada vista
sigue a su propio Link.

## 3. Secuencia de pruebas

Primero todo en **3:2**, y después la comparación en **16:9**. Anota la hora
de inicio de cada prueba.

### A. Identidad y movimiento individual (3:2)
1. En el lobby, comprueba que cada vista dice **YOU** sobre la insignia
   correcta (tabla anterior).
2. Ya en el tutorial, mueve a un jugador cada vez mientras los demás no tocan
   nada: solo debe moverse el Link de su color, y su vista debe seguirlo.
3. Repite con el teclado si alguien lo usa.

### B. Tutorial
Jugad el tutorial ("Chambers of Insight") hasta el segundo piso. El paso al
segundo piso es donde ocurrió un cierre que ya está corregido: si vuelve a
pasar, detente (sección 5).

### C. Transiciones
Pasad varias veces de una zona a otra y dejad que un jugador caiga y reviva.
La pantalla se funde a negro y vuelve; ninguna vista debe quedarse congelada.

### D. Desconectar y reconectar
1. Desconecta el mando de un jugador. El juego se pausa y la barra de título
   dice "Player N: reconnect the controller".
2. Vuelve a conectarlo. El juego sigue y ese mismo mando vuelve a mover al
   mismo jugador.
3. Si hay dos mandos del mismo modelo, desconecta los dos y vuelve a
   conectarlos en orden inverso. Comprueba quién controla a quién y anótalo.
4. La tecla de pausa (por defecto **Mayús+P**, `config.ini` [KeyMap]) reanuda sin esperar al
   mando.

### E. Completar una fase
Terminad una fase entera, con su jefe. Anota qué fase y cuánto se tardó.

### F. Salir y volver a abrir (guardado)
1. Para volver al launcher: en la pantalla de enlace (P1–P4), cualquier
   jugador pulsa **B dos veces**. Cerrar la ventana también termina la
   sesión y guarda.
2. Vuelve a abrir el juego. En la pantalla de partidas, comprueba que cada
   archivo usado conserva su progreso y que los demás archivos siguen igual.

Es la prueba más importante del guardado: en las pruebas automáticas el
juego nunca llegó a escribir el guardado, así que esto no se ha visto nunca.

### G. Comparación en 16:9
Hazlo con **2 o 3 jugadores**: con 4, el 16:9 no llega a la velocidad completa
en el PC de desarrollo (el juego va a un 93 % y da tirones); con 4, quedaos en
3:2. Marca **16:9 view (experimental)** y repite A, B y C (y, si da tiempo, E)
con el mismo número de jugadores. Compara con 3:2:
- cada vista enseña más sala a los lados, sin estirarse;
- el HUD (corazones, rupias y objeto) va en los bordes, y se desplaza un
  poco hacia dentro mientras hay un cuadro de texto abierto (es lo esperado);
- durante las cargas, y en pantallas que no son una fase (lobby, menús,
  selección de fase, fuente de hadas), la imagen es 3:2 con bandas negras
  (también es lo esperado).

Si algo falla en 16:9, vuelve a 3:2 y sigue la prueba allí.

## 4. Qué anotar ante un problema

- Hora, número de jugadores y qué persona era cada jugador.
- Dispositivos: modelo de cada mando y quién tenía el teclado.
- Modo gráfico: 3:2 o 16:9, escala, ventana o pantalla completa.
- Escena: fase, piso, zona y qué estaba pasando en pantalla.
- Qué hicisteis justo antes: botones, desconexiones, pausas.
- Evidencias: una foto o captura de la pantalla y la carpeta de la sesión
  (sección 6).

## 5. Cuándo detener una prueba

Detente, sin volver a abrir el juego todavía, si:
- un jugador pierde el control o mueve a otro Link;
- el juego se congela, se cierra o muestra un error;
- un archivo desaparece, pierde progreso o aparece cambiado otro archivo;
- la imagen queda negra o congelada en alguna vista más de unos segundos.

Primero recoge las evidencias (sección 6). Si hay un problema de guardado, no
juegues más con ese archivo y conserva la copia del principio.

## 6. Recoger las evidencias

Cada sesión local guarda una carpeta propia que nunca se sobrescribe:

```
build\sessions\AAAA-MM-DD_HH-MM-SS\
    session.log      versión, commit, jugadores, mandos, escena cada 10 s,
                     rendimiento, guardados y, si lo hay, el error
    inputs.inputs    los botones de cada jugador en cada momento
    start.sav        el guardado tal como estaba al empezar la sesión
    config.ini       la configuración del launcher
    keybinds.ini     las teclas
```

Además, `build\fsr_local_last.log`, `.inputs` y `.sav` son copias de la
última sesión (la siguiente las sobrescribe).

Tras un cierre inesperado:
1. No abras otra sesión todavía.
2. Copia la carpeta más reciente de `build\sessions\` (o comprímela).
3. Si salió un cuadro de error, haz una captura.
4. Añade tus notas (sección 4).

Con `inputs.inputs` y una copia de `start.sav`, la sesión se puede repetir
exactamente sin personas (instrucciones para el desarrollador en
DESIGN.es.md §10).

## 7. Limitaciones conocidas

- Solo se oye la GBA de J1.
- Rendimiento medido en el PC de desarrollo: 2 y 3 jugadores a velocidad
  completa en 3:2 y en 16:9; 4 jugadores a velocidad completa en 3:2, con
  algún frame tardío (puede notarse un pequeño tirón de vez en cuando); 4
  jugadores en 16:9, no. El registro de cada sesión anota el tiempo por
  frame cada 10 s.
- Los colores lavados y el titileo de sprites que se notaron antes no se han
  reproducido ni comparado con una referencia; si los veis, anotad la escena
  y el ajuste Screen.
- Un cierre que tuvieron unos probadores con cuatro jugadores no se ha
  reproducido y no está corregido. Si ocurre, las evidencias de la sección 6
  son lo que falta para encontrarlo.
- La vista 16:9 solo se ha comprobado con entradas automáticas; ningún
  combate contra un jefe se ha visto en 16:9.
