# openjack — App Store listing

Copy/paste into App Store Connect. Mirrors `android/play-assets/LISTING.md`, with
the differences Apple requires (subtitle, keywords, promotional text).
`scripts/asc_release.py listing` pushes the fields below and the screenshots,
so edit them here rather than in the console.

One rule that differs from Play:

- **Never mention Android, Google Play, or another platform** in the description.
  Apple rejects listings that reference competing stores.

## New App form (My Apps -> + -> New App)

| Field | Value |
| --- | --- |
| Platform | iOS |
| Name | `openjack` (must be unique App Store-wide; see fallbacks below) |
| Primary Language | English (U.S.) |
| Bundle ID | `com.danheskett.openjack` |
| SKU | `openjack` |
| User Access | Full Access |

If `openjack` is taken, in order of preference: `openjack 21`,
`openjack Cards`, `openjack Game`. The name is public, capped at 30
characters, and can be changed with any later version — the SKU and bundle ID
cannot.

## Subtitle (<=30 chars)

```
Blackjack with play chips
```

## Promotional text (<=170 chars)

Editable anytime without submitting a new build — use it for release notes or
seasonal copy.

```
No ads, no tracking, no purchases, no real money. Blackjack against the dealer, free and open source.
```

## Keywords (<=100 chars, comma-separated, no spaces after commas)

Do not repeat the app name — it is already indexed.

```
blackjack,21,cards,card game,dealer,split,double down,offline,classic,table
```

## Description (<=4000 chars)

```
Blackjack (21) against the dealer. Place a bet, take cards, and try to finish closer to 21 than the dealer without going over. The chips are play chips: there is no real money, nothing to buy, and nothing to win.

No ads. No tracking. No accounts. No in-app purchases. openjack never touches the network. It's just the game. You can even play it in airplane mode.

THE RULES
• Hit, stand, or double down on any two cards
• Split pairs, up to four hands; split aces take one card each
• Insurance, or even money on a blackjack, when the dealer shows an ace
• Blackjack pays 3:2, insurance 2:1, and a tie is a push
• The dealer checks for blackjack under an ace or a ten

HOUSE RULES IN OPTIONS
• A single deck or a six-deck shoe
• Dealer stands on soft 17, or hits it
• Late surrender on or off

ON ANY SCREEN
• The table fits your screen, upright or sideways, on iPhone or iPad
• Run out of chips and you get a fresh stake

FREE AND OPEN SOURCE
openjack is open source. Read the code, report a bug, or build it yourself: https://github.com/dannyheskett/openjack
```

## App Review notes

Sent to Apple's reviewer with every submission that has none yet
(`scripts/asc_release.py` sets them, with the team's review contact).

```
Thank you very much for reviewing my game. openjack is blackjack against the dealer, played with play chips only: there is no real-money wagering, no purchase of any kind, no prizes, and chips cannot be bought or exchanged. It needs no account, sign-in or network access. It runs on iPhone and iPad in either orientation; tapping the title bar at the top of the screen opens the menu, where Options sets the house rules.
```

## App information

- **Category (primary):** Games -> Card
- **Category (secondary):** Games -> Casino
- **Content Rights:** does not contain third-party content
- **Age Rating:** answer **Simulated Gambling: Frequent/Intense** (the whole game
  is a gambling simulation) and "None" to everything else. App Store Connect
  computes the rating from the answers; it will not be the 4+ the other games
  in this family get. Answer "No" to "Gambling" (real money): there is none.
- **Copyright:** `2026 Daniel Heskett`
- **Support URL:** https://danheskett.com
- **Marketing URL:** https://danheskett.com/projects/openjack/
- **Privacy Policy URL:** https://danheskett.com/app/privacy-policy/

## Screenshots

Two device families are required because the app declares iPhone and iPad
(`UIDeviceFamily [1,2]` in `ios/Info.plist`): the 6.9" iPhone set (1290x2796)
and the 13" iPad set (2064x2752). Each has an upright and a sideways folder;
upload one orientation per slot, not a mixture.

Captured from the web build -- the same C the app runs -- in a headless browser:

    npm i playwright-core
    make web
    node scripts/gen_store_screenshots.mjs --src build/web

## App Privacy (App Store Connect -> App Privacy)

Answer **"No, we do not collect data from this app."** — accurate and verified:
no network code, no analytics SDK, no permissions requested. This yields a
"Data Not Collected" privacy label. It has no API, so it is set once by hand,
and it must be **published** (the button at the top right) before a version can
be submitted.

## Pricing

Free. No in-app purchases.

## Export compliance

openjack uses no encryption of any kind. `ITSAppUsesNonExemptEncryption = false`
in `ios/Info.plist` stops App Store Connect asking on every upload.
