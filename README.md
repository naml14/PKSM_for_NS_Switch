# FRLG read-only inspection foundation

The portable C++17 inspector and **host development command** check FRLG checksum ranges. The experimental Switch integration has been built with devkitPro, but has not been independently verified or tested on a console. Neither is a save editor. Functional testing uses synthetic data and host fakes, not real Switch hardware; no real-console compatibility is claimed.

A checksum match does **not** establish the game, language, or overall validity of an image. Corrupted, unrelated, or fabricated data can pass these limited checks.

## Try the host command

Build with a C++17 compiler and inspect a newly generated synthetic image:

```sh
# Keep the scratch parent private and trusted. A directory another user owns,
# or that other users can write, lets them replace the example directory and
# redirect the generated files, so ownership and permissions are both checked.
# The runner applies the same checks to the whole chain above the scratch root.
set -eu
root="$PWD/.tooling/test-work"
mkdir -p "$root"
uid="$(id -u)"
dir="$root"
while :; do
  test ! -L "$dir"
  owner="$(stat -c %u "$dir")"
  test "$owner" = "$uid" || test "$owner" = 0
  mode="$(stat -c %a "$dir")"
  test $(( 8#$mode & 022 )) -eq 0 || test $(( 8#$mode & 01000 )) -ne 0
  parent="$(dirname "$dir")"
  if [ "$parent" = "$dir" ]; then break; fi
  dir="$parent"
done
chmod 700 "$root"
set -o noclobber
work="$(mktemp -d "$root/example.XXXXXXXX")"
CXX="${CXX:-c++}"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
  src/main.cpp src/frlg_save.cpp src/frlg_report.cpp src/frlg_content.cpp \
  src/frlg_pokemon.cpp src/frlg_content_report.cpp src/frlg_text.cpp -o "$work/frlg-inspect.exe"
"$CXX" -std=c++17 tests/frlg_save_test.cpp -o "$work/frlg-fixture.exe"
"$work/frlg-fixture.exe" healthy > "$work/synthetic.sav"
"$work/frlg-inspect.exe" "$work/synthetic.sav"
rm -rf -- "$work"
```

For this fixture, stdout is:

```text
Slot 0: checksummed sections: international 14/14, Japanese 14/14; save index 17; complete checksum ranges: international and Japanese (ambiguous); section 4 checksums: international and Japanese (ambiguous)
Slot 1: checksummed sections: international 0/14, Japanese 0/14; save index unavailable; complete checksum ranges: none; section 4 checksums: not observed; invalid sector signature
Game identity, language, and content are not verified.
```

Use `frlg-inspect --report SAVE` for per-section checksum observations and warnings for both blocks, or `frlg-inspect --content SAVE` for decoded content and Pokémon summaries.

The content mode displays the decoded player name alongside the raw byte codes, and the decoded nickname of every Pokémon. When a name's codes require the other character table, it is decoded with that table and marked `[jp]`. The Switch result screen displays the decoded trainer name and up to 24 occupied box slots, with the number omitted.

These modes do not identify the game, language, or revision, and they do not select a block. Run `frlg-inspect --help` for usage.

The exit status is **0** when at least one slot has all 14 unique sections matching either checksum range, **1** for an input or read error or no complete range, and **2** for an incorrect argument count. Exit 1 for an incomplete slot does not prove the save cannot be used. An erased slot has an invalid signature; this alone is not proof of corruption.

## Produce a verified copy

`frlg-inspect --set-money VALUE SAVE [OUTPUT]` edits the amount of money of the block the game will load and produces a **verified copy**. It never writes to a console and never modifies the input file: without `OUTPUT` it only verifies and prints, and with `OUTPUT` it writes the copy there and refuses to write it over the input.

The tool recomputes the checksum of the section it changed, inspects the copy again, reads the money back and checks that every byte which differs from the original falls inside the changed value and that checksum. The copy is only produced when all of that passes, and the number of bytes that changed is reported as it is.

## Edit the save on the console

The application can change the amount of money of the save of the game that is running. It asks for the write mount only after the user chose to edit, it shows the old amount, the new amount and the number of bytes that will change, and it waits for an explicit confirmation before anything is written.

After the write the file is read back, the money is read back, the whole inspection is repeated, and every byte that differs from the original has to fall inside the money and its checksum. When that verification fails, the original bytes are written back before the app stops. The save counter is never changed, so the block the game loads stays the block that was edited. When the platform refuses the write mount, the app says so and stops.

**Close the game before you use the edited save.** The game keeps its own copy of the values in memory and writes them back when it saves, so a game that is still running can undo the change.

## Checks and limits

Only exactly 131,072-byte images are accepted. The first two 14-sector slots are inspected independently. Each 4 KiB sector is checked for its signature, section ID and checksum; duplicate or out-of-range IDs and invalid signatures are reported. Counts are **unique** matching IDs, so duplicate sections cannot make a slot complete. Sector order may rotate. Incomplete slots report their matching-section counts; a section with an invalid signature or ID is not counted.

Checksums use a modulo-2^32 sum of little-endian 32-bit words, folded once to 16 bits. The international and Japanese section-4 ranges are evaluated independently:

| Section ID | Bytes checksummed |
|---|---|
| 0 | `0xF24` |
| 1–3, 5–12 | `0xF80` |
| 4 | `0xEE8` international; `0xEC0` Japanese |
| 13 | `0x7D0` |

A complete range means all 14 stored sector checksums match that range. Section 4 results are also shown independently for incomplete slots, including when neither range matches or the section was not observed. Both section-4 ranges can match, and even a single matching range does not prove a language or game; the labels describe checksum lengths, not identification. Sector padding, the extra image area, semantic save contents, Pokémon legality, and unknown layouts are not validated.

Save indexes are observed from sectors with a recognized signature and ID, independently of their checksum results. Mixed indexes are reported as a possible partial or link save, **not** as proof of corruption. The host command does not select an active or newer slot, access Switch saves, write, repair, or edit data.

## Experimental Switch integration source

This is an **experimental, unverified source slice**, not instructions to use a console. It lists existing profiles, asks the user to select one, and then asks for a supported title before attempting any save mount. The selection screen briefly displays bounded profile nicknames; account identifiers are not printed; the result screen displays trainer and Pokémon summary data. No account-creation selector is used. To avoid ambiguous or unsafe labels, this first adapter omits profiles with unreadable, blank, non-ASCII, leading/trailing-space, or duplicate nicknames; those profiles cannot be selected in this slice.

After both choices, the source requests libnx `fsdevMountSaveDataReadOnly`, opens only the mapped filename in `rb` mode, reads at most 131,073 bytes, closes the file, and unmounts before inspecting exactly 131,072 bytes. It has no save creation, write, restore, commit, bank, or SD-output path. A failed mount is an access error, **not proof that the title or save is absent**. Cleanup errors are displayed and block another attempt. Checksums are limited observations, not proof of game identity, language, or save validity.

The [pinned project reference](https://github.com/Insektaure/pkHouse/blob/27650746bef5fb81353b00ae7393f7a3822bff16/include/game_type.h) documents these Spanish mappings; they are not observations from any user's console:

| Title | Application ID | File |
|---|---|---|
| FireRed | `0100EB702342C000` | `FireRed_s.sav` |
| LeafGreen | `01002B5023434000` | `LeafGreen_s.sav` |

Other regions, cross-title permissions, live-save access, and actual hardware behavior are unverified and not guaranteed. A successful build does not establish console compatibility or safety. Do not treat this source as tested deployment advice.

`switch/Makefile` requires devkitPro with devkitA64, libnx, and `elf2nro`, and `DEVKITPRO` set to that installation. Running `make` from `switch/` produced an NRO in an isolated Linux/amd64 container using `devkitpro/devkita64@sha256:82575ea78651b530b2e232bb3799cfd1fe331514e053d5f724bb4b28191fb79d`. The build exited successfully with 188 compiler warnings originating in libnx headers; it was not run on a console. The native entry point is `switch/main.cpp`, separate from the host `src/main.cpp`.

The source uses the documented `accountProfileGet` with a null user-data pointer for profile metadata. An earlier host syntax-only check failed because `switch.h` was unavailable. The later native build used real devkitPro headers, but does not establish runtime safety or hardware behavior.

## Synthetic tests

```sh
CXX="$PWD/.tooling/zig-cxx" bash tests/run.sh
CXX="$PWD/.tooling/zig-cxx" bash tests/cleanup_test.sh
```

Alternatively set `CXX` to another C++17 compiler. The existing tests assert exact output, exit status, and unchanged synthetic input bytes. Fixed checksum vectors exercise overflow, a single fold, each section boundary, both section-4 lengths, ambiguity, malformed sections, and mixed indexes.

To run the portable Switch coordinator tests using the `work` directory created in the host example above:

```sh
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
  tests/switch_readonly_test.cpp src/switch_readonly.cpp src/frlg_save.cpp \
  -o "$work/switch-readonly-test.exe"
"$work/switch-readonly-test.exe"
```

These tests use fake read-only operations and synthetic bytes. They cover both mappings, explicit selection, inspection, failure and cancellation paths, size bounds, and close-before-unmount ordering; they do not build the native adapter or test hardware. Each existing host suite run creates a unique directory under ignored `.tooling/test-work/` and removes only its own temporary directory on exit. The cleanup test checks that this occurs and unrelated scratch data remains unchanged. The manual examples retain their generated files for inspection.

## Format references

- [FireRed/LeafGreen decompilation save routines](https://github.com/pret/pokefirered/blob/master/src/save.c)
- [PKHeX Generation III save format](https://github.com/kwsch/PKHeX/blob/master/PKHeX.Core/Saves/SAV3.cs) and [FRLG layout](https://github.com/kwsch/PKHeX/blob/master/PKHeX.Core/Saves/SAV3FRLG.cs)
- [FRLG international and Japanese format map](https://decryptu.github.io/pokeldn/frlg_rom_map.html)
- [Generation III save data structure](https://bulbapedia.bulbagarden.net/wiki/Save_data_structure_(Generation_III)) and [pkHouse](https://github.com/Insektaure/pkHouse)
- libnx [filesystem device declarations](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/runtime/devices/fs_dev.h) and [account declarations](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/acc.h)

The implementation is independently written from documented API and format facts; these links do not grant a license to copy implementation code or assets.

## License

Copyright (C) 2026 naml14

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
