# Sound library and script mods (design draft)

Status: phases 1 (sound library) and 2 (script mods in the port) are implemented; 3 and 4 are not. Where the port differs from this draft:
ids of library sounds are the order of `sounds.json` (not by name, nothing stores them), fighter ids in the folder are not checked against
the game's table, a command's numbers are the words the game stores, and `camera` takes `aim` for the target (tx, ty, tz) as well.
Today a sound belongs to an effect folder
(`effects/<id>/effect.json` `"sound"`); that keeps working.

## Goal

- Sounds are assets of their own, with a name, not a property of an effect.
- Mods can **replace** any of the game's 87 battle sounds and **add** new ones.
- A Digimon's action scripts can be edited and chosen per Digimon, so "Agumon's normal attack uses my sound"
  is possible without touching Gabumon's.
- A mod that is wrong is rejected at load, with the reason in the log, and the game plays the original.

## How the game plays a sound (facts, from `dw3_decomp`)

- A script command `10, sound, time` plays battle sound `sound` (an index of `FIGHTSTG_battleSounds`, 0 to 0x59).
  `0x62`/`0x63` mean "the hit's sound": the technique's `scriptSound`, or 0x38 if the hit is a result 3.
- Scripts are arrays of 16-bit words run in order. There are no jumps. Commands are 1 hit, 2 model, 3 sprite
  effect, 4 stage, 5 camera, 6 effect model, 7 flash, 8/9 nothing, 10 sound, 11 wait, 0/0xFF end.
- A script is loaded in one place (`battle_script.c`, state 0: `script->pc = getArchiveEntry(...)`).

## Sound library

Folder `data/mods/sounds/`:

```
sounds.json
punch.wav
agumon_hit.wav
```

```json
{
  "schema_version": 1,
  "sounds": {
    "punch":      { "file": "punch.wav", "volume": 1 },
    "agumon_hit": { "file": "agumon_hit.wav", "volume": 1.5 }
  },
  "replace": { "57": "punch" }
}
```

- A name is 1 to 32 characters of `a-z 0-9 _ -`. `file` is a plain file name in the folder (no `/`, `\` or `..`), a WAV
  as now (any rate or channels, up to 10 s). `volume` is 0 to 4.
- `replace` maps an original sound index (1 to 0x59) to a library name: the game plays that WAV wherever it would have
  played the original, including the hit sound of every technique that uses it.
- Ids for scripts: the port gives each library name an id from 0x100 up (in name order) when it scans. Scripts store
  **names**, never ids, so adding or removing a sound doesn't change a script.
- Loading stays lazy (decoded the first time it plays), and **Custom sounds** (Options > Mods) switches everything
  in this section off live: replaced and added sounds play the game's own again, or nothing for a new one.

## Script mods

File `data/mods/scripts/<fighter id>/<script index>.json`: the whole script of one Digimon (the id is the fighter's id,
what the viewer's Fighter tab shows; the index is its position in the Digimon's action scripts).

```json
{
  "schema_version": 1,
  "commands": [
    { "op": "effect", "mode": "load", "effect": 2 },
    { "op": "model", "who": "own", "do": "motion", "n": 3 },
    { "op": "sound", "sound": "punch", "time": 0, "hit": true },
    { "op": "effect", "mode": "play", "effect": 2, "at": [0, -384, 5120] },
    { "op": "wait", "ticks": 10 },
    { "op": "end" }
  ]
}
```

Commands (the same ones the viewer already lists as text):

| op | fields |
|----|--------|
| `hit` | `when`: `"last"` or `"next"` |
| `model` | `who`: `own`, `partner1-3`, `enemy1-3`; `do`: `wait_motion`, `idle`, `motion` (`n`), `show`, `hide`, `move` (`to`, `time`), `turn` (`to`), `jump` (`n`, `distance` when n is 4), `wait_jump`, `add_fighter` (`fighter`), `home` (`time`), `home_offset` (`time`), `remove` |
| `effect` | `mode`: `load` or `play`; `effect`: a number or `"own"`; `at`: `[x, y, z]` for play |
| `stage` | `mode`: `wait` or `change`; `stage`; `fade_out`, `fade_in` |
| `camera` | `time`; either `shot` (1 to 7, 8 is the enemy's view) or `at`, `look`, `turn`, `rz`, `projection` |
| `effect_model` | `mode`: `wait` or `play`; `model`; `at`, `turn` for play |
| `flash` | `mode`: `start` or `end`; `time` |
| `sound` | `sound`: a library name, an original index, or `"hit"`/`"hit_last"` (the technique's hit sound); `time`; `hit` (see below) |
| `wait` | `ticks` |
| `end` | |
| `raw` | `words`: the command's 16-bit words as the game reads them; for anything above that has no name. Checked by the same length rules |

**Hit-aware sounds.** `"sound": "punch", "hit": true` plays `punch`, except that a hit of result 3 plays the game's
own 0x38 sound, as `"hit"` does today. In the compiled script this is the sound's id with bit 0x4000 set;
`FIGHTSTG_runScriptSound` checks it before playing. Without `"hit": true` the sound always plays.

### What the port does with it

1. At start, scan `data/mods/scripts/`, validate every file, and list it in `--check-mods`
   (loaded, rejected with the reason, skipped).
2. When a script starts (the one place that loads it), look up `(fighter id, index)`. If a valid mod exists and
   **Custom scripts** is on, compile it to 16-bit words once, keep them, and point `script->pc` at them; else the
   game's own script runs.
3. Everything resolves at compile time: sound names to ids, `"own"` effects stay as the game's 9999.

### Validation (the file is rejected, not partly used)

- Known `op` and fields of the right type and range; the compiled length matches the command's.
- Ends with `end`. At most 500 commands and 4096 words. A `wait` is at most 600 ticks; a `move`/`fade` time at most 600.
- Sounds: a library name that exists, or an index 1 to 0x59. Effects: 0 to 9999 or `"own"`. Fighter ids that exist.
- The same strict JSON reader as the other manifests (size, depth, no duplicate names, nothing after the value).
- A script that references a sound the library lacks is rejected, so it can't silently go quiet.

## Options and switches

- `custom_sounds` (exists): the library, `replace` and sound ids in scripts. Off: replaced sounds play the originals,
  new ones play nothing, effect-folder sounds play nothing.
- `custom_scripts` (new, default 1): script mods. Off: the game's own scripts run. Read each time a script starts,
  so it switches live in the Mods tab like the others.
- Precedence for a hit sound: a sound in the script > the effect's own sound (`effect.json`) > `replace` > the game's.

## Model viewer

- **Sounds tab**: the library (import, play, volume, rename, delete) and the 87 originals with Play and "used by"
  (the techniques). Each original has "Replace with..." and "Clear". "New sound" imports a WAV under a name.
- **Effects tab**: its "Hit sound" row becomes a picker over the library (or none), writing `effect.json` as today.
- **Fighter tab, scripts**: the script list the viewer already decodes becomes editable. "Customize this script"
  copies the game's script into a mod file; then edit a command's fields, insert, delete, reorder. Play runs it in the
  viewer's script player. Validation messages are the port's.
- The viewer writes only into `data/mods/`; the game's own data is never touched.

## Phases

1. Sound library and `replace` in the port, Sounds tab in the viewer (no scripts).
2. Script mods in the port: compiler, validation, `--check-mods`, `custom_scripts`, hit-aware sounds.
3. Script editing in the viewer, starting with changing a sound command, then the other fields, then insert/delete.
4. Digimon mods (`digimon.json`) can ship their scripts.

Each phase is testable on its own; 1 changes nothing for anyone who adds no files.

## Open questions

- Is a script file shared by several Digimon in the game's archives? If so the key stays the fighter id, so a mod for
  one never changes another. To check against the data before phase 2.
- Do we want per-technique sounds as well (the same script playing different sounds by technique)? Not needed if
  scripts are per Digimon; left out.
- Names: should the library be allowed subfolders (`sounds/agumon/hit.wav`)? Flat for now.
- Enemy scripts: the same files serve enemies (their fighter ids), no separate case.
