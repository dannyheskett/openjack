# openjack — Google Play store listing

Copy/paste these into the Play Console (**Grow → Store presence → Main store
listing**, plus **Store settings** for category). The images in this folder are
the shipped assets. `scripts/play_release.py` pushes the fields below and the
images from this folder, so edit them here rather than in the console.

The screenshots regenerate with `scripts/gen_store_screenshots.mjs`, and the
icon and feature graphic with `scripts/gen_icons.py`.

## Assets (this folder)

| File | Play field | Spec |
|------|-----------|------|
| `icon-512.png` | App icon | 512×512 PNG (32-bit) |
| `feature-graphic-1024x500.png` | Feature graphic | 1024×500 PNG/JPG |
| `screenshots/phone/` | Phone screenshots | 4× 1080×1920 PNG |
| `screenshots/phone-landscape/` | Phone screenshots (sideways) | 4× 1920×1080 PNG |
| `screenshots/tablet/` | 7-inch and 10-inch tablet screenshots | 4× 1600×2560 PNG |
| `screenshots/tablet-landscape/` | Tablet screenshots (sideways) | 4× 2560×1600 PNG |

The game plays in both orientations, so each slot has an upright and a sideways
set. Upload one orientation per slot, not a mixture.

## App name (≤30 chars)

```
openjack
```

## Short description (≤80 chars)

```
Blackjack against the dealer, no betting. Free, no ads, no tracking.
```

## Full description (≤4000 chars)

```
Blackjack (21) against the dealer. Take cards and try to finish closer to 21 than the dealer without going over. There is no betting: the game keeps count of the hands you win and lose.

No ads. No tracking. No accounts. No in-app purchases. openjack requests zero permissions and never touches the network. It's just the game.

THE RULES
• Hit or stand on any hand
• Split pairs, up to four hands; split aces take one card each
• The dealer checks for blackjack under an ace or a ten
• Each hand wins, loses, or ties the dealer. The game counts your wins and losses

HOUSE RULES IN OPTIONS
• A single deck or a six-deck shoe
• Dealer stands on soft 17, or hits it

ON ANY SCREEN
• The table fits your screen, upright or sideways, on a phone or a tablet
• Fully offline, a small download, and easy on your battery

FREE AND OPEN SOURCE
openjack is open source. Read the code, report a bug, or build it yourself: https://github.com/dannyheskett/openjack
```

## Categorization (Store settings)

- **App or game:** Game
- **Category:** Card
- **Tags:** card, blackjack, offline
- **Email:** dan@danheskett.com
- **Website:** https://danheskett.com
- **Content rating:** answer the IARC questionnaire truthfully. It asks about
  simulated gambling: answer **no**. There is no betting, wagering, or currency
  of any kind, only a count of hands won and lost.
- **Target audience:** set to match the other games in this family. An earlier
  build had play-chip betting and was set to 18 and over. Update the Play
  Console content rating and target audience when this build ships.

## Data safety (Policy → App content)

- Data collected: **None**
- Data shared: **None**
- App has no `INTERNET` permission (verify in the manifest) → "no data
  transmitted off the device" is truthful.
- Privacy policy URL: **https://danheskett.com/app/privacy-policy/**

## Screenshots

Pushed via the Play API from the folders above. Captured from the web build --
the same C the Android app runs -- in a headless browser at each target size:

    npm i playwright-core
    make web
    node scripts/gen_store_screenshots.mjs --src build/web

That one command also refreshes the App Store sets under
`ios/app-store-assets/screenshots/`, so the two listings cannot drift apart.
Regenerate whenever the table, the chrome or the font changes.
