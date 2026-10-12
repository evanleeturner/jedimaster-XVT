"""The AI program: the separate program that decides what computer craft do.

The engine starts it as a child process and asks it about each craft due to
think, in the bytes ``engine/src/xvt_runtime/ai/ai_wire.h`` sets out. For now
``__main__`` is a stand-in that answers every question with no action.
"""
