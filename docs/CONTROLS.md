# Controls

All game inputs can be remapped in **Controller Settings** (two keyboard keys
and one controller button/axis per input), with presets, dead zone and
"restore defaults". Controllers are hot-plugged. Menu input is never passed
to the game: while a menu is open the game is paused, and buttons held when it
closes are ignored until released.

## Default keyboard

| Input | Player 1 | Player 2 |
|---|---|---|
| Up / Down / Left / Right | Arrows (or W/S/A/D) | I / , / U / O |
| Shot (Button 1) | Z (or J) | Keypad 1 |
| Bomb (Button 2) | X (or K) | Keypad 2 |
| Start | Enter (or 1) | 2 |
| Coin | 5 | 6 |

Cabinet: Service coin = `9`, Service = `0`, Tilt = `T`, Test / Service Mode = `F2`.

Frontend hotkeys (remappable under Controller Settings > Hotkeys): `Esc` quick
menu, `F5` quick save, `F9` quick load, `F6`/`F7` previous/next slot,
`F12` screenshot, `F11` fullscreen, `Backspace` (hold) rewind, `Pause` or `P`
pause, `F10` FPS counter, `F3` reset.

## Default controller (Xbox / PlayStation / Steam Deck / arcade stick)

Each player uses the controller assigned under Controller Settings > Player
Controllers (automatic: 1st controller = P1, 2nd = P2).

| Input | Button |
|---|---|
| Move | D-pad or left stick |
| Shot / Bomb | A / B (Cross / Circle) |
| Start | Start / Menu (Options) |
| Coin | Back / View (Share) |
| Quick menu | Back + Start held together (configurable) |

The game uses two buttons (MAME's `cyvern` input definition has no third
button). Button 1 firing was confirmed in scripted runs; button 2 is labelled
Bomb from the game's design and was not specifically confirmed in testing.

## Service / test mode

Hold `F2` (the board's test switch) to enter the SKNS test menu, where
coinage, difficulty and other settings are changed. The game keeps them in the
8 KB battery-backed RAM, which the frontend stores between sessions
(`nvram/` in the data folder).

## DIP switches (Gameplay Settings)

Only the switches MAME documents for the SKNS are offered:

| Switch | Values | Default |
|---|---|---|
| Flip Screen | Off / On | Off (verified pixel-identical to MAME with On) |
| Use Backup RAM | Yes / No | Yes |
| Freeze | Off / On (freezes the game, a factory switch) | Off |

Changes apply when the game is restarted.
