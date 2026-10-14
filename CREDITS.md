# Credits

The native port of Digimon World 2003 builds on the work of others:

- **[dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp)** (MIT License, Copyright (c) 2026 juandav, ReGame-Labs). It is the
  decompiled C of the game that this port compiles for the PC, with our patches in `native/patches/`. Without it there
  would be no port.
- **[Dmw2003FastText](https://github.com/markisha64/Dmw2003FastText)** (MIT License, by markisha64). The idea of
  Fast text: the text boxes reading X held as well as pressed, outside battles.
- **[SDL 3](https://libsdl.org)** (zlib License, Copyright (C) 1997-2026 Sam Lantinga). The window, input and audio.
  `SDL3.dll` ships as it comes.
- **[Xelu's free controller prompts](https://thoseawesomeguys.com/prompts/)** (CC0, by Nicolae "Xelu" Berbece, in
  the SVG edition by [haaldor](https://github.com/haaldor/Xelu_prompts_SVG)). The button and key icons of the options
  menu and of the game's texts, scaled down by `tools/prompts.py`, in `prompts/` by the exe with their license.
- **[psx-spx](https://psx-spx.consoledev.net)** (the PlayStation specifications, after Martin Korth's nocash docs).
  The hardware behaviour the platform layer reproduces, including two data tables of the chips: the SPU's Gaussian
  interpolation weights and its reverb resampling filter.

PlayStation is a trademark of Sony Interactive Entertainment, Xbox of Microsoft and Nintendo Switch of Nintendo; the
icons only show which button to press, and the project has no relation to them.

Digimon World 2003 is © Bandai / Bandai Namco. This project carries no game data: you supply your own disc
(see `README.md`). The game's own tables the decompiled C carries are taken out of the exe when it is built and
read at run time from your own copy, as are the reverb presets.
