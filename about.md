# Input Buffer

Forgives inputs that come a few physics ticks too early.

**Shadow map:** when a level opens, a hidden shadow icon plays through it, finds a route that survives, and works out how early or late each of its clicks can be. The route is drawn into the level. While you follow it, early inputs are moved into those safe windows.

**Off the route:** ship, wave and swing inputs are checked with a short live look-ahead, and other modes hold an early release until the click is used.

**Seeing it work:** every moved input shows a ring where you pressed, a dot where the game got it, and a floating **+Nt**. The overlay has a map status line and live diagnostics.

**Stages** (F6 in a level, or the pause menu): Off, Stage 1 (1-2 ticks), Stage 2 (3-6), Stage 3 (8-16).

This is an assist mod: runs made with it aren't legitimate for leaderboards or demon list submissions.
