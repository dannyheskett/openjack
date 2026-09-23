# Touch and scaling

How openjack fits one table to every screen, and how touch reaches it. The rules
here are the family's (openblocks, openklondike, openpairs); this note records
how they apply to a blackjack table.

## One layout, four arrangements

`src/layout.c` is pure: it takes the view size and returns every rectangle on
the table. There is no desktop renderer and touch renderer to choose between.
For each view it tries:

| Buttons | Split hands | Suits |
|---------|-------------|-------|
| band along the bottom | one row of four | desktop windows, iPad sideways |
| band along the bottom | two rows of two | phones held upright |
| column down the right | one row of four | phones held sideways, wide windows |
| column down the right | two rows of two | tall tablets |

and keeps the arrangement with the biggest cards. The shapes in the right-hand
column are what the solver picks, not rules written into it.

Cards are sized for four hands of two cards, whatever is dealt, so a split never
shrinks the cards or moves the dealer. A hand longer than its slot compresses
its fan instead of overflowing. When a view keeps a second row for split hands
and the round has only one or two, the table moves down half a row so it sits
centred (`layout_for_hands`); that is the only thing that moves when a third
hand appears.

## Chrome is sized from the long edge

The wordmark (`ref/45`), the status line and hand totals (`ref/38`), the message
band and the buttons are all sized from `ref = max(width, height)`, never from
the live height. openblocks is portrait-locked, so its height-based formulas
always meant the long edge; applied to the live height in a game that rotates,
turning the phone sideways would halve every piece of chrome. openklondike hit
exactly this (its PR #15), and `tests/test_layout.c` asserts that turning the
device leaves every chrome size unchanged.

The margin (`short/28`) and gap (`short/90`) come from the short edge, as in the
rest of the family.

## Safe area

On Android the Activity pushes all four window insets over JNI
(`src/safe_area.c`); the table keeps clear of them, and the wordmark bar grows
to clear a camera cutout and steps aside from it. iOS hands the game a viewport
that already excludes the notch and the home indicator, so every inset is zero
there, as on desktop and web.

## Touch

`src/input.c` is the family recognizer: a tap is decided on release (under
0.5 s and within a third of a card width of where it started), a two-finger tap
is Escape, vertical swipes move a menu selection and horizontal swipes cycle an
Options value. Android Back is Escape.

On the table a tap presses the button under it. Buttons are at least
`2.25 × status font` tall, which is about 50 pt on a phone. A tap on the
wordmark bar or the status line opens the menu, because a two-finger tap is not
something a player finds alone; the first hands also say so in the empty row
where the player's cards land.

Input waits while dealt cards are landing (`game_busy`), so a tap can never act
on a card the player has not seen yet.

## Motion

Every rule resolves the moment the player acts. What the player sees is paced
separately: each dealt card and the hole-card flip go into a queue in
`src/game.c`, and `game_update()` plays it out on the fixed 60 Hz clock, a card
every quarter second. A card slides in from off the top-right corner, where a
shoe would sit; the hole card turns over a fifth of a second, squeezed through
the middle of the turn. Sounds and the win and loss count follow the queue too,
so the result is heard and counted when the last card lands, not before.
