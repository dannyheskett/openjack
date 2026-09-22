# App Store assets

Artwork and listing copy for the iOS App Store, mirroring `android/play-assets/`
for Google Play. Nothing here is compiled into the app — the in-bundle app icon
lives in `ios/Assets.xcassets`, not in this folder.

Until the App Store Connect record exists and the `IOS_*` / `ASC_*` secrets are
set, the release workflow produces an unsigned `.ipa` and skips the TestFlight
upload. **[TESTFLIGHT.md](TESTFLIGHT.md) is the step-by-step for turning it on**
— no Mac required.

## Required

| Asset | Size | Notes |
| --- | --- | --- |
| `icon-1024.png` | 1024×1024 | **No alpha channel, no transparency, no rounded corners.** Apple masks the corners itself; a submitted icon with an alpha channel is rejected outright. `scripts/gen_icons.py` flattens it to RGB for exactly this reason. |
| `screenshots/iphone-6.9/` | 1290×2796 | Required. 1–10 images. Covers every current iPhone; Apple scales this set down for older devices. |
| `screenshots/ipad-13/` | 2064×2752 | Required **because the app declares iPad**. A version with no iPad set cannot be submitted. |

openjack ships for **iPhone and iPad** (`UIDeviceFamily = [1, 2]` in
`ios/Info.plist`) in **both orientations**, which is why there are two device
sets rather than the single iPhone set the portrait-only games in this family
carry. `UIRequiresFullScreen` is set, so iPad Split View cannot hand the game a
third-of-a-screen window.

The `*-landscape` folders hold the same frames turned sideways. Apple takes
either orientation for a slot but wants **one consistent set** — upload whichever
orientation you want that slot to show, not a mixture. `scripts/asc_release.py`
pushes the two upright folders.

Screenshots must be PNG or JPEG, sRGB, with no alpha channel;
`scripts/gen_store_screenshots.mjs` writes plain RGB PNGs, so the committed
files already satisfy that.

## Generating the assets

```sh
python3 scripts/gen_icons.py                   # icons, here and for Play
npm i playwright-core                          # dev-only, not a repo dependency
./scripts/build_raylib_web.sh && make web
node scripts/gen_store_screenshots.mjs --src build/web
```

The screenshots come from the web build because it compiles the same
`src/render.c` and `src/layout.c` the iOS app does, and the layout follows the
window — so a browser at 2064×2752 lays the table out exactly as an iPad does.
Real frames from the real renderer, capturable without a Mac.

Traps the script already handles, worth knowing if it is ever rewritten:

- A pointer move and press inside one frame records the tap origin at the
  PREVIOUS position, so the delta reads as a drag and the tap never fires.
  Settle after moving, before pressing.
- A press has to be held past a couple of frames. 60ms is silently dropped;
  120ms is reliable.
- The deal is random, so the page clock is pinned (`SHOT_CLOCK`): the game seeds
  its shoe from the time, and at that instant the first hand is a pair of
  eights, which shows a split. If the seeding changes, the script stops rather
  than capturing another hand.
- Buttons are found in the picture by their fill colour, never computed from
  the layout formula, and every tap must change the picture or the run fails.
