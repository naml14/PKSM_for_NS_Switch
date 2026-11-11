# Writing to a save: the design before any write

Objective: define the first safe write capability, so that editing a save becomes a normal
feature of the tool instead of the riskiest thing in it.

Status: design only. No code writes anything yet, and nothing in this document has been
implemented or tested on a console.

## What the read-only milestone already proves

- The Gen 3 FRLG layout is verified against the `pokefirered` decompilation and PKHeX, and
  against the user's own save: trainer name, play time, money, Pokedex counts, box occupancy
  and party records agree between the host reader and the Switch reader.
- Every sector carries a checksum over its used length, and the checksum of every section in
  the user's save validates. A write that changes section bytes must therefore recompute the
  checksum of the section it touched, or the game will reject the save.
- The save holds two complete 14-sector blocks. The game loads the block with the higher
  save counter, so a write to the older block would be silently ignored, and a write to the
  newer block keeps the counter that makes it win.

## The first write candidate

Change the money, and nothing else.

It is the smallest possible change that still exercises the whole path: one 32-bit value at
SaveBlock1 offset 0x290, obfuscated with the SaveBlock2 key at offset 0xF20, inside one
section, whose checksum must be recomputed. It is visible on the next read, it is obvious to
the user whether it worked, and a wrong value can be corrected by writing the original bytes
back.

Editing a Pokémon is deliberately not the first step. It touches the encrypted substructures,
the party and box checksums and the level and HP fields at once, so a mistake there is harder
to detect and harder to undo.

## The pipeline

1. Read the save through the read-only path, into memory, and keep the original bytes.
2. Apply the change to the copy in memory.
3. Recompute the checksum of every section whose bytes changed.
4. Read the modified copy back and re-run the whole inspection: the changed field must hold
   the new value, every other field must be identical to the original, and every section
   checksum must validate.
5. Show the user the diff, the old value and the new value, and the result of step 4.
6. Only then, and only with an explicit confirmation, write.

Steps 1 to 5 never touch the console. A host tool that reads a save file, produces a
modified copy and prints the diff is useful on its own, and it is the part that can be
verified exhaustively without any risk to a game.

## Writing to the console

The console write is the last step and the only one that can damage a game. It is allowed
only after the host pipeline has been proven, and with these conditions:

- The app mounts the save for writing only after the verification of step 4 has passed on
  the exact bytes it is about to write.
- The original bytes stay in memory for the whole session. If the write or the read-back
  fails, the original bytes are written back immediately.
- The write is never partial: the file is written whole, and the app confirms the size on
  disk before closing it.
- The game must be closed. A mounted save cannot be written while the game owns it, which is
  the behaviour that protects the user.
- If the platform refuses the write mount, the app says so and stops. It never retries with
  different privileges.

## Why the existing backup matters

The user already has a JKSV backup of the real save, taken with the game closed. That backup
is the ground truth for every comparison in this design, and the rollback of last resort.
It must stay untouched: it is read, never written.

## Abort conditions

The write path is abandoned, not worked around, when:

- the write mount is refused by the platform,
- the read-back after a write does not match the intended change byte for byte,
- any section checksum fails after the change,
- the save counter and the slot selection would leave the game loading a different block
  than the one that was modified.

## Open questions to settle before implementing

- Whether the Switch platform allows a third party application to mount another title's save
  for writing at all. The read-only mount works; the write mount may be refused, and that
  refusal is a safety property, not a bug to work around.
- Whether the app should offer a "write to the console" step at all, or stop at producing a
  verified modified file that the user deploys with their own backup tool. The second option
  is safer and is the default recommendation until the first is proven.
- Whether the trainer name, the party and the boxes become editable before the money, and
  in which order.
