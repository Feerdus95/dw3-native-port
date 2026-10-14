# Changelog

## Unreleased

- **Fast travel** (options menu, off by default): on the map screen, X on a visited area asks whether to travel there;
  on Yes the home mark moves to it and the menu, as it closes, takes you there. You arrive through one of the game's own doors into the area that is open at that
  point of the story (or at its start, for the two areas no door leads into). After Flawe's Fast Travel hack; no part
  of it is used.
- The options menu in tabs, Display, Graphics, Input and Mods, turned with L1 / R1 (Q / E or Page Up / Down on the
  keyboard) beside the battle menu's own page arrows, read from your disc.
- **F12** saves the game's picture, as drawn, to `screenshots/` by the exe.
- **VSync** (options menu, on by default): frames wait for the screen's refresh, which ends the tearing in
  fullscreen.
- Battle shadows: the partner's shadow while its commands are chosen (its picture beside the menus turned it off).
- The window, resized from the menu or by Widescreen battles, fits the screen it is on and is centred there.
- **Widescreen battles** (options menu, off by default): battles shown 16:9 with the 3D a third wider, the way
  emulators' widescreen hacks do it; the fields stay 4:3. The window turns 16:9 with it. The menus keep their shape,
  against the left or the right edge; the HP panels, the partner's picture and the messages stretch with the picture.
- **Battle shadows** (options menu, on by default): the Digimon in battle cast their own shape on the ground, along a
  light from above, with soft edges (blurred as they darken the ground once), hidden behind them. The original has none.
- **Perspective 3D** (options menu, on by default): the 3D's textures in perspective, by the depth the GTE gave each
  point, without the PlayStation's warping as the camera moves.
- **Smooth 3D textures** (options menu, on by default): the 3D models' textures filtered between their texels when
  enlarged, their see-through texels left out; sprites, text and the backgrounds stay sharp.
- **True colour** (options menu, on by default): the graphics card keeps the colours at 8 bits a channel, without the
  PlayStation's 5-bit banding in shading and blending.
- **Soft shadows** (options menu, off by default): the shadows under the characters on the field darken the ground
  they fall on, with smooth edges at any resolution, instead of the original's grey disc.
- **Fast text** (options menu, on by default): holding X advances the texts, outside battles and card games. After
  markisha64's Dmw2003FastText.
- **Fast forward:** Tab or R3 cycles x2, x3, x5 and off, with ">> xN" in a corner. The music and the movies keep
  their speed.
- **Extra starter packs** (options menu, on by default): Dragon (Agumon, Guilmon, Veemon), Tactical (Kotemon,
  Monmon, Patamon) and Mystic (Kumamon, Renamon, Veemon), on a second page of the choice, with their own texts in the
  five languages. The lobby shows the pack's partners, each with a greeting, and Veemon celebrates with the others.
- **BladeGarurumon** (options menu, on by default): a 45th Digimon every partner can learn, with Growlmon and Grizzmon
  at level 40. Its DNA model gets MetalGarurumon's animations, retargeted at start from your own disc, and a signature
  of its own, Solar Laser: a technique of its own (444), named in every language. It is kept in save bytes the game doesn't use: cards stay standard, and a PlayStation
  simply doesn't see it.

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
