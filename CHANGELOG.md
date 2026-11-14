# Changelog

## 0.1.0 — 2026-11-13

First release.

- SH-2 static translator for the game program and the SKNS BIOS, with a
  fallback interpreter built from the same instruction semantics.
- Recompiled build (private, from your ROMs) and generic build
  (redistributable, interpreter only), bit-identical in output.
- Super Kaneko Nova System board: ROZ tilemaps with line/column scroll, RLE
  zooming sprites, palette brightness and additive sprites, YMZ280B, SH7604
  DIVU and free-running timer, RTC, hit unit, NVRAM.
- Verified against MAME 0.264: pixel-identical boot/attract frames up to frame
  1200 and with Flip Screen on; audio envelope correlation 0.998.
- Shared SDL2 frontend with vertical-monitor support (original orientation by
  default), save states, rewind, input movies, NVRAM persistence, remapping.
- Windows x64 zips and a Steam Deck / Linux AppImage.
