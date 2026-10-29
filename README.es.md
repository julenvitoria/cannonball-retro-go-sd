# Cannonball (OutRun) para Game & Watch — homebrew de Retro-Go SD

[English](README.md) | **Español**

Port de [Cannonball](https://github.com/djyt/cannonball), el motor de OutRun de Chris White, como
homebrew GWHB para el firmware Retro-Go SD 2.x (rama `external_cores`).

- Juego original a 30 fps, como la recreativa. Sonido de los chips reales (SegaPCM + YM2151) a
  22 050 Hz.
- No incluye nada del juego: usa tus ROMs de OutRun rev. B y las convierte en la consola la
  primera vez.

## Instalación

En la SD:

    /homebrews/Cannonball.bin        <- este programa
    /homebrews/cannonball/           <- las ROMs de OutRun rev. B (el contenido de outrun.zip, descomprimido)

Las ROMs van directamente dentro de `/homebrews/cannonball/`, sin subcarpetas. El firmware oculta
esa carpeta en el menú, porque no contiene ningún homebrew.

En el menú, pestaña Homebrew → Cannonball.

La primera vez comprueba las ROMs, crea `/homebrews/cannonball/outrun.dat` (2,3 MB, con las ROMs
ya ordenadas y convertidas) y lo copia a la flash externa. Las siguientes veces arranca
directamente. Una vez creado `outrun.dat`, las ROMs ya no hacen falta.

Las ROMs se buscan por nombre y, si el nombre no coincide, por CRC (como hace Cannonball), así
que sirve el set de MAME `outrun` (rev. B). Si falta alguna o no es la revisión B, sale un
mensaje con el nombre del fichero.

Si alguna vez la flash externa se borra o se reescribe (por ejemplo con una actualización), el
programa lo detecta y vuelve a copiar `outrun.dat`. Si aun así algo falla, basta con borrar
`outrun.dat` y se regenera desde las ROMs.

## ROMs necesarias

Son los 31 ficheros de OutRun rev. B, el set `outrun` de MAME: los que salen sueltos al
descomprimir `outrun.zip`. Las subcarpetas del zip (`outrunb`, `outrunra`, `outrundx`, …) son
otras versiones y no hacen falta. Ojo: `outrunb` es una versión pirata de la época, no la rev. B.

En total ocupan 2.064.384 bytes (1,97 MB).

| Fichero | Tamaño | CRC32 | Contenido |
|---|---:|---|---|
| `epr-10380b.133` | 64 KB (65.536 B) | `1f6cadad` | CPU principal |
| `epr-10382b.118` | 64 KB (65.536 B) | `c4c3fa1a` | CPU principal |
| `epr-10381b.132` | 64 KB (65.536 B) | `be8c412b` | CPU principal |
| `epr-10383b.117` | 64 KB (65.536 B) | `10a2014a` | CPU principal |
| `epr-10327a.76` | 64 KB (65.536 B) | `e28a5baf` | CPU secundaria |
| `epr-10329a.58` | 64 KB (65.536 B) | `da131c81` | CPU secundaria |
| `epr-10328a.75` | 64 KB (65.536 B) | `d5ec5e5d` | CPU secundaria |
| `epr-10330a.57` | 64 KB (65.536 B) | `ba9ec82a` | CPU secundaria |
| `epr-10187.88` | 32 KB (32.768 B) | `a10abaa9` | Programa de sonido |
| `opr-10193.66` | 32 KB (32.768 B) | `bcd10dde` | Muestras de sonido |
| `opr-10192.67` | 32 KB (32.768 B) | `770f1270` | Muestras de sonido |
| `opr-10191.68` | 32 KB (32.768 B) | `20a284ab` | Muestras de sonido |
| `opr-10190.69` | 32 KB (32.768 B) | `7cab70e2` | Muestras de sonido |
| `opr-10189.70` | 32 KB (32.768 B) | `01366b54` | Muestras de sonido |
| `opr-10188.71` | 32 KB (32.768 B) | `bad30ad9` | Muestras de sonido |
| `opr-10185.11` | 32 KB (32.768 B) | `22794426` | Carretera |
| `opr-10186.47` | 32 KB (32.768 B) | `22794426` | Carretera |
| `opr-10268.99` | 32 KB (32.768 B) | `95344b04` | Fondos (tiles) |
| `opr-10232.102` | 32 KB (32.768 B) | `776ba1eb` | Fondos (tiles) |
| `opr-10267.100` | 32 KB (32.768 B) | `a85bb823` | Fondos (tiles) |
| `opr-10231.103` | 32 KB (32.768 B) | `8908bcbf` | Fondos (tiles) |
| `opr-10266.101` | 32 KB (32.768 B) | `9f6f1a74` | Fondos (tiles) |
| `opr-10230.104` | 32 KB (32.768 B) | `686f5e50` | Fondos (tiles) |
| `mpr-10371.9` | 128 KB (131.072 B) | `7cc86208` | Sprites |
| `mpr-10373.10` | 128 KB (131.072 B) | `b0d26ac9` | Sprites |
| `mpr-10375.11` | 128 KB (131.072 B) | `59b60bd7` | Sprites |
| `mpr-10377.12` | 128 KB (131.072 B) | `17a1b04a` | Sprites |
| `mpr-10372.13` | 128 KB (131.072 B) | `b557078c` | Sprites |
| `mpr-10374.14` | 128 KB (131.072 B) | `8051e517` | Sprites |
| `mpr-10376.15` | 128 KB (131.072 B) | `f3b8f318` | Sprites |
| `mpr-10378.16` | 128 KB (131.072 B) | `a1062984` | Sprites |

Los nombres pueden variar: si un fichero no coincide por nombre, se busca por su CRC.

## Controles

| Botón | Acción |
|---|---|
| Izquierda / derecha | Volante (y elegir canción / ruta) |
| A | Acelerar |
| B | Frenar |
| Arriba / abajo | Marcha larga / corta |
| GAME | Start |
| TIME | Moneda (no hace falta: está en *free play*) |
| PAUSE/SET | Menú de Retro-Go |

## Menú

El juego empieza en el menú de Cannonball (el logo de OutRun sobre la carretera):

- **JUGAR**: entra al juego (modo demostración; GAME empieza una carrera).
- **MODOS DE JUEGO**, como en Cannonball:
  - **MODO CONTINUO**: las 15 etapas una detrás de otra, con sus propios récords; **TRÁFICO**
    de 0 a 8 (3 por defecto). **EMPEZAR** y luego GAME en la pantalla de la música.
  - **CONTRARRELOJ**: una etapa contra el reloj; **VUELTAS** de 1 a 5, **TRÁFICO** de 0 a 8.
    **EMPEZAR** enseña el mapa: izquierda/derecha eligen la etapa, A o GAME empiezan, B vuelve. Se
    guarda la mejor vuelta de cada etapa.
- **AJUSTES**: izquierda/derecha o A cambian el valor, B vuelve (igual en las páginas de los modos).
  - **MARCHAS**: manual (arriba/abajo = larga/corta) o automático.
  - **TIEMPO**: fácil, normal, difícil, muy difícil o infinito.
  - **TRÁFICO**: fácil, normal, difícil, muy difícil o sin tráfico.
  - **DIRECCIÓN**: de 1 a 9, lo rápido que gira el coche mientras mantienes izquierda/derecha (la
    cruceta es digital y el volante gira poco a poco; 5 por defecto, el de Cannonball es 3; más alto responde antes).
- **SALIR**: vuelve a Retro-Go.

En el menú: arriba/abajo para moverse, A o GAME para elegir. Para volver al menú desde el juego:
PAUSE/SET → Opciones → **Menú de Cannonball**.

El menú sale en el idioma de Retro-Go si es inglés, español, portugués, francés, italiano, alemán o
noruego, sin tildes (la letra de OutRun no las tiene); en el resto, en inglés. Los ajustes los
guarda el firmware junto con los demás ajustes del homebrew, así que se conservan al reiniciar.

Las puntuaciones se guardan en `/homebrews/cannonball/hiscores*.bin`.

## Compilar

Con el Arm GNU Toolchain 15.2.rel1 (el mismo que usa retro-go-sd):

    make GCC_PATH=/ruta/a/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin

O con Docker: `make docker`. Sale `build/Cannonball.bin`.

`external/cannonball` es djyt/cannonball 27493eb con un parche mínimo que aplica
`tools/prepare_cannonball.py` (para regenerarlo: `tools/prepare_cannonball.py /ruta/a/cannonball`).
El parche solo cambia *dónde* viven los datos grandes; la lógica del juego, el render y el sonido
no se tocan.

## Cómo está hecho

| Qué | Dónde | Tamaño |
|---|---|---|
| Código + datos del motor | RAM_EMU | 130 KB de código, 400 KB de BSS |
| Sprites, tiles, ROMs de CPU, muestras PCM | Flash externa (`outrun.dat`) | 2,3 MB |
| Carretera decodificada | RAM_EMU | 257 KB |
| Buffer del frame (índices de paleta) | RAM_EMU | 140 KB |
| RAM de tiles y texto (`hwtiles`) | DTCM | 68 KB |

Queda unos 15 KB libres en RAM_EMU y 36 KB en DTCM. Esta disposición es la que salió mejor en el
benchmark en la consola (`cannonball_bench`): la carretera en RAM y los tiles en flash.

Ficheros del port (`src/gw/`):

- `main_cannonball.c`: lado del firmware (arranque, conversión, entrada, ritmo, audio, menú).
- `gw_engine.cpp`: lo que hacían `main.cpp` y la capa SDL de Cannonball.
- `romconv.cpp`: crea `outrun.dat` desde las ROMs.
- `roms_gw.cpp`, `config_gw.cpp`, `utils_gw.cpp`: sustituyen a los de Cannonball (sin XML, sin
  iostreams, ROMs leídas directamente de la flash).
- `cxx_runtime_gw.cpp`: la parte mínima de libstdc++ que usa el motor.
- `shim/`: cabeceras sustitutas (tipos de SDL, `iostream` vacío).

## Estado

- Probado en el emulador gwemu con el firmware `external_cores` (b791f15):
  - conversión de ROMs y copia a flash;
  - los datos convertidos son idénticos bit a bit a los que genera Cannonball;
  - menú propio (ajustes que se conservan al reiniciar, textos en español y alemán), volver al
    menú desde el menú de pausa, SALIR;
  - modo attract, selección de música, carrera, choque, menú de pausa, opciones, salir al menú,
    apagado, mensaje sin ROMs y recuperación de una copia en flash obsoleta.
- Probado también en la consola. El benchmark previo, en la consola a 280 MHz, dio unos 19 ms por
  frame de media sobre un presupuesto de 33 ms.

## Licencia

Cannonball es de Chris White y se distribuye bajo su licencia (`external/cannonball/license.txt`):
uso no comercial. OutRun es de SEGA; las ROMs no se incluyen.
