# Cannonball (OutRun) para Game & Watch — homebrew de Retro-Go SD

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

En el menú de pausa, Opciones → Marchas: manual o automática.

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
  - modo attract, selección de música, carrera, choque, menú de pausa, opciones, salir al menú,
    apagado, mensaje sin ROMs y recuperación de una copia en flash obsoleta.
- Sin probar en la consola. El benchmark previo, en la consola a 280 MHz, dio unos 19 ms por frame
  de media sobre un presupuesto de 33 ms.

## Licencia

Cannonball es de Chris White y se distribuye bajo su licencia (`external/cannonball/license.txt`):
uso no comercial. OutRun es de SEGA; las ROMs no se incluyen.
