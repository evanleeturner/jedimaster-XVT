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

## After a battle

When a battle is flown, the launcher keeps its results: each player's score,
kills and losses, and your own kills by craft type, accuracy and losses. It
keeps the newest 20, in a `results` folder beside its settings file. The
page's Debriefing menu shows each one as the game's mission overview and
player statistics, and `/results` in Discord lists them. The battle summary
and tournament standings pages, and awards and promotions, are not shown yet.

When the page has room for the game's menu frame (a window at least 640 by 480
pixels, without forced colors), opening the Debriefing shows the debriefing
screen as the game drew it, in the frame: the background the game picked for a
win, a loss or a tie on your side, the page tabs at their old places, the
numbers in the game's own font, the Leave button and your side's badge, all
read out of your own install. The words sit on a dark, see-through panel, so
the game's art still shows behind them, and colored words such as ranks sit
on a darker patch of their own, so every word stays easy to read. The
Mission Selection screen is out of the way
until you leave. The tables stay in the page, out of sight, so a screen reader
reads every number. With no frame, or in a small window, the page shows the
plain tables as before, and a picture the install lacks is left out.

## The side buttons on the page

When the page has room for the game's menu frame (a window at least 640 by
480 pixels, without forced colors), the eight small buttons down the left edge
of the Mission Selection screen work as they did in the game when flying
alone. Training, Melee, Tournament, Combat engagement, Battle and Campaign
pick the kind of mission: a first press shows the last mission you saw of that
kind, or its first open one; pressing the lit kind again shows the next open
mission, and a right click or Shift with the press shows the previous one,
wrapping round at either end. For Training, Combat engagement and Campaign the
press walks that side's own game list, every mission in it, including ones the
network menu does not list or marks as not open. Rebel pilot and Imperial pilot choose the side:
Training, Combat engagement and Campaign then show that side's list, and the
frame's background changes to match. The buttons light up as the game's did,
from pictures read out of your own install. With no frame (a small window, or
no game pictures) the buttons are not on the page and the Missions menu does
the same job. The page remembers the last mission of each list only until it
is reloaded.

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
