# Known issues

- **Attract-mode phase:** from about frame 1,450 the attract sequence runs
  roughly one frame behind MAME 0.264 (see HARDWARE.md, "How it was verified").
  The cause is boot timing about 2 ms later than MAME's; MAME's own two SH-2
  cores differ from each other by a similar amount. Not visible in play.
- **Audio is not sample-identical to MAME** (envelope correlation 0.998, level
  within 0.5 %). The interpolation and mixing follow MAME's YMZ280B; remaining
  differences come from the timing above and from resampling to 48 kHz.
- **No game-value gadgets** (score, lives, bombs): addresses not yet verified.
- **Only the US set** (`cyvern`) is supported; the Japanese set is refused.
  Only the USA BIOS is used.
- **Not tested on real hardware or by a person:** Windows 10/11 PCs, the
  Steam Deck, physical controllers and arcade sticks, and actual play-throughs.
  Gameplay was exercised with scripted and random inputs (headless) through
  several stages.
- **Paddle inputs** of the SKNS are not mapped (Cyvern does not use them).
- **Watchdog:** the SH7604 watchdog runs in watchdog mode and is kicked by the
  game; an overflow (which would reset the board) is not modelled, as in MAME.
- **CRT curvature** is not offered (SDL_Renderer has no shaders).
