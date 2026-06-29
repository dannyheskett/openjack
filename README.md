# openjack

Blackjack (21) written in C with Raylib: classic, familiar controls, rules,
layout, and visuals. Play the house, place a bet, and try to grow your bankroll.

The window is freely **resizable**, but the **cards are a fixed size** (the same
vector card art as openklondike).

## Building

### Linux/WSL2

```bash
./scripts/build_raylib_linux.sh
make
make run
```

### Release Build

```bash
make release
make run-release
```

### Windows Cross-Compile

Requires mingw-w64:

```bash
./scripts/build_raylib_windows.sh
make windows
```

Produces `build/openjack-x64.exe` and `build/openjack-x86.exe`.

### macOS

```bash
./scripts/build_raylib_mac.sh
make mac  # -> build/openjack-mac (universal arm64 + x86_64)
```

## Tests

```bash
make test
```

## Rules

- Get closer to 21 than the dealer without going over.
- **Hit**, **stand**, or **double down** (one card on a doubled bet).
- The dealer draws to 17 and stands on all 17s.
- **Blackjack pays 3:2.** A tie is a push (your bet comes back).
- No insurance, no split.
- You bet from a bankroll; if you go broke you get a fresh stake.

## Playing

Set your wager, then **Deal**. Use the on-screen buttons or the keyboard:

| Key | Action |
|-----|--------|
| H | Hit |
| S | Stand |
| D | Double down |
| Left / Right (or - / +) | Lower / raise the bet |
| Enter / Space | Deal, or start the next hand |
| Escape | Menu |
| Alt+Enter | Toggle fullscreen |

## Recording

Toggle **Record: On/Off** from the menu to capture your session to an H.264 MP4
(`openjack-YYYYMMDD-HHMMSS.mp4`). No external tools required. The capture is
supersampled so the video is as crisp as the live game.

## License

MIT. See [LICENSE](LICENSE).
