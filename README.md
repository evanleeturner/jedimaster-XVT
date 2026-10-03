# jedimaster-XVT
Opensource XVT client distro build on openxvt to play the classic XvT game in discord.

## The engine

`engine/` holds the game engine: [OpenXvT](https://github.com/elyosh/OpenXvT)
by elyosh, an open-source reimplementation of *Star Wars: X-Wing vs. TIE
Fighter* and *Balance of Power*, and [Aeron](https://github.com/elyosh/Aeron),
the platform library it runs on. Both came here with their full history and
have been modified in this project since October 2026.

## License

The engine under `engine/` is GPLv3, the license OpenXvT and Aeron came with;
see `LICENSE`. Everything outside `engine/` is this project's own work: code
is MIT and documents are CC BY 4.0. `REUSE.toml` names the license of every
file. The two parts meet only across a process boundary: this project's
programs start the engine and talk to it, and none of them links it.
`tools/license_boundary.py` checks that rule on every change.

The game data is not included: you need your own copy of *X-Wing vs. TIE
Fighter*.
