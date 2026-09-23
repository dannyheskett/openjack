# openjack

Blackjack (21) against the dealer, written in C. There is no betting: the game
counts the hands you win and lose. It runs natively on Windows, macOS, Linux, Android, and iOS, and in the browser via
WebAssembly.

Rendering, input, and audio go through raylib 6.0 on every platform except iOS,
which uses a native Metal backend with no raylib (see
[Architecture](#architecture)). The game logic (`src/game.c`) and the table
layout (`src/layout.c`) are platform-independent and shared unchanged.

## Platforms

| Platform | Build | Orientation | Input |
|----------|-------|-------------|-------|
| Linux / Windows / macOS | native (raylib) | any window shape | mouse + keyboard |
| Web (WASM) | Emscripten (raylib) | any window shape | mouse + keyboard + touch |
| Android | NativeActivity (raylib) | portrait or landscape | touch |
| iOS | native Metal (no raylib) | portrait or landscape, iPhone + iPad | touch |

One adaptive layout serves every platform. `src/layout.c` derives every metric
from the live view size each frame and tries four arrangements: the buttons in
a band along the bottom or in a column down the right side, and split hands in
one row or two. It keeps the one that gives the biggest cards, so a phone held
upright, a phone held sideways, an iPad and a desktop window each get a table
that fits, and rotating re-fits the same table. Cards are one size for the whole
view, so nothing jumps when a hand is split.

## Rules

- Finish closer to 21 than the dealer without going over. Aces count 1 or 11,
  picture cards 10.
- **Hit** or **stand**. A hand that reaches 21 stands by itself.
- **Split** two cards of the same value, up to four hands. Split aces take one
  card each, and 21 after a split is not a blackjack.
- The dealer checks for blackjack under an ace or a ten before anyone plays,
  then draws to 17.
- Each hand wins, loses, or ties the dealer (a push). The status line counts
  wins and losses; a split hand counts on its own, and a push counts as
  neither.
- There is no betting, and no chips or other currency. An earlier version had
  play-chip betting, which the App Store does not accept from an individual
  developer account.
- Nothing is saved between sessions.

### House rules (Options)

| Option | Values | Default |
|--------|--------|---------|
| Decks | 1, or a 6-deck shoe | 6 |
| Soft 17 | dealer stands, or hits | stands |

A change applies from the next hand, never to one already dealt. The shoe is
reshuffled between hands once three quarters of it has been dealt. If a round
ever runs a single deck dry, the shoe is rebuilt from the cards not on the
table, so no card can appear twice in a round.

## Controls

**Mouse and keyboard**, on desktop and desktop browsers:

| Input | Action |
|-------|--------|
| Click a button | Do it |
| H / S / P | Hit / Stand / Split |
| Enter / Space | Deal, or the next hand |
| Escape | Back to the menu, game stays resumable |
| Alt+Enter | Toggle fullscreen |
| Click a menu row | Choose it |
| Up / Down + Enter | Menu navigation; Left / Right cycle a value in Options |

**Touch**, on iOS, Android, and mobile browsers:

| Gesture | Action |
|---------|--------|
| Tap a button | Do it |
| Tap the title bar or the status line | Back to the menu, game stays resumable |
| Two-finger tap | The same, anywhere on the table |
| Tap a menu row | Choose it; in Options, tapping cycles the value |
| Swipe up / down | Move the menu selection; left / right cycles a value |

Cards are dealt one at a time and the hole card turns over; the buttons appear
once the cards have landed.

## Menu and window

These behave identically in every game in this family (openblocks, openrackem,
openklondike, opencheckers, openpairs, opensweeper, openjack). The code for them
(`src/menu.c`, `src/window.c`, `src/present.c`, and the gfx, safe-area,
timing, audio and recorder layers) is the same file in every repo.

- **Menu**: Resume Game (when a game is in progress), New Game, Options, Sound,
  Record (desktop only), Exit (desktop only, set apart by a blank line). Options
  holds the house rules and Back.
- **Menu input**: Up / Down (or W / S) move, Enter / Space choose, Left / Right
  (or A / D) cycle an Options value, Escape backs out. A mouse click or a tap on
  a row chooses it. Swipes move the selection and cycle values.
- **Menu size**: derived from the long edge of the view, so it is the same size
  upright and sideways and grows with the window; it shrinks only when its rows
  would not otherwise fit.
- **Back to the menu**: Escape, Android Back, or a two-finger tap. Losing focus
  (app backgrounded, tab hidden, window deactivated) also returns to the menu;
  the game stays resumable.
- **Window**: desktop opens at 960×720, resizes freely down to 640×480, and
  Alt+Enter toggles borderless fullscreen and back to the previous window.
  Web fills the browser viewport. Android and iOS are fullscreen.

## Building

raylib is built once from source into a gitignored install directory (per
platform) before the game is built. Each `scripts/build_raylib_*.sh` clones
raylib (pinned via `RAYLIB_TAG`, default `6.0`) and installs its headers and
`libraylib.a`. CI runs these scripts before each build.

### Desktop

```bash
./scripts/build_raylib_linux.sh      # once, on a fresh clone
make                                 # -> build/openjack   (dev, -O2)
make run
make release                         # -> build/openjack-release (-O3)
```

Windows (mingw-w64 cross-compile) and macOS (universal arm64 + x86_64):

```bash
./scripts/build_raylib_windows.sh && make windows   # -> build/openjack-x64.exe, -x86.exe
./scripts/build_raylib_mac.sh     && make mac       # -> build/openjack-mac
```

### Android (needs the Android SDK + NDK)

```bash
./scripts/build_raylib_android.sh
make android        # -> build/openjack.apk   (debug-signed, sideloadable)
make android-play   # -> build/openjack.aab   (Play App Bundle; PLAY_* signing vars)
```

A `NativeActivity` with no Gradle; a small `OpenjackActivity` Java class
(compiled with `javac` + `d8`) handles immersive full screen and hands the
window insets to the layout. arm64-v8a, `targetSdk` 36, 16 KB-page aligned.

### iOS (needs macOS + Xcode; no raylib)

```bash
make ios-sim   # -> build/ios-sim/Openjack.app   (Simulator, arm64)
make ios       # -> build/openjack.ipa           (device arm64, unsigned)
```

iPhone and iPad, both orientations, iOS 15+. The `.ipa` is unsigned unless
`IOS_SIGN_IDENTITY` / `IOS_PROFILE` / `IOS_TEAM_ID` are set; AWS Device Farm
re-signs an unsigned one on upload.

### Web (needs Emscripten)

```bash
./scripts/build_raylib_web.sh
make web        # -> build/web/openjack.{html,js,wasm}
make web-serve  # http://localhost:8080/openjack.html
```

## Tests

Unit tests with no raylib or window required:

```bash
make test
```

- `test_game` — hand totals, naturals and the dealer's peek, standing and
  busting, soft 17 both ways, splitting (pairs, ten-values, aces, the four-hand
  limit), the win and loss count across rounds and split hands, actions out of
  phase, the hidden hole card, the deal queue's order and
  timing, the shoe (contents, the reshuffle point, and no card twice in a round
  even when the shoe runs dry), and the fixed 60 Hz clock.
- `test_layout` — readable cards on real device shapes in both orientations;
  every hand, card and button inside the view and clear of each other in every
  phase; chrome that does not resize when the device is merely turned; buttons
  that hit-test where they are drawn.
- `test_input` — the touch tap recognizer: release-decided taps, drag rejection,
  two-finger tap, swipes, and slop scaling.
- `test_menu` — the family menu: it fits every view, keeps its size on rotation,
  grows with the window, and a pointer picks the row under it.

## Continuous integration and releases

Every pull request to `main` builds all platforms via GitHub Actions
([`ci.yml`](.github/workflows/ci.yml)) and runs `make test`. Pushing to `main`
cuts the next `release-N` via [`release.yml`](.github/workflows/release.yml),
which attaches per-platform archives, the Android APK, the iOS `.ipa` and the
WASM bundle to the GitHub Release; when the store secrets are set it also
uploads the AAB to the Play internal track, uploads the `.ipa` to TestFlight and
submits it to App Review. Setup:
[`android/play-assets/KEYSTORE.md`](android/play-assets/KEYSTORE.md) and
[`ios/app-store-assets/TESTFLIGHT.md`](ios/app-store-assets/TESTFLIGHT.md).

## Recording (desktop only)

Toggle **Record: On/Off** from the menu to capture the session to an H.264 MP4
(`openjack-YYYYMMDD-HHMMSS.mp4`), one video frame per rendered frame, no
external tools. The table is re-rendered at a fixed 640×480 and supersampled
for capture. Mobile and web compile it out.

```bash
./build/openjack --record            # auto-named file
./build/openjack --record clip.mp4   # explicit path
```

## Architecture

- `src/game.c` — rules only: the shoe, the deal, every action, the dealer, and
  settlement. No drawing, no input, no platform. Every rule resolves the moment
  the player acts; a presentation queue then plays the dealt cards and the
  hole-card flip out one at a time on the fixed 60 Hz clock, and input waits for
  it. Covered by `make test`.
- `src/layout.c` — the table solver: card size, rows, hand slots, the fan, and
  the buttons for each phase, as pure functions of the view size. This is what
  makes rotation and iPad support fall out rather than being special-cased.
- `src/render.c` — one adaptive renderer: chrome, the vector card art (shared
  with openklondike), the deal and flip animation, buttons, menus. Drawing goes
  through a small primitive layer (`src/gfx.h`): `src/gfx_raylib.c` wraps
  raylib, `ios/gfx_metal.mm` is a native Metal implementation. `src/oj_types.h`
  supplies raylib-compatible types so the shared code compiles without raylib
  on iOS.
- Audio is a similar seam (`src/audio.h`): `src/audio_raylib.c` vs
  `ios/audio_ios.mm` (AVAudioEngine). Effects are synthesized at startup; sound
  is off by default.
- iOS backend: `ios/plat_ios.mm` (touch / screen / timing) and `ios/ios_main.mm`
  (UIKit app + `CAMetalLayer` view + `CADisplayLink` loop).
- `src/safe_area.c` carries all four window insets, not just the top: sideways,
  the camera cutout and the gesture bar move to a side edge.
- All text is the bundled Nunito SemiBold (SIL OFL, see `NOTICE`), embedded so
  there is no runtime asset file. There are no asset files at all: cards are
  drawn, sounds are synthesized, icons and store screenshots are generated
  (`scripts/gen_icons.py`, `scripts/gen_store_screenshots.mjs`).

## Dependencies

- A C99 compiler (GCC or Clang); a C++ / Objective-C++ compiler for the iOS
  backend.
- [raylib](https://github.com/raysan5/raylib) 6.0 (static) on all platforms
  except iOS, built by the `scripts/build_raylib_*.sh` helpers.
- The MP4 recorder uses two vendored public-domain (CC0) single-header
  libraries: [minih264](third_party/minih264) and [minimp4](third_party/minimp4).

## Project structure

```
openjack/
├── src/            # shared C sources + gfx/audio raylib backends
├── ios/            # native Metal / UIKit backend (Objective-C++) + App Store assets
├── android/        # NativeActivity manifest, resources, Java activity + Play assets
├── web/            # Emscripten HTML shell
├── scripts/        # raylib build scripts, asset/font generators, store tooling
├── third_party/    # vendored single-header libs + Nunito
├── tests/          # game, layout, input and menu unit tests
├── docs/           # touch and scaling notes
├── Makefile
├── LICENSE         # MIT (this project's own code)
└── NOTICE          # third-party attributions
```

## License

openjack's own code is released under the [MIT License](LICENSE). The vendored
`minih264` and `minimp4` libraries are public domain (CC0), and the Nunito font
is under the SIL Open Font License; see [NOTICE](NOTICE) for attributions.
