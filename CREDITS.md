# Credits

The native port of Digimon World 2003 builds on the work of others:

- **[dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp)** (MIT License, Copyright (c) 2026 juandav, ReGame-Labs). It is the
  decompiled C of the game that this port compiles for the PC, with our patches in `native/patches/`. Without it there
  would be no port.
- **[SDL 3](https://libsdl.org)** (zlib License, Copyright (C) 1997-2026 Sam Lantinga). The window, input and audio.
  `SDL3.dll` ships as it comes.
- **[psx-spx](https://psx-spx.consoledev.net)** (the PlayStation specifications, after Martin Korth's nocash docs).
  The hardware behaviour the platform layer reproduces, including two data tables of the chips: the SPU's Gaussian
  interpolation weights and its reverb resampling filter.

Digimon World 2003 is © Bandai / Bandai Namco. This project carries no game data: you supply your own disc
(see `README.md`). The game's own tables the decompiled C carries are taken out of the exe when it is built and
read at run time from your own copy, as are the reverb presets.
