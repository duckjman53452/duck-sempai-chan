# Input Buffer

Forgives inputs that come a few physics ticks too early.

**Cube, ball, UFO, robot, spider:** if you tap and let go before a jump is possible (still in the air, not yet on the orb), the release is held for a short window. If the game would have used the click during that window, it does.

**Ship, wave, swing:** every click and release is checked by simulating your path a few ticks ahead with the game's own physics. If sending it now would crash you almost immediately, it's held back to the nearest tick in the window where the path survives.

**Stages** (cycle with **F6** in a level, or from the pause menu):

- **Off**: vanilla input
- **Stage 1**: 1-2 ticks
- **Stage 2**: 4 ticks (3-6 configurable)
- **Stage 3**: 12 ticks (8-16 configurable), for testing and demos

While any stage is active an overlay shows the stage and every saved input. This is an assist mod: runs made with it aren't legitimate for leaderboards or demon list submissions.
