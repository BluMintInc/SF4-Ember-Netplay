# Focus cancel marker

How the frame meter tells a focus, and a dash out of one, from the game's own
data, for the focus cancel reading (`CancelChain` in
`src/training/FrameMeter.hxx`).

Read-only research in `SSFIV.exe.i64` (image base 0x400000) through idalib,
without auto analysis, and in the stock `.bcm` and `.bac` files of the game
folder. Nothing was renamed, commented, patched or saved. Addresses are
virtual addresses; Ember's `Locate` code uses `peRoot + (va - 0x400000)`.

## Result

A fighter is in a focus when its status is Skill (16) and the command move
it plays has category bit 0x80. The command move is the `.bcm` move record
the Skill was started from; `Chara::Actor` vtable +0x228 (0x5423B0) returns
it (actor +0x2188), and its category is the 32-bit word at +0x28. The same
word's bits 0x80000 and 0x100000 mark the command moves DASH and BACK_DASH,
which is how a dash out of a focus is played.

```
const Move* move = actor->vtable[0x228 / 4](actor);   // null unless status is Skill
focus      = move && (*(uint32*)(move + 0x28) & 0x80);
dash       = move && (*(uint32*)(move + 0x28) & 0x80000);
back dash  = move && (*(uint32*)(move + 0x28) & 0x100000);
```

`TrainingRuntime`'s `ReadFighters` reads it and resolves it with
`ResolveActionClass` into `FighterSample::actionClass`: Focus, ForwardDash,
BackDash, Other, or Unknown when there is no sample or a Skill has no command
move. The dash statuses FrontDash (12) and BackDash (13) are classed as dashes
too. The meter and the renderers see only the class, never an action ID.
`SF4E_TRAINING_CAPTURE=1` writes it as the `action_class` column (the enum's
value), with the reading's cancel as `cancel_dash` and `cancel_startup`.

## How each piece was confirmed

**The game reads the bit as "focus".**
- `0x5B2F80` (CPU opponent code) calls vtable +0x228 on an actor and returns
  `move != 0 && *(char*)(move + 0x28) < 0`: the move's category has bit 0x80.
  `0x5B2FD0` asks the same after checking vtable +0x298 (GetStatus,
  `0x542520`, already bound by Ember) is 16, and `0x5B38F0` does the same test.
- `0x5B4C80` weighs moves by the same word: 0x200 special, 0x400 super, 0x800
  ultra, 0x80 focus, 0x100 taunt, 0x80000 dash, 0x100000 back dash.
- Entering Skill (`0x54E7A0`, the status table at `0x94F310`, entry 16) reads
  vtable +0x228 and turns the category into the actor's attack attributes;
  0x80 sets attribute 0x80.
- The executable holds the category names in bit order (strings at
  `0x936C98` onward, listed from `0xA50428`): LP, MP, HP, LK, MK, HK, THROW,
  SAVING, APPEL, SUPER MOVE, SUPER COMBO, ULTRA COMBO, EX, TARGET COMBO,
  STAND, CROUCH, JUMP, FORWARD, BACKWARD, DASH, BACKDASH. Bit 7 is SAVING, the
  game's name for focus, and bits 19 and 20 are DASH and BACKDASH. Ryu's moves
  carry exactly these bits (5LP 0x1, 5MP 0x2, throws 0x40, APPEAL 0x100,
  specials 0x200, DASH 0x80000, BACK_DASH 0x100000).

**The command move is the current Skill's, and only a Skill's.**
- `0x54CC30` (starts the pending command move) stores the move at actor
  +0x2184 and +0x2188, takes the action ID from the move's +0x24 (its script
  index, plus a per-fighter offset for a taunt, category 0x100), and always
  enters status 16. So a command move, a dash included, plays as a Skill.
- Every status entry other than 16 (`0x548890` and its inlined copies, about
  20 sites) zeroes actor +0x2188. A BAC flow change inside a Skill (a focus
  going from its charge to its level 2 or 3 release) does not go through a
  status entry, so the focus's move stays.

**A dash out of a focus is the command move DASH or BACK_DASH.**
- The `.bcm` cancel lists named SAVING and SAVING_CANCEL hold only the moves
  DASH and BACK_DASH (Ryu's file, list 8 and 9), and DASH and BACK_DASH carry
  categories 0x80000 and 0x100000. Started from those lists they play as Skill
  (above), with action 18 or 19, the same scripts the dash statuses play
  (`0x54B650` and `0x54B6A0`, the FrontDash and BackDash entries, play
  actions 18 and 19).
- So in the game the focus cancel's dash is expected as status 16 with class
  ForwardDash or BackDash, not as status 12 or 13. The meter accepts both;
  which one the game shows has not been seen in a capture.

**The bit marks every focus and nothing else.** A script over every fighter's
command file (the layer `GameFiles.hxx` picks) listed the moves with bit 0x80
and the names of the `.bac` scripts they start:
- In all 44 fighter folders, every such move starts a script named SAVING or
  FOCUS (SAVING_MAX, SAVING2, SAVING3, EX_SAVING, SUPER_SAVING_* or
  SUPER_FOCUS_*, the red focus and the cancel versions included), and no
  other move has the bit.
- Following the BAC flow commands of those scripts adds, on every fighter, the
  level 2 and 3 releases of a red focus done as a cancel (SUPER_FOCUS_LV1_EX
  and the like), and nothing that is not named for a focus. A flow change keeps
  the command move, so the live read covers them.
- Scripts named for a focus that neither reaches: SAVING_MAX2 on most
  fighters, a few older copies in the PSN, RLN and CNL folders, and
  the copies numbered 1000 above the marked ones on Gen, Yun, Yang and the
  boss. The game's `action % 1000` tests suggest those play in a stance; they
  were not traced. If they are started from a marked move, the live read covers
  them too, where a list of script indices would not.
- The live read also works in a rollback match, where `ReadFighters` does not
  read the fighters' files.

## Limits

- Not checked in the game; no live memory read was made. A capture of
  DP xx FADC forward and back, on hit and on block, with
  `SF4E_TRAINING_CAPTURE=1` is owed, to compare `action_class` and the
  meter's reading with what happened.
- A Skill no command move started (one the game starts itself) reads Unknown,
  and a focus cancel is only read from a move classed Other.
- The tests in `src/tests/focus_cancel_test.cxx` are synthetic: they check the
  meter's contract with samples made by hand, not the game.
