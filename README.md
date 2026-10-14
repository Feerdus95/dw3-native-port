# Digimon World 2003: native PC port

The PlayStation game (Europe, SLES-03936) running natively on Windows, built from the
[dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp) C code. It is not an emulator: the game's own code runs as a
PC program, with the console's hardware rewritten around it. It is free; donations are optional.

**Bring your own disc.** None of the game's content ships with the port: no graphics, sound, text or data tables. The
exe holds the game's logic as the decompiled C describes it, compiled anew; everything else comes from your own copy
of the game, read into a disc image.

## A native port, not a recompilation

There are two good ways to bring a PlayStation game to the PC, and both take a lot of work:

- **A static recompilation** translates the game's original MIPS machine code into C, instruction by instruction, and
  runs it next to an emulation of the console's hardware (GPU, sound chip, CD drive). It needs no understanding of
  the code, so it reaches the whole game quickly and plays it 1:1 from the start. It is the faster, safer road, and
  the projects that take it do real, valuable work.
- **A native port**, this one, starts from a *decompilation*: the game rewritten as readable, named C
  ([dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp), someone else's work, without which this port wouldn't
  exist). That C is compiled by a modern compiler, and the console's hardware isn't emulated: the drawing,
  sound, files, pad and memory card are rewritten for the PC (OpenGL, SDL).

What the second road gives:

- **The game can be changed as source code**, not only as patched bytes: tables can grow, fields can be added (the
  [field editor](https://github.com/Feerdus95/dw3-field-editor) makes new ones), and fixes are written in C.
- **Files by name, of any size.** The game reads `data/files/NNNN.bin`, not sectors of a disc, so a mod's file can be
  bigger than the original.
- **Drawing made for the PC.** The 3D is drawn at up to 8 times the resolution with its exact points, and the battle
  models through a depth buffer, instead of the console's pixels scaled up.
- **Room to grow.** Nothing in it is tied to the MIPS processor, so other systems are within reach, and nothing of the
  game's content is in the exe.

The price is that every difference from the console has to be found and fixed by hand, which is why the port is
tested by playing it through.

## Requirements

- Windows 10 or 11.
- A graphics card with OpenGL 3.3 (any from the last decade).
- About 900 MB free: the game read from your disc goes next to the port.
- Your disc of *Digimon World 2003 (Europe) (En,Fr,De,Es,It)*, as an image. A `.cue`/`.bin` pair is best; an `.iso`
  may cause issues (a plain 2048-byte one loses the movies' sound).

### The disc it was tested with

| | |
|---|---|
| Title | Digimon World 2003 (Europe) (En,Fr,De,Es,It) |
| Serial | SLES-03936 |
| Track | 1 data track, MODE2/2352 |
| `.bin` size | 692,146,560 bytes |
| `.bin` CRC32 | `007df18e` |
| `.bin` MD5 | `da76a5685b875c353343102b7587fded` |
| `.bin` SHA-1 | `457cb233349ba841e03b33d8060f8fbcadd45cb3` |
| `SLES_039.36` CRC32 | `3334ed7a` |
| `SLES_039.36` SHA-1 | `d1b7e4d646e3a9c2b88fdb25d20b5f7116bbb06d` |

The port checks the game's executable inside the image (`SLES_039.36`), not the image itself: another good dump of
the same disc works even if its `.bin` hashes differ. The American (*Digimon World 3*) and Japanese versions are
different games for the port and are refused.

## Setup

1. Unzip the port anywhere you can write (a folder of your own, not `Program Files`).
2. Run `dw2003.exe`. The first time, a window asks for your disc image: drop the `.cue` (or `.bin`) on it, or click
   **Browse...**. It checks the version and reads the disc into `data/` next to the game, in a few seconds. The window
   speaks English, Spanish, French, German and Italian (top right; it starts in your system's language).
3. From then on, `dw2003.exe` starts the game straight away. The image isn't needed any more.

The port's folder then holds:

| | |
|---|---|
| `dw2003.exe`, `SDL3.dll` | the port |
| `dw2003.fill` | where the game's own tables are in `data/`: the exe reads them from there at startup. Keep it next to the exe |
| `data/` | the game read from your disc. Yours: don't share it |
| `saves/card1.mcd` | your memory card |
| `native.ini` | your options, written by the options menu |

Saves go to `saves/card1.mcd`, a standard memory card image, created formatted on the first run. A card from an emulator (DuckStation, ePSXe...) works if
you copy it there.

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
| Tab (gamepad: R3) | Fast forward: x2, x3, x5, off |
| Esc (gamepad: Back) | Options menu |

A gamepad works too (an Xbox pad or any SDL knows: A cross, B circle, X square, Y triangle, triggers L2 / R2, analog
sticks), with vibration. It can be plugged in or swapped while playing.

## Options

**Esc** (Back on a gamepad) opens the options menu; the game pauses while it is open, and what you change is saved to
`native.ini`. Its tabs, Display, Graphics, Input and Mods, turn with **L1 / R1** (Q / E or Page Up / Down):

| | |
|---|---|
| Frame rate | 50 (the European original) or 60 (the game's own 60 Hz mode; walking speed follows at the next area) |
| Resolution | the 3D drawn 1 to 8 times finer than the PlayStation's 320x240, on the graphics card |
| Sub-pixel 3D | the 3D's points at their exact place at higher resolutions, without the PlayStation's wobble |
| Depth buffer | the battle models' polygons in the right order at higher resolutions |
| Widescreen battles | battles in 16:9, the 3D seeing a third wider; the menus keep their shape at the edges, the HP panels and messages stretch, the fields stay 4:3. The window turns 16:9. Off by default |
| Perspective 3D | the 3D's textures in perspective, without the PlayStation's warping (with Sub-pixel 3D) |
| Smooth 3D textures | the 3D models' textures smoothed when enlarged, not in blocks; sprites and text stay sharp (with Sub-pixel 3D) |
| True colour | the colours at 8 bits a channel, without the PlayStation's 5-bit banding (dithering then has no use) |
| Dithering | the PlayStation's dithering on shaded polygons |
| Soft shadows | the shadows under the characters on the field darken the ground, with smooth edges, instead of the original's grey disc. Off by default |
| Battle shadows | the Digimon in battle cast their shape on the ground, as the light from above falls (with Sub-pixel 3D and the Depth buffer) |
| Smooth movies | the movies smoothed when scaled up, or with sharp pixels |
| VSync | each frame shown whole, at the screen's refresh: no tearing |
| Fullscreen, Window size | the window fits the screen it is on, centred |
| Vibration, Stick deadzone | the gamepad |
| Fast text | holding X advances the texts, outside battles and card games |
| Fast travel | on the map screen (the field menu's Map), X on a visited area asks, and Yes takes you there as the menu closes, through one of the game's own doors into it that is open at that point of the story. Once you have been to Amaterasu, Square switches between the two servers' maps. Off by default. After Flawe's Fast Travel hack |
| Objective on map | the map screen blinks the area where the story's next step happens (visited or not); on the other server's map when the step is there. Off by default |
| Digilog | a last row of the field menu, DIGILOG: a journal in the item list's own panels, two tabs turned with left / right. Active: the story's current step and the side quests found (8 DDNA bosses, the old weapons, the dungeons' knights and masters, the Black Kings, the Undersea Base's two (while the story is there) and a few errands, each titled with its reward's name); Done: what is behind. Each with the place it happens at and what to do; L1 / R1 turn the pages, X shows the place on the map. Off by default |
| Fast forward | x2, x3 or x5 (Tab or R3 while playing); the music keeps its speed. Not saved |
| Extra starter packs | three more packs to pick in a new game: Dragon (Agumon, Guilmon, Veemon), Tactical (Kotemon, Monmon, Patamon), Mystic (Kumamon, Renamon, Veemon) |
| BladeGarurumon | a 45th Digimon every partner can learn, with Growlmon and Grizzmon at level 40: its own model (made at start from your disc's), techniques and signature, Solar Laser. Saves stay standard memory cards |

`native.ini` can also be written by hand (see `native.ini.example`); every line is optional:

    data = data          ; where the game read from your disc is (relative to the exe)
    fullscreen = 0       ; 1 to start in fullscreen
    scale = 4            ; window size in 320x240 steps (unset: the biggest that fits the screen)
    render_scale = 4     ; 1-8: the Resolution option (unset: as the window, up to 4)
    fps = 50             ; 60: the game's own 60 Hz mode
    widescreen = 0       ; 1: battles in 16:9 (the window too)
    perspective = 1      ; 0: the 3D's textures warp as on the PlayStation
    smooth_3d = 1        ; 0: the 3D's textures in sharp blocks
    true_color = 1       ; 0: the colours at the PlayStation's 5 bits a channel
    dither = 1           ; 0 turns off the PlayStation's dithering
    battle_shadows = 1   ; 0: no shadows in battle
    soft_shadows = 0     ; 1: the field's shadows see-through and smooth
    vsync = 1            ; 0: frames shown at once, which can tear
    smooth_movies = 1    ; 0 shows the movies with sharp pixels
    deadzone = 3277      ; how far the sticks and triggers move before they count (0-32767)
    rumble = 1           ; 0: no vibration
    fast_text = 1        ; 0: holding X doesn't advance the texts
    fast_travel = 0      ; 1: X on the map screen travels to the area
    map_objective = 0    ; 1: the map screen blinks the story's next area
    journal = 0          ; 1: the field menu's journal row
    extra_packs = 1      ; 0: only the game's three starter packs
    bladegarurumon = 1   ; 0: partners don't learn BladeGarurumon
                         ; F12 in game saves the picture to screenshots/ by the exe
    debug = 0            ; 1: F1 debug stage select, F3 sound test, F4 vibration test, F2 dump of the game state,
                         ;    F5 / F9 save / load a state (saves/quick.state)
    log =                ; console logs: cd, snd, pad, mc, ovl, gpu or all

### Effects' own images

A battle's 2D effect can be drawn from your own PNGs: put them in `data/mods/effects/<id>/`, played in their names'
order, one every other field (30 a second). Each is the 320x240 screen around the effect's point, as the model
viewer's Export PNGs saves an effect, or a bigger image of the same shape for finer detail; their alpha blends them
over the battle. A critical hit's go in `critical/` inside that folder (without it, criticals use the others). Effect
2 is the Fight command's hit. The ids are the model viewer's Effects tab's.

## Troubleshooting

- **"The game's data doesn't match this version of the port"**: `data/` was read with another version of the port, or
  got damaged. Delete `data/` and start the game: it asks for the disc again.
- **"Can't write to the data folder"**: the port is somewhere Windows protects (`Program Files`, a read-only drive).
  Move the folder somewhere of your own.
- **No sound in the movies**: the disc was read from a plain `.iso`. Delete `data/` and read it again from a
  `.cue`/`.bin`.
- **The window doesn't open**: the graphics card needs OpenGL 3.3; update its driver.
- **It crashed**: `crash.txt` next to `dw2003.exe` says where. Please open an issue with it (the form asks for what
  helps), and a memory card from just before if you can. Never attach the game's files.

For scripts: `dw2003.exe --extract "path/to/image.cue"` reads the disc without the window, and the `DW3_DATA`
environment variable points the port at a `data/` elsewhere.

## Building

For developers; players only need the release zip.

- [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (its `i686-w64-mingw32-clang`): set `LLVM_MINGW` to its folder,
  or put its `bin/` on the PATH.
- Python 3.10 or newer, and GNU `patch` on the PATH (Git for Windows has it).
- [dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp) cloned **next to** this repository, at the commit in
  `DECOMP_COMMIT` (`build.py`):

      some-folder/
        dw3_decomp/      git checkout <DECOMP_COMMIT>
        native/          this repository (any name)

- SDL 3.4.16's MinGW development files (`SDL3-devel-3.4.16-mingw.zip` from the SDL releases), unzipped into
  `third_party/SDL3-3.4.16/`.

Then:

    python build.py                      # build/dw2003.exe (incremental; --clean for all)
    build/dw2003.exe --extract "path/to/Digimon World 2003 (Europe) (En,Fr,De,Es,It).cue"
    python build.py                      # again: now it takes the game's tables out of the exe
    python tools/package.py              # dist/dw2003-native.zip, refused if any of the game is in it

The first build has no `data/` yet, so its exe keeps the game's tables and can read the disc into `data/`; every build
after that takes them out (`tools/asset_scan.py`) and writes `dw2003.fill`. `python tools/smoke.py` builds and boots the
game unattended for 20 seconds; `python build.py --sanitize` builds with UBSan into `build-san/`.

## Legal

© 2026 Feerdus. The port's own code is under the MIT License (`LICENSE`); what it builds on, and their licenses, is in
`CREDITS.md`.

A fan project, not affiliated with or endorsed by Bandai Namco. Digimon World 2003 is © Bandai / Bandai Namco; Digimon
is © Akiyoshi Hongo, Toei Animation. The port carries nothing of the game: everything comes from the disc you own.

---

## En español

Port nativo para PC de Digimon World 2003 (Europa, SLES-03936), compilado desde el C de
[dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp). No es un emulador: el código del juego corre como un programa de
PC. Es gratis; las donaciones son opcionales.

**Necesitas tu propio disco.** El port no trae contenido del juego: ni gráficos, ni sonido, ni textos, ni tablas. El
exe tiene la lógica del juego tal como la describe el C descompilado, compilada de nuevo; todo lo demás sale de tu
copia.

**Port nativo, no recompilación.** Una recompilación estática traduce el código máquina original del juego a C,
instrucción por instrucción, y lo corre junto a una emulación del hardware de la consola: llega antes a todo el juego y
lo reproduce 1:1, y es un trabajo valioso. Este port parte de una *descompilación* (dw3_decomp, trabajo ajeno sin el
que no existiría): el juego reescrito como C legible, compilado con un compilador moderno, y el hardware de la consola
reescrito para PC en vez de emulado. Eso permite cambiar el juego como código fuente (tablas que crecen, campos nuevos
con el editor), archivos de cualquier tamaño, 3D dibujado hasta 8 veces más fino y, a futuro, otros sistemas. El
precio: cada diferencia con la consola hay que encontrarla y corregirla a mano, por eso se prueba jugándolo entero.

**Requisitos:** Windows 10 u 11, una tarjeta gráfica con OpenGL 3.3, unos 900 MB libres y tu disco de *Digimon World
2003 (Europe) (En,Fr,De,Es,It)* como imagen: mejor `.cue`/`.bin`; una `.iso` puede dar problemas. El disco con el que se
probó y sus sumas (CRC32, MD5, SHA-1) están en la tabla de arriba. El port comprueba el ejecutable del juego
(`SLES_039.36`), no la imagen: otra copia buena del mismo disco sirve. Las versiones americana y japonesa no.

1. Descomprime el port en una carpeta tuya donde se pueda escribir (no en `Program Files`).
2. Ejecuta `dw2003.exe`. La primera vez, una ventana te pide la imagen: arrastra el `.cue` (o el `.bin`) o pulsa
   **Buscar...**. Comprueba la versión y lee el disco a `data/`, en pocos segundos. La ventana está en español, inglés,
   francés, alemán e italiano (arriba a la derecha).
3. Desde entonces, `dw2003.exe` arranca el juego directamente. Deja `dw2003.fill` junto al `.exe`: el port lee de
   `data/` las tablas del juego al arrancar.

Las partidas se guardan en `saves/card1.mcd`, una imagen de memory card estándar (se crea formateada la primera vez):
sirven las de emuladores.
**Esc** abre el menú de opciones (se guarda en `native.ini`); F11 o Alt+Enter, pantalla completa. Controles y
opciones: arriba.

Si sale **"The game's data doesn't match..."**, borra `data/` y arranca el juego: vuelve a pedir el disco.

© 2026 Feerdus, licencia MIT. Proyecto de fans, sin relación con Bandai Namco. Digimon World 2003 es © Bandai /
Bandai Namco.
