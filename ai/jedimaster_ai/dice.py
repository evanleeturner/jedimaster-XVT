"""Each craft's own random numbers, from a state its notebook keeps.

Purpose:
    Random choices that every machine of a network game makes alike, and
    that a replay after a rewind makes again: the state lives in the craft's
    notebook, so the numbers follow from the question alone.

Invariants:
    - Integers only. A state is 1 to 2**32 - 1, and ``step`` never leaves
      that range: the generator is Marsaglia's xorshift32 (shifts 13, 17, 5),
      whose states other than 0 form one cycle.
    - The same state gives the same numbers on every machine and Python.
"""

from __future__ import annotations

MASK = 0xFFFFFFFF
# An odd multiplier, so distinct seeds stay distinct states (the golden ratio
# times 2**32, a common choice for spreading bits).
SPREAD = 0x9E3779B1
# The state of random seed -2 with object index 0xFFFF, which seed -1 with
# that index shares, since its own product is the forbidden 0.
SHARED = (0xFFFF0000 * SPREAD) & MASK


def seed(random_seed: int, object_index: int) -> int:
    """Return a craft's first state from the engine's seed and its index.

    Distinct pairs give distinct states, but for one: 2**32 pairs have
    2**32 - 1 states to go to, so seeds -1 and -2 share one at object index
    0xFFFF.
    """
    pair = (random_seed & 0xFFFF) << 16 | (object_index & 0xFFFF)
    return ((pair + 1) * SPREAD) & MASK or SHARED


def step(state: int) -> int:
    """Return the state after this one."""
    state ^= (state << 13) & MASK
    state ^= state >> 17
    state ^= (state << 5) & MASK
    return state


def roll(state: int, sides: int) -> tuple[int, int]:
    """Return a number from 0 to sides - 1, and the state after this one."""
    if sides < 1:
        raise ValueError(f"a roll needs 1 side or more, not {sides!r}")
    state = step(state)
    return state % sides, state
