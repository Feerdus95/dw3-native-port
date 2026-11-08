# Changelog

## Unreleased

- Cancel on B shows On when either the swapped keys or the swapped gamepad buttons are in place, so a custom binding on the other half no longer leaves it showing Off.

## v0.18.9 (beta)

- The launcher moves with the arrows where it makes sense. On the home screen, Left and Right walk the bar (Play, Settings, Saves, Tools, language) and Up and Down move between it and the buttons under it, where Left and Right swap Play and Quit. In Settings, Up from the first row reaches the tabs, and Left and Right switch them.
- **Menu cursor wraps** (Options, Input; off by default). Up from the first row goes to the last, and down from the last to the first, in the field menu, the item lists and shops, and the techniques list (who uses it, which, and whom it heals). The other menus keep stopping at their ends. Also in the launcher's Input tab. Thanks to Hucaru (#14).
- **Cancel on B**: a row in Controls (options menu, Input tab; and the launcher's Input tab) that swaps Triangle and Circle, keys and gamepad buttons, so the game's cancel is on B. It is a remap: the icons in the game's texts follow it, and pressing it again undoes it. The row, before the list of buttons, shows On or Off. Thanks to Hucaru (#14).
- The launcher's settings list scrolls with the wheel by itself (three rows a notch) and the selection goes to the row under the pointer, instead of the selection jumping row by row and fighting the pointer; its scroll bar is wider, leaves the values clear and can be dragged with the mouse, its thumb lighting under the pointer and when held.
- **Menus in fast forward** (Options, Input; off by default). With fast forward on, a menu's cursor repeats at its normal pace when a direction is held, instead of at the game's speed. Also in the launcher's Input tab. Thanks to Hucaru (#19).
- **Skip PRESS START** (Options, Input; off by default). The title screen shows New game and Continue as soon as its intro ends, without waiting for Start. Also in the launcher's Input tab. Thanks to Hucaru (#17).
- **Heal on level up** (Options, Mods; off by default). A partner that levels up in a battle gets its HP and MP back in full; its Digimon's levels do not heal. Also in the launcher's Mods tab. Thanks to Hucaru (#18).
- **Inactive window** (Options, Display; off by default). While the game's window is not in front, nothing happens as before, the sound is muted, or the game is paused until the window is back. Also in the launcher's Display tab. Thanks to Hucaru (#16).

## v0.18.8 (beta)

- **Script mods** (`data/mods/scripts/<fighter>/<script>.json`): a fighter's battle script (its attack, hit, KO and evade scripts) as a list of commands that replaces the game's, e.g. `{"schema_version": 1, "commands": [{"op": "sound", "sound": "punch"}, {"op": "end"}]}`. A sound command can play a sound of the library, one of the game's (1 to 89), the hit's sound, or the game's own on a result 3 hit. A file that is wrong is rejected with the reason and the game's script runs; `--check-mods` lists them. Options > Mods > Custom scripts switches them off in game (also `native.ini` and the launcher's Mods tab). The model viewer (v0.18.5) edits the scripts in its Fighter tab and saves these files.

## v0.18.7 (beta)

- **A sound library** (`data/mods/sounds/sounds.json`): WAVs of that folder, each with a name and a volume, and a `replace` table that says which of the game's battle sounds (1 to 89) each one replaces, e.g. `{"sounds": {"punch": {"file": "punch.wav", "volume": 1}}, "replace": {"57": "punch"}}`. A replaced sound plays the WAV wherever the game plays it (hits, steps, falls, effects' casts), to its end. Options > Mods > Custom sounds switches it off in game, and `--render-sounds` never replaces, so the game's own stay available. WAVs load when they first play, or a frame at a time at start-up for the ones that replace a sound. The model viewer (v0.18.4) has a Sounds tab to import, name and assign them.

## v0.18.6 (beta)

- **An effect's sound replaces the game's hit sound** (v0.18.5 mixed it with the game's). With Custom sounds on and a sound in the effect's folder, only that sound plays; with it off, or no sound in the folder, the game's own plays. A hit that is a result 3 always keeps the game's. WAVs load the first time they are needed, not at start-up, so nothing stalls a hit.
- `dw2003.exe --render-sounds DIR [list]` plays the game's battle sounds with its own sound code and writes each as `DIR/<index>.wav`, with no window or audio device. The launcher does it for all of them into `data/sounds` after reading the disc, and once at start-up on an install that doesn't have them (or whose `data/sounds/version.txt` is older), in the background. A render that fails is not retried at every start. The model viewer (v0.18.3) uses them for its Original sound buttons.

## v0.18.5 (beta)

- **Custom effects and Custom sounds** (Options, Mods; on by default). Custom effects plays the battle effects' own images (`data/mods/effects/`) instead of the game's; off plays the game's own. An `effect.json` can also give the folder a sound (`{"sound": {"file": "hit.wav", "volume": 1}}`: a WAV of up to 10 s, any rate or channels, volume 0 to 4) that plays when the effect starts, mixed with the game's own; a folder with only a sound needs no PNGs. Both switch in game and show at the next effect. They are also in `native.ini` and in the launcher's Mods tab.
- The launcher's Tools tab shows an **Update** button, with the new version, to the right of Open on the model viewer's and the field editor's rows when a newer release of that tool is out, instead of one row for both. Left and right move between the two buttons; the mouse picks by where it clicks.

## v0.18.4 (beta)

- **Save anywhere** (Options, Mods; off by default). The field menu gets a SAVE row that opens the game's memory card screen from wherever you stand; loading that save puts you back at the same spot. The slot shows the area and the place's name. Cancelling returns to the field. A save made this way can show a wrong place name in the original game or an emulator. Thanks to Hucaru (#2).
- **Quit** in the field menu asks "Do you wish to quit the game?" with TITLE MENU, DESKTOP and CANCEL (shown in the game's five languages), and the options menu (Display tab) has a Quit game row. Thanks to Hucaru (#1).
- The launcher's Mods tab has the Full EXP Share and Save anywhere rows.
- Fixed: the launcher's "Your team" showed the slot with the most play time instead of the last one saved.

## v0.18.3 (beta)

- **Full EXP Share** (Options, Mods; off by default). Every party partner still standing at the end of a battle gets the full EXP, even if it did not fight, and its selected or used digivolutions the full Digimon EXP; knocked-out partners get neither. Thanks to Zaw Htet Naing (#13, #12).
- For mod authors: `dw2003.exe --check-mods` lists the Digimon mods in `data/mods/digimon` (loaded, disabled, rejected or skipped, with the reason and any warning) and exits with the number rejected. It prints to standard output, so redirect it to a file (`dw2003.exe --check-mods > mods.txt`).
- If the game cannot allocate memory at start-up it now says so in a message box and exits, instead of crashing.
- For builders: `build.py` stops when `dw3_decomp` is not at the commit the port was made against, or has uncommitted changes (`--allow-decomp-mismatch` overrides it); it also fails on a new unimplemented PsyQ function (`tools/stubs.baseline`), records what built it in `build/report.md`, and the repository's tests run on GitHub Actions (`tools/run_tests.py`).

## v0.18.2 (beta)

- **A Digimon mod's `like` is checked.** It must be a partner of the game's fighters' file (259, ExVeemon's, by default); a mod that points at one that isn't there is rejected and the log says so, where before it loaded and its record was silently left out.
- The effects' and the stage props' JSON files (`effect.json`, `stages.json`, `props.json`) are read by the same strict reader as the manifests: a name twice, a malformed number, text after the value or a file over 4 MB is now reported in the log, with its line, instead of being let by.
- Fixed: looking up a fighter in the fighters' file could read outside it when the file's list started at a word near 4 GB.

## v0.18.1 (beta)

- Fixed: the launcher's updates (the port's and the tools') never unpacked: the path of Windows' tar had a tab in it, so tar never started. Versions before this one still can't update from the launcher; unpack this release by hand once and they can.

## v0.18.0 (beta)

- **Digimon mods are checked.** Each `digimon.json` is read by a stricter JSON reader (`platform/json.c`: limits on size and depth, every
  allocation checked, real escapes and UTF-8, JSON's number grammar, a name twice in an object refused, `true`/`false`, nothing after the value) and validated: a wrong type, a number out of its
  field's range, fewer than six techniques, a model that isn't there, isn't a model file or has fewer entries than `subs` asks for, `pair.scripts` past its scripts, a name or help that isn't plain ASCII, a missing English help, or a `fid` that is a file of the
  game's disc rejects the mod, and the log says why. Folders are read in the order of their names, and mods that clash on id, fusion pair or
  effect id are all left out. `schema_version`, `author` and `version` are accepted; `enabled` takes `true`/`false`.
- Fixed: a manifest with fewer than six techniques read outside the techniques table; a `template` out of range and a Digimon's HP/MP above 255 or at 0
  are now refused or held in the byte they live in.
- Fixed: the stage mods' files were read without checking the file's size or a failed read.
- New tests: `tools/test_json.c`, `tools/test_mods.c`; `test_effect_stages` now runs on the real JSON reader.

## v0.17.8 (beta)

- **The launcher updates the port.** When a newer version is out, the "New version" chip downloads it, installs it over the old files and
  restarts the launcher; settings, saves and data stay. It asks to close the game first and never stops it.

## v0.17.7 (beta)

- **Digimon mods are folders now.** `data/mods/digimon/<name>/digimon.json` adds a Digimon partners can learn (stats, techniques with
  their texts, what it is learnt with, an optional fusion), with no change to the port; the README lists the fields. Flamedramon is the first:
  `dw2-extractor/flame_dcb.py` writes its manifest beside its model. BladeGarurumon stays built in, made from the game's own files.
- Changed: to turn a Digimon mod off, set `"enabled": 0` in its manifest (or move its folder); `flamedramon = 0` in `native.ini` no longer
  does anything. Flamedramon has no switch in the options menu or the launcher.
- Fixed: the DNA fusion into Flamedramon left the end of its attack's sound looping until the next map or fight; it now stops with the attack.

## v0.17.6 (beta)

- Changed: the Mods tab no longer shows the Flamedramon row. Its model can't be shipped, so players found a switch they couldn't turn on.
  If you make `data/mods/7120.bin` yourself the game still offers it; `flamedramon = 0` in `native.ini` turns it off.

## v0.17.5 (beta)

- Fixed: Check for updates could say it couldn't reach GitHub when GitHub's API was refusing the request. The launcher now reads
  the public releases feed instead, so it no longer uses the API at all.

## v0.17.4 (beta)

- Fixed: downloading or updating the model viewer or the field editor could fail at once at 0%, depending on the connection.
  The launcher no longer needs GitHub's API for them, which limits how often an address can ask.
- If a request still fails, `launcher.log` now says why (its HTTP status or WinHTTP error).

## v0.17.3 (beta)

- **Launcher: tool updates.** It now also checks for new versions of the model viewer and the field editor, at start and with
  Check for updates. If one is out, Tools shows an **Update tool** row; your settings and the tool's data stay. Close the tool
  first. Tools the launcher downloaded before this version are offered the latest release once, as it can't tell their version.

## v0.17.2 (beta)

- Fixed: 3D lines, like the wireframes of a digivolution, were wide and blocky at higher render scales. They are now thin and
  follow the model smoothly; 2D lines in menus stay as they were.

## v0.17.1 (beta)

- Fixed three crashes, reported by players and fixed by j0an2:
  - walking to an exit before the partners are called (a balloon drawn without its actor);
  - opening the Weapons list of the item menu (the category of no item);
  - calling Digmon or Submarimon (the field's saved position read from the player's missing actor).

## v0.17.0 (beta)

- **Extra Digimon belong to the save they were made with.** If a save slot is written by the original game, an emulator or a
  card from elsewhere after the port's, the port no longer attaches the old forms to the new save: it leaves them out and keeps
  a copy of the card as `saves/card1.pre-stale.mcd` before its next save. A save that has extra Digimon still loads in the
  original game, but without them.
- **Launcher** (`dw3launcher.exe`): the disc's first reading moves here from the game, with Play, Settings (every option
  with its description, the controls' keys and gamepad buttons, the mods with their credits), a notice of new versions
  and Tools.
- **Saves page** in the launcher: what each slot of the memory card holds and whether its extra Digimon still belong to the
  save, **Export card** (a backup, or to play on another computer), **Import card** (a PlayStation or emulator `.mcd` works)
  and **Restore last copy**. Every import or restore keeps the card you had in `saves/backups` first.
- The launcher has a language picker with flags at the end of its top bar, and a DW3 icon (also the icon of `dw3launcher.exe`
  and `dw2003.exe`).
- **Tools**: the launcher downloads the model viewer and the field editor from GitHub, sets them up to read the port's
  data and opens them; one already beside the port is used as it is.
- Fixed: the game couldn't start from a folder with accents or other non-English letters in its path (Windows 10 1903
  or later).
- Reading the disc also writes `data/names.txt`, the disc's file names, which the model viewer reads.
- **Effects by Digimon level**: a mod can have images for Rookies, Champions, Ultimates and Megas
  (`data/mods/effects/<id>/champion/` and so on), each folder with its own timing, and `stages.json` says which partner is
  what. A level with no images plays the nearest lower level's, then the base mod, then the original. The model viewer's
  Effects tab makes them: pick the level and drop the PNGs on the window.
- `"active": 0` in an `effect.json` leaves that folder out.
- Fixed: critical hit images could show on an item's or a missed attack's effect, after an earlier critical hit, and a
  double-magic technique only counted its last hit's critical.

## v0.16.0 (beta)

- **Flamedramon as a DNA fusion in the Tag menu**, like Paildramon: a partner with ExVeemon tags a partner with Greymon
  (either way round) and the pair fuses into Flamedramon, using the game's own DNA animation (dark stage, entrance, the
  form's attack, the white flash back). The attack is one of its three signatures, chosen at random, played inside that
  same stage. The fusion costs 100 MP each and is fire. It needs the same `data/mods/7120.bin` as Flamedramon itself.
- **More than one pair per Digimon**: the game's data holds a single Tag partner per form; the port now keeps a table of
  extra pairs, so a form can fuse with several.

## v0.15.1 (beta)

- Fixed: Flamedramon's eyes were half shut in its normal pose. They stay half shut only in the weak pose (a quarter of its HP or less).

## v0.15.0 (beta)

- **Flamedramon's signatures** are Digital Card Battle's three fire attacks, with the animation, camera and effects of that
  game: Fire Rocket (60 MP, power 300), Knuckle Fire (84 MP, 480) and Express Claw (four hits of 150, 110 MP), learnt at
  levels 50, 70 and 85 and shown as signature techniques with their own descriptions. It now needs ExVeemon and Greymon at
  level 40, and its model is `data/mods/7120.bin` (made by dw2-extractor).
- **Textured fire that moves**: Digital Card Battle's effect models scroll their texture as the original did, in their own
  VRAM page. Only those effects (452-458) do; every effect of the game is as it was.
- **Flamedramon** (options menu, on while its model is in `data/mods/7120.bin`): a 46th Digimon of its own, from Digimon
  World 2. A quick fire attacker (strength and speed high, fire-immune, weak to water) with six techniques of its own
  (Ember Jab to Express Claw); learnt with ExVeemon and Greymon. The port
  now holds a list of extra forms rather than one. `--give 472` gives it to every partner on a load (a development aid:
  it goes into the card on the next save, so use a copy).
- **BladeGarurumon's progress is kept in the save file's own spare space.** The memory card file has 2 KB at its end that
  the game never reads or writes; each save slot now has an area there for the forms the port adds (BladeGarurumon, and
  the ones to come), with a checksum and a version. The game's own bytes are unchanged, so a card is still a standard one.
- **Saves made before this are carried over**: loading one reads BladeGarurumon from where it was kept before, and the
  next save writes it into its area. It is also still written where it was, so such a save still loads in the previous
  version with its progress. The first time a save without an area is read or saved, the card is copied once to
  `saves/card1.pre-dw3x.mcd`.
- The memory card is written to a temporary file and renamed over `card1.mcd`, so a crash while saving can't leave a
  half-written card.

## v0.14.0 (beta)

- **Faster drawing**: the game's own 1x picture (the PlayStation's VRAM, which the game reads back for some textures)
  is no longer drawn in software for every primitive. Primitives are queued and drawn only when something reads or
  writes that part of the VRAM, and only those the read depends on; a frame the next clear covers is dropped. In battle
  the CPU's drawing went from 7 ms a frame to 1.6 ms, and the late frames of a fight from about 12 to 2-7.
- What still has to be drawn in software is 2.7 times faster, pixel for pixel the same.
- Screens with no full clear (the title, loading screens, the step from the EXP screen back to the field) draw what is
  queued three frames later, a little each frame, instead of all at once when the queue fills.
- The first fight's props load faster: a stage's OBJ models are read in 2 ms instead of 26, so the fight no longer
  starts with a 78 ms frame.
- The performance meter's CSV notes why the queued VRAM was drawn on each frame it was.

## v0.13.0 (beta)

- **Props in the fight stages** (`data/mods/fightstages/<id>/props.json`, written by the model viewer's Stages tab):
  billboards (grass, flowers, rocks), falling-particle emitters (petals, snow, embers), the stage's own falling leaves
  and small 3D models (an OBJ with a PNG), placed anywhere in the stage. Up to 100000 billboards, 64 emitters of 5000
  particles, 5000 leaves and 64 models. They are hidden by the stage and by the fighters where those are nearer, bend in
  the stage's wind, and take its fog (or a fog of their own).
- **The grass is trodden down under a fighter**: a blade near a fighter's surface is flattened, so the feet no longer
  clip through it.
- All of it is drawn on the card from tables uploaded once: billboards, particles, leaves and 3D models (a mesh kept
  there, one instanced draw per mesh), so the props cost next to nothing: twelve trees of 6000 triangles take 0.07 ms
  of the CPU, where they took 4 ms.
- A stage's props list is baked into a cache (`data/cache/props_<id>.bin`) the first time it is read, and is read again
  only when the JSON changes. A fight keeps its props through a signature move's own stage.
- **Effects' own pictures no longer hitch** the first frames of a signature move: each effect folder of `mods/effects`
  is read when the game starts and its pictures are put on the card one a frame.
- **Performance meter** (F10, or `perf = 1` in `native.ini`): a corner graph while a fight runs, frames stacked by
  game, drawing and swap against the vsync budget, with late frames marked and the card's times for the scene and the
  props; every frame goes to `perf/battle_<time>.csv`.

## v0.12.0 (beta)

- Fight stages edited in the model viewer (v0.5.0 on: its Stages tab, parts moved, removed or taken from other stages,
  OBJs from Blender) play in battle: saved in the game's own format to `data/mods/`, read instead of the disc's.
- Widescreen battles: the whole interface keeps its shape. The HP panels each sit against their side, with a gap
  between them; the partner's picture, its frame and the gauge against the right edge; the messages in the middle.
- **Battle sky and fog** (options menu, Graphics tab, off by default): the battle's flat background becomes a sky,
  darker toward the top and the stage's own colour at the horizon, and the stage's far side fades into it with
  distance (the stages' painted horizon kept clear), so the arena's edge no longer ends against a flat colour. Each
  stage takes its kind's look from `stages.ini` by the exe: open air, night, rooms (fog toward black), water;
  digital, space and the techniques' backdrops are left as they are. Leaves fall through the forests and parks, carried
  by a gusting wind, among the fighters; the forests' trees sway in it and the clouds of East Wire Forest and Central
  Park drift. The game's own picture (1x) stays as it was.

## v0.11.0 (beta)

- Fast text waits for X to be held a third of a second: a tap shows the page whole, as in the original, and the next
  tap goes on, instead of the tap also skipping the page it finished.
- **Controls** (options menu, Input tab): each PlayStation button's two keys and gamepad button, set by pressing them,
  saved in `native.ini` (`keys_cross = Z, Space`, `pad_cross = a`).
- **Button icons** (options menu, Input tab): the menu's help line, tab ends and Controls page show the icons chosen
  there: PS1 (the game's own in its texts), PS3, PS4, PS5 (the default), Xbox 360, Xbox One, Xbox Series, Switch,
  Keyboard, or Auto by the device used last; from Xelu's free prompts pack (CC0), in `prompts/` by the exe.
- The options menu is drawn four times finer, its text and icons sharp at any window size.
- The game's own texts show the face buttons (✕ ○ □ △, as in "△ Button: Back") with the same icons: the key or the
  pad's button each is bound to.

## v0.10.0 (beta)

- **Effects' own images**: a battle's 2D effect drawn from PNGs in `data/mods/effects/<id>/` (the model viewer's
  export format, at any size, with alpha), and a critical hit's from `critical/` there, which the original doesn't
  tell apart. Drawn by the graphics card in the effect's place. The folder's `effect.json`, which the model viewer
  writes, gives each frame's ticks (1/60 s), an offset, a scale and the blend (alpha or added).
- **Digilog**, the journal (options menu, off by default): a last row of the field menu, its seven-row panel made of
  the game's own tiles. It lists the story's steps so far in the item list's panels and pages, the current one first:
  each with its place (the field banner's own name), whether it is done, and what to do, in the five languages; X
  shows its area on the map and comes back. Left / right turn two tabs: Active (the story's step, highlighted, and the
  side quests found, from places visited: DDNA bosses, old weapons, dungeons, Black Kings, errands) and Done.
- **Objective on map** (options menu, off by default): the map screen blinks the area where the story's next step
  happens, read from the stages' own story events.
- **Fast travel** (options menu, off by default): on the map screen, X on a visited area asks whether to travel there;
  on Yes the home mark moves to it and the menu, as it closes, takes you there. Once you have been to Amaterasu,
  Square switches between the two servers' maps, to travel from one to the other. You arrive through one of the game's own doors into the area that is open at that
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
