# The launcher

The launcher starts single-player battles in the game, from a local web page
or from the Discord bot, and reports how each one ended. This page covers
the commands that pick the game build it flies.

## Keeping the game current

The game and the program that flies the computer craft (the AI program) are
built together and ship in one package, so they always match. Install the
newest build that passed CI with:

```sh
jedimaster update
```

- It needs the GitHub command line tool, `gh`, installed and logged in
  (`gh auth login`). Without it the command says so and stops.
- It downloads the package for this computer, unpacks it, checks that the
  game says it is the commit it was built from and that its AI version equals
  its AI program's, and only then makes it the current build.
- `jedimaster update --commit SHA` installs the build of one commit instead of
  the newest.
- `jedimaster update --list` shows the installed builds, the current one
  marked with `*`. `jedimaster update --use SHA` makes an installed build
  current again, so going back is one command. The three newest builds are
  kept, and the current one and the one it replaced are never removed.
- Nothing updates by itself: the launcher changes build only when you run
  this command.
- Updating on macOS is not built yet. Builds are kept in
  `~/.local/share/jedimaster/builds` on Linux,
  `~/Library/Application Support/jedimaster/builds` on macOS and
  `%LOCALAPPDATA%\jedimaster\builds` on Windows; `--builds DIR` names another
  folder.

After `jedimaster update`, the bot's command needs neither `--engine` nor
`--ai-program`:

```sh
jedimaster page --bot
```

The current build is looked up again at the start of every battle, so a
running bot flies a newly installed build at its next battle, with no
restart. `--engine PROGRAM` and `--ai-program FOLDER` still name a game
program or AI program by hand; without `--ai-program` the AI program packaged
beside the game program is used.

## When a battle is refused

Before every battle, the launcher asks the game and its AI program which AI
version each speaks (`--version`), and refuses the battle if they differ, in
words, instead of letting it start and end at once. The same check runs once
when `jedimaster page` starts, as a warning only: the bot keeps running, so
`/status` and `/link` still work.

| The launcher says | It means | What to do |
| --- | --- | --- |
| This game build speaks AI version A and its AI program speaks AI version B, so computer craft could not fly. | The game and its AI program come from different commits. | Run `jedimaster update`. |
| This game build is too old to say which AI version it speaks. | The game is an older build that does not answer `--version`. | Run `jedimaster update`. |
| This game build could not be asked which AI version it speaks: REASON. | The game could not be started, or did not answer within the time limit. | Check the game file, or run `jedimaster update`. |
| No AI program was found beside this game build, so computer craft could not fly. | The game has no AI program packaged with it and none was named. | Run `jedimaster update`, or name one with `--ai-program`. |
| The AI program in FOLDER did not say which AI version it speaks: REASON. | The named AI program could not be run, or printed no version. | Check the folder, or run `jedimaster update`. |

`jedimaster launch` makes the same check and, when it refuses, exits with
status 1 without starting the game.
