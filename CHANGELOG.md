# Changelog

## Unreleased

- **Fast text** (options menu, on by default): holding X advances the texts, outside battles and card games. After
  markisha64's Dmw2003FastText.
- **Fast forward:** Tab or R3 cycles x2, x3, x5 and off, with ">> xN" in a corner. The music and the movies keep
  their speed.

## v0.9.0 (first public release, beta)

Digimon World 2003 (Europe, SLES-03936) as a native Windows program, built from the
[dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp) C code with the console's hardware rewritten for the PC. A beta
until a full playthrough has been checked: see **Not checked yet** below.

**Setup**

- No Python, no tools: the first run opens a window that asks for your disc image (drop the `.cue`/`.bin` on it, or
  browse), checks it is the European game and reads it into `data/` in a few seconds. The window speaks English,
  Spanish, French, German and Italian.
- Nothing of the game's content in the download: the exe reads the game's own tables from your `data/` at startup
  (`dw2003.fill` says where).
- A formatted memory card on the first run, `saves/card1.mcd`; cards from emulators work.

**Graphics**

- OpenGL 3.3, the 3D drawn 1 to 8 times finer than the PlayStation's 320x240 with its exact points (no wobble), the
  battle models through a depth buffer (no flicker at high resolutions).
- The window sized to your screen (1280x960 on a 1080p one) and the 3D as fine, up to 4x, until you choose in the
  options menu.
- Fullscreen (F11 or Alt+Enter), the PlayStation's dithering on or off, the movies smoothed or sharp.

**Sound and movies**

- The sound chip rewritten: music, effects and reverb from your disc's own presets; the movies with their XA audio.

**Controls**

- Keyboard and gamepad (Xbox pads and any SDL knows): analog sticks with a round deadzone, vibration, plugged in or
  swapped while playing.

**Options**

- Esc (gamepad: Back) opens the options menu, saved to `native.ini`: 50 or 60 Hz (the game's own 60 Hz mode),
  resolution, sub-pixel 3D, depth buffer, dithering, smooth movies, fullscreen, window size, vibration, stick
  deadzone.

**For modders and testers**

- Mods as plain files: `data/mods/NNNN.bin` replaces a disc file, of any size; the
  [field editor](https://github.com/Feerdus95/dw3-field-editor) writes its changes there and in `data/mods/stages/`.
- `--field`, `--at`, `--party`, `--event`, `--progress`, `--set` start straight in a field for tests; `debug = 1` in
  `native.ini` turns on the developers' stage select (F1), the sound and vibration tests (F3, F4) and savestates
  (F5 / F9).
- `--extract IMAGE` reads a disc without the window.

**Not checked yet**

- A full story playthrough, battles in depth (DV, signature, DNA), the card game, and sound and music everywhere.
  Reports are welcome: see the issue templates.
- An `.iso` with 2048-byte sectors plays the movies without sound; a `.cue`/`.bin` is best.
