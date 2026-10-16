# Where the engine came from

This page answers three questions about jedimaster-XVT: which of its code
came from other projects, which license that code is under, and where to
report a problem.

## The code that came from OpenXvT and Aeron

The game engine in `engine/` began as a copy of two open-source projects
by Adrien Moulin (elyosh on GitHub):

- [OpenXvT](https://github.com/elyosh/OpenXvT), a reimplementation of
  *Star Wars: X-Wing vs. TIE Fighter* and its expansion *Balance of
  Power*. The copy includes every OpenXvT change up to commit `51dfbfb`,
  dated 1 October 2026.
- [Aeron](https://github.com/elyosh/Aeron), the platform library OpenXvT
  runs on, in `engine/aeron/`. The copy includes every Aeron change up to
  commit `af5370d`, dated 29 September 2026.

Both arrived in this repository on 2 October 2026 with their full history,
so `git log` shows each original commit and its author. Every change after
those two commits was made in this project.

Aeron carries other open-source libraries in `engine/aeron/third_party/`.
Each keeps its own license, and its license file sits in its folder.

## A separate project from that date

jedimaster-XVT is developed on its own. It takes no new code from OpenXvT
or Aeron, and it sends them no changes, pull requests or bug reports. The
engine here has changed since the copy and will keep changing, so in time
its code and its behavior may differ a great deal from OpenXvT's.

## The license stays with the code

OpenXvT and Aeron are released under the GNU General Public License,
version 3 (GPLv3), and so is the engine here. That holds however much the
code changes: the copied code and every change made to it stay under
GPLv3, and the engine stays under GPLv3 for as long as any code from
OpenXvT or Aeron remains in it. The full license text is in `LICENSE`.

The rest of this repository, outside `engine/`, is this project's own
work: its code is MIT and its documents are CC BY 4.0. `REUSE.toml` names
the license of every file.

## Reporting a problem

If something goes wrong with this package, please open an issue in
[this repository](https://github.com/evanleeturner/jedimaster-XVT/issues).
Please report it here, not to OpenXvT or Aeron: their code and this
engine have parted, so they may not have the problem, and they did not
write the changes made here.

## No warranty

This software is provided "as is", without warranty of any kind, to the
extent the law allows. Sections 15 and 16 of the GPLv3 in `LICENSE` give
the exact terms; this page explains them and does not replace them.

## Thanks

OpenXvT and Aeron are the work of Adrien Moulin and their contributors.
This project builds on that work, and the credit for it is theirs.
