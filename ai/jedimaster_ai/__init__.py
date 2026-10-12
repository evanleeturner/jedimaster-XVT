"""The AI program: the separate program that decides what computer craft do.

The engine starts it as a child process and asks it about each craft due to
think, in the bytes ``engine/src/xvt_runtime/ai/ai_wire.h`` sets out.

| Module | Holds |
| --- | --- |
| ``program`` | the run: options, log lines, the read-think-answer loop |
| ``wire`` | the messages' bytes, both directions |
| ``world`` | what the messages mean: craft views, actions, answers |
| ``think`` | one question's answer, craft by craft |
| ``orders`` | the mission order codes and the behavior that flies each |
| ``behaviors`` | the behaviors; ``drift`` flies every order for now |
| ``notebook`` | the memory a craft's record keeps for the program |
| ``dice`` | each craft's random numbers, from its notebook |
| ``logline`` | the log line format; ``events.json`` holds each event's text |
"""
