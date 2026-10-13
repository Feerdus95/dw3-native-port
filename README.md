# Digimon World 2003: native PC port

The PlayStation game (Europe, SLES-03936) running natively on Windows, built from the
[dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp) C code. It is not an emulator. It is free; donations are optional.

**Bring your own disc.** Nothing from the game ships with the port. You need your own copy of
*Digimon World 2003 (Europe) (En,Fr,De,Es,It)*, as a `.bin`/`.cue` image.

## Setup

1. Unzip the port anywhere you can write (not `Program Files`).
2. Run `dw2003.exe`. The first time, it asks for your disc image (`.cue`/`.bin`, dropped on the window or browsed; an
   `.iso` may cause issues), checks that it is the right version and reads it into
   `data/` next to the game (about 850 MB, a few seconds). From then on it starts straight away.

The game's own tables (stats, scripts, file positions) aren't in the exe either: it reads them from `data/` at
startup, so keep `dw2003.fill` next to `dw2003.exe`. If `data/` gets damaged, delete it and the game asks again.

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
| Esc (gamepad: Back) | Options menu: frame rate, dithering, window, vibration... (saved to `native.ini`) |

A gamepad works too (Xbox 360 or any SDL knows: A cross, B circle, X square, Y triangle, triggers L2 / R2, sticks
analog), with vibration. It can be plugged in or swapped while playing.

## Options

Optional: a `native.ini` next to `dw2003.exe` (see `native.ini.example`):

    data = data          ; where the game read from your disc is (relative to the exe)
    fullscreen = 0       ; 1 to start in fullscreen
    scale = 3            ; window size in 320x240 steps
    dither = 1           ; 0 turns off the PlayStation's dithering
    smooth_movies = 1    ; 0 shows the movies with sharp pixels
    deadzone = 3277      ; how far the sticks and triggers move before they count (0-32767)
    rumble = 1           ; 0: no vibration
    render_scale = 1     ; 1-8: drawn on the graphics card (OpenGL 3.3) that many times finer, 3D without wobble
    fps = 50             ; 60: the game's own 60 Hz mode (the walking speed follows at the next area)
    debug = 0            ; 1: F1 debug stage select, F3 sound test, F4 vibration test, F2 dump of the game state,
                         ;    F5 / F9 save / load a state (saves/quick.state)

---

## En español

Port nativo para PC de Digimon World 2003 (Europa, SLES-03936), compilado desde el C de dw3_decomp. No es un
emulador. Es gratis; las donaciones son opcionales.

**Necesitás tu propio disco.** El port no trae nada del juego.

1. Descomprimí el port donde puedas escribir (no en `Program Files`).
2. Ejecutá `dw2003.exe`. La primera vez te pide la imagen de tu disco (`.cue`/`.bin`, arrastrada a la ventana o
   buscada; una `.iso` puede dar problemas), comprueba la versión y la lee a `data/`
   (unos 850 MB, pocos segundos). Después arranca directo. Dejá `dw2003.fill` junto al `.exe`.

Las partidas se guardan en `saves/card1.mcd`, una imagen de memory card estándar: sirven las de emuladores.
F11 o Alt+Enter: pantalla completa. Controles y opciones: arriba.
