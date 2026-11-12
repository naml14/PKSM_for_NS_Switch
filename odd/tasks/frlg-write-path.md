# Writing to a save: the first verified copy

Objective: produce a modified and verified copy of a save on the host, without ever writing to a console or to the input file.
Branch: `feat/frlg-read-only`; delivery: `ask-on-risk`. The user delegated the design of this increment, so the specifications quote this document's own normative sentences.

## Specs

- S1: "A host tool that reads a save file, produces a modified copy and prints the diff is useful on its own, and it is the part that can be verified exhaustively without any risk to a game."
- S2: "The pipeline reads the save into memory, changes a copy, recomputes the affected section checksums, reads the copy back and re-runs the whole inspection, shows the user a diff, and only then may write."
- S3: "Only exactly 131,072-byte images are accepted."
- S4: "The game loads the block with the higher save counter, so a write to the older block would be silently ignored, and a write to the newer block keeps the counter that makes it win."
- S5: "The differences between the original and the copy may only fall inside the bytes of the changed field and the bytes of the recomputed checksum; a byte outside that set must never change, and the number of bytes that did change is reported as it is."
- S6: "If the platform refuses the write mount, the app says so and stops. It never retries with different privileges."
- S7: "The user backup is read, never written, and it is the ground truth for every comparison."
- S8: "The write is never partial: the file is written whole, and the app confirms the size on disk before closing it."
- S9: "The write mount is only requested after the user chose to edit, and the app never asks for write access while it is only reading."
- S10: "Nothing is written to the console until the user has seen the old value, the new value and the number of bytes that will change, and has confirmed it explicitly."
- S11: "The original bytes are kept in memory until the write has been verified, and when the verification fails they are written back before the app stops."
- S12: "After the write the file is read again, the money is read back, the whole inspection is repeated, and every byte that differs from the original has to fall inside the changed field and the recomputed checksum."
- S13: "The app tells the user to close the game before the new values count, because the game keeps its own copy of the values in memory and writes them back when it saves."
- S14: "The save counter is left alone, so the block the game loads stays the block that was edited."
- S15: "The editable values of a Pokémon are the nickname, the level, the IVs, the EVs, the shininess, the gender and the nature. The current HP is not shown and not edited, and the experience is not edited because it follows the level."
- S16: "The values that come from the personality value are edited together: the nature, the gender, the shininess and the IVs all come from it, so the tool searches for a personality value that gives the requested traits instead of changing them one by one."
- S17: "When the requested traits cannot exist together, the tool says so and writes nothing. The nature constrains the parity of the IVs, so some combinations are impossible."
- S18: "The level is not stored, it is derived from the experience, so editing the level writes the experience that the game would give for that level and the species, and the derived level reads back as the requested one."
- S19: "The EVs respect the limits of the game: at most 252 per stat, at most 510 in total, and the four stats that cannot carry EVs at the same time leave the rest at zero."
- S20: "The legality of a record depends on how it was obtained. A record that was bred keeps a personality value that a breeding could have produced, and a record that was found in the wild keeps its personality value when the traits do not change, because those traits come from it."
- S21: "The traits that do not come from the personality value, the nickname, the level and the EVs, are written without touching it, so a record that was legal stays legal."
- S22: "A record is written with its checksum recomputed and its data encrypted again, and it is read back before it is handed over."
- S23: "The editor of a Pokémon is shown on the screen of the console, with the traits of the record and the values that can be changed."
- S24: "The editor refuses the values that the game cannot hold, and it says which one."
- S25: "The editor shows the level and the traits that the record will have after the change, before anything is written."
- S26: "The editor of a Pokémon uses the same write path as the money, so it asks for the confirmation and it verifies the record after the write."

## Tasks

- [x] T1 (S1-S5, S7; commit 690ea75): Produce a verified modified copy on the host. The first editable field is the money, because it is one value in one section, so its checksum is the only one that has to be recomputed, and a wrong value is obvious on the next read. Proven on the real save: 999999 -> 123456, five bytes changed, and the user confirmed the game loaded the new save.
- [x] T2 (S6, S8-S14; commits d024933, 4ea5364 and 4209245): Write to the console, with the original bytes kept in memory for a rollback, and only after the verification passed on the exact bytes to be written. The logic that decides and verifies lives in host-testable helpers; the console application only mounts the save for writing, drives the editor and asks for the confirmation. Proven on the console: the user edited the money of the running game and the change was written and verified.
- [ ] T3 (S15-S22; depends on T2): Edit the Pokémon records: nickname, level, IVs, EVs, shininess, gender and nature. The current HP is neither shown nor edited, and the experience is not edited because it follows the level.
- [ ] T4 (S23-S26): Show the editor of a Pokémon on the console, which needs a screen because it shows the traits of a record.

## Interface

Helpers exposed by `include/pksm/frlg_save.hpp`, implemented by reusing the internals of the reader so the two cannot drift apart:

```cpp
// The number of bytes a section's checksum covers, for the international or
// the Japanese range.
std::size_t frlg_section_used_length(std::size_t section_id, bool international);
// The checksum of the sector that starts at sector_base, over used_length bytes.
std::uint16_t frlg_sector_checksum(const std::vector<std::uint8_t>& image,
                                    std::size_t sector_base, std::size_t used_length);
```

The new module `include/pksm/frlg_write.hpp`:

```cpp
struct MoneyEdit {
    bool ok = false;
    std::string reason;                  // why it failed, empty when it worked
    std::vector<std::uint8_t> image;      // the modified copy; empty when it failed
    std::size_t block = 0;                // the block that was edited
    std::uint32_t old_money = 0;
    std::uint32_t new_money = 0;
    std::size_t changed_bytes = 0;        // how many bytes differ from the original
    std::vector<std::size_t> changed_offsets;  // where they differ
};

MoneyEdit set_frlg_money(const std::vector<std::uint8_t>& original, std::uint32_t value);
```

The new module `include/pksm/frlg_console_write.hpp`:

```cpp
// The result of a write to a save file.
struct ConsoleWriteResult {
    bool ok = false;
    std::string reason;              // why it failed, empty when it worked
    bool rolled_back = false;        // the original bytes were written back
    std::size_t changed_bytes = 0;   // how many bytes differ from the original
    std::uint32_t old_money = 0;
    std::uint32_t new_money = 0;
};

// Writes the new money to the file at path, reads it back, verifies it and
// writes the original bytes back when the verification fails.
ConsoleWriteResult commit_frlg_money(const std::string& path, std::uint32_t value);
```

The new module `include/pksm/frlg_money_editor.hpp`:

```cpp
// The money as the user edits it on the console. The game accepts at most
// 999999, so the editor never goes above that.
struct MoneyEditor {
    std::uint32_t value = 0;
    std::size_t digit = 0;   // 0 is the most significant digit
    static constexpr std::size_t digit_count = 6;
};

enum class MoneyEditorAction { left, right, up, down };
void apply_money_editor_action(MoneyEditor& editor, MoneyEditorAction action) noexcept;
```

The command line: `frlg-inspect --set-money VALUE SAVE [OUTPUT]`. `VALUE` is a decimal u32 and anything else is an argument error. `SAVE` is the file to read and the single character `-` reads the save from standard input in binary mode. `OUTPUT` is optional: without it the tool verifies and prints without writing, and with it the tool writes the copy and refuses to write it when it resolves to the same file as `SAVE`. The tool prints the block it edited, the old and the new money, how many bytes changed and the result of the verification, and on failure it prints `error: <reason>` on standard error. The exit status stays consistent with the rest of the tool: 0 on success, 1 for an input, read or verification error, 2 for an argument error.

## Log

- L1: The user asked to continue the development and agreed with the proposed order, which put the read path and the documented write path design before any editing.
- L2: The first increment of the write path was delegated with the algorithm fixed in advance: pick the block the game loads, the highest clean save counter among the blocks whose fourteen sections match a checksum range, and refuse when two blocks share that counter; write the new money encoded with the SaveBlock2 key; recompute the checksum of the affected section; and verify the copy by inspecting it again, reading the money back and checking that no byte outside the permitted set changed.
- L3: The delegated worker found that requiring exactly six changed bytes was impossible for arbitrary values, because a new value can share bytes with the old one and setting the current value changes nothing. The rule was corrected to the one in S5: the differences may only fall inside the permitted bytes, and the actual count is reported, including zero.
- L4: The user restored the produced copy with their own backup tool and confirmed: "se ve perfecto, al cargar el nuevo save al juego. Proceda a T2". The write path on the host is therefore proven end to end on a real save, and the console path starts now.
- L5: The console path keeps every decision in host-testable helpers, so the rules that protect a real save are verified without a console. The application only mounts the save for writing when the user asks to edit, shows the old value, the new value and the number of bytes that will change, and waits for an explicit confirmation. The original bytes stay in memory until the write has been read back and verified, and they are written back when that verification fails. The save counter is not touched, because the block the game loads has to stay the block that was edited.
- L6: An independent verification of the console write path found six defects by static review, because its isolated preparation timed out and it could not run a single test. All six were fixed. A write that fails halfway now puts the original bytes back before the app stops. The bytes that go to the console are the ones that were confirmed, and the write is refused when the file changed in the meantime. The device is committed before it is released, and the result of the release is reported. The editor never carries an amount above the maximum the game accepts, and an amount above it is refused. A block that is complete under the Japanese checksum range is accepted as well, because the selection accepts either range. The size of the file on disk is confirmed before it is closed.
- L7: The reviewer also reported that the rollback test cannot tell a real restoration from an abort before the write. The restoration is now covered by its own test, which puts mixed bytes in a file and puts the original back, and the write itself is covered by the tests of the successful path. What stays uncovered is the behaviour of the platform itself: the mount that is refused, the commit of the device and the release of it can only be observed on the console.
- L8: The user tested the corrected build on the console and confirmed that it works: "funcionó perfectamente, ya tenemos el primer escalón hecho". The write path therefore works on the real hardware, which also answers the platform behaviour the reviewer could not observe.
- L9: The user fixed the scope of the editor of Pokémon: "los PS actuales del pokémon no se deberían ni ver ni editar, ya que no es importante para que un pokémon sea legal. La experiencia tampoco es necesario que sea editable ya que si se edita el nivel este valor pasará a 0". The editable values are the nickname, the level, the IVs, the EVs, the shininess, the gender and the nature, because they are quantities with limits or a short list of options.
- L10: The user explained that both ways of handling the personality value are right and that the choice depends on the record: the legality is kept by recomputing the personality value only when the Pokémon could be obtained in a wild encounter in the game where it was created, or when it is the result of a breeding, that is, when the origin is configured as a breeding. Since the origin is not being edited yet, the recommendation was to build the search of the personality value and to apply the legality rule per origin: the breeding is supported now and the wild encounter warns that the record stops being a legitimate encounter.
- L11: The user confirmed that approach: the search of the personality value, with the legality rule per origin.
