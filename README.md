# Digimon World 2003: native PC port

The PlayStation game (Europe, SLES-03936) running natively on Windows, built from the
[dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp) C code. It is not an emulator. It is free; donations are optional.

**Bring your own disc.** Nothing from the game ships with the port. You need your own copy of
*Digimon World 2003 (Europe) (En,Fr,De,Es,It)*, as a `.bin`/`.cue` image.

## Setup

1. Install [Python 3](https://www.python.org), used once for the extraction.
2. Extract your disc next to the game:

       python tools/extract_iso.py "path/to/Digimon World 2003 (Europe) (En,Fr,De,Es,It).cue"

   The script checks that the image is the right version, then writes `data/` (the executable, the game's files and the
   movies).
3. Run `dw2003.exe`.

Saves go to `saves/card1.mcd`, a standard memory card image. A card from an emulator (DuckStation, ePSXe, etc.) works
if you copy it there.

## Controls

| Keyboard | PlayStation |
|---|---|
| Arrows | D-pad |
| Z / Space | Cross |
| X | Circle |
| A | Square |
| S | Triangle |
| Q / W | L1 / R1 |
| E / R | L2 / R2 |
| Enter | Start |
| Backspace | Select |
| F11 or Alt+Enter | Fullscreen |

## Options

Optional: a `native.ini` next to `dw2003.exe` (see `native.ini.example`):

    data = data          ; where the extracted game is (relative to the exe)
    fullscreen = 0       ; 1 to start in fullscreen
    scale = 3            ; window size in 320x240 steps
    dither = 1           ; 0 turns off the PlayStation's dithering
    smooth_movies = 1    ; 0 shows the movies with sharp pixels
    debug = 0            ; 1: F1 debug stage select, F3 sound test, F4 vibration test, F2 dump of the game state,
                         ;    F5 / F9 save / load a state (saves/quick.state)

---

## En español

Port nativo para PC de Digimon World 2003 (Europa, SLES-03936), compilado desde el C de dw3_decomp. No es un
emulador. Es gratis; las donaciones son opcionales.

**Necesitás tu propio disco.** El port no trae nada del juego.

1. Instalá Python 3 (solo para extraer).
2. `python tools/extract_iso.py "ruta/a/Digimon World 2003 (Europe) (En,Fr,De,Es,It).cue"`
3. Ejecutá `dw2003.exe`.

Las partidas se guardan en `saves/card1.mcd`, una imagen de memory card estándar: sirven las de emuladores.
F11 o Alt+Enter: pantalla completa. Controles y opciones: arriba.
