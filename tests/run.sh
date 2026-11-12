#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/.."
: "${CXX:=c++}"
uid="$(id -u)"
# The scratch root and every directory above it must be private and owned by a
# trusted principal, the invoking user or root. A directory another local user
# can write, or owns, lets them rename the scratch root and substitute an
# attacker-owned tree whose run directory the suite then compiles and runs with
# the user's privileges. The owner can widen the permissions at any time, so
# ownership decides trust and not the current mode alone.
require_trusted_chain() {
    local dir="$1" mode owner parent
    while :; do
        mode="$(stat -c %a "$dir" 2>/dev/null || true)"
        owner="$(stat -c %u "$dir" 2>/dev/null || true)"
        if [[ ! "$mode" =~ ^[0-7]+$ || ! "$owner" =~ ^[0-9]+$ ]]; then
            printf 'error: cannot read the permissions of %s\n' "$dir" >&2
            return 1
        fi
        if [[ "$owner" != "$uid" && "$owner" != 0 ]]; then
            printf 'error: %s is owned by another user\n' "$dir" >&2
            return 1
        fi
        if (( (8#$mode & 022) != 0 && (8#$mode & 01000) == 0 )); then
            printf 'error: %s is writable by other users\n' "$dir" >&2
            return 1
        fi
        parent="$(dirname "$dir")"
        if [[ "$parent" == "$dir" ]]; then break; fi
        dir="$parent"
    done
    return 0
}
root="$PWD/.tooling/test-work"
mkdir -p "$root"
if [[ -L "$root" || ! -d "$root" ]]; then
    printf 'error: test scratch root must be a real directory\n' >&2
    exit 1
fi
# The scratch root must stay unwritable for anyone but its owner and must
# belong to the invoking user. A writable root lets another local user replace
# the run directory with a symbolic link while the suite runs and redirect the
# generated saves, including a real save file with a matching name, elsewhere.
chmod 700 "$root" 2>/dev/null || true
root_owner="$(stat -c %u "$root")"
if [[ "$root_owner" != "$uid" ]]; then
    printf 'error: test scratch root must be owned by the invoking user\n' >&2
    exit 1
fi
scratch_mode="$(stat -c %a "$root")"
if [[ ! "$scratch_mode" =~ ^[0-7]+$ ]] || (( 8#$scratch_mode & 022 )); then
    printf 'error: test scratch root must not be group or world writable\n' >&2
    exit 1
fi
# The directories above the scratch root must be trusted as well.
if ! require_trusted_chain "$(dirname "$root")"; then
    exit 1
fi
# Never write through a link that already exists.
set -o noclobber
work="$(mktemp -d "$root/run.XXXXXXXX")"
# The tests that touch files write inside this scratch, so the cleanup check
# can see that nothing is left behind.
export PKSM_TEST_WORK="$work"
# The cleanup is armed before the check, so the directory is removed even when
# the check refuses to use it.
trap 'rm -rf -- "$work"' EXIT
if [[ -L "$work" ]]; then
    printf 'error: test scratch directory must not be a symbolic link\n' >&2
    exit 1
fi

scenarios=(healthy both fallback wrap mixed mixed-last partial international japanese neither
           partial-international partial-japanese section4-missing overflow carry checksum payload
           short long signature signature-last id id-high duplicate duplicate-last)
for id in {0..13}; do scenarios+=("tail-$id"); done

"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    src/main.cpp src/frlg_save.cpp src/frlg_report.cpp src/frlg_content.cpp src/frlg_pokemon.cpp src/frlg_content_report.cpp src/frlg_text.cpp src/frlg_write.cpp -o "$work/frlg-inspect.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    tests/frlg_content_report_test.cpp src/frlg_content_report.cpp src/frlg_pokemon.cpp src/frlg_text.cpp -o "$work/frlg-content-report-test.exe"
"$work/frlg-content-report-test.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    tests/frlg_text_test.cpp src/frlg_text.cpp -o "$work/frlg-text-test.exe"
"$work/frlg-text-test.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    tests/frlg_report_test.cpp src/frlg_save.cpp src/frlg_report.cpp -o "$work/frlg-report-test.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    tests/frlg_write_test.cpp src/frlg_write.cpp src/frlg_save.cpp src/frlg_content.cpp \
    -o "$work/frlg-write-test.exe"
"$work/frlg-write-test.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    tests/frlg_console_write_test.cpp src/frlg_console_write.cpp src/frlg_money_editor.cpp \
    src/frlg_save.cpp src/frlg_content.cpp src/frlg_write.cpp \
    -o "$work/frlg-console-write-test.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    tests/frlg_pokemon_traits_test.cpp src/frlg_pokemon_traits.cpp \
    -o "$work/frlg-pokemon-traits-test.exe"
"$work/frlg-pokemon-traits-test.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    tests/frlg_pokemon_write_test.cpp src/frlg_pokemon_write.cpp src/frlg_legality.cpp \
    src/frlg_learnsets.cpp src/frlg_species_tables.cpp \
    -o "$work/frlg-pokemon-write-test.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    tests/frlg_pk3_test.cpp src/frlg_pokemon_write.cpp src/frlg_pokemon_traits.cpp \
    src/frlg_legality.cpp src/frlg_learnsets.cpp src/frlg_species_tables.cpp \
    -o "$work/frlg-pk3-test.exe"
"$work/frlg-pk3-test.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    tests/frlg_learnsets_test.cpp src/frlg_learnsets.cpp \
    -o "$work/frlg-learnsets-test.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    tests/frlg_pokemon_edit_test.cpp src/frlg_pokemon_edit.cpp src/frlg_pokemon_write.cpp \
    src/frlg_legality.cpp src/frlg_learnsets.cpp src/frlg_species_tables.cpp \
    src/frlg_pokemon_traits.cpp src/frlg_save.cpp src/frlg_content.cpp \
    -o "$work/frlg-pokemon-edit-test.exe"
"$work/frlg-pokemon-edit-test.exe"
"$work/frlg-learnsets-test.exe"
"$work/frlg-pokemon-write-test.exe"
"$work/frlg-console-write-test.exe"
"$work/frlg-report-test.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    tests/frlg_content_test.cpp src/frlg_content.cpp src/frlg_save.cpp -o "$work/frlg-content-test.exe"
"$work/frlg-content-test.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    tests/frlg_pokemon_test.cpp src/frlg_pokemon.cpp src/frlg_content.cpp src/frlg_save.cpp \
    -o "$work/frlg-pokemon-test.exe"
"$work/frlg-pokemon-test.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    tests/switch_readonly_test.cpp src/switch_readonly.cpp src/frlg_save.cpp \
    -o "$work/frlg-switch-test.exe"
"$work/frlg-switch-test.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic \
    tests/frlg_save_test.cpp -o "$work/frlg-fixture.exe"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic tests/frlg_content_fixture.cpp -o "$work/frlg-content-fixture.exe"
"$work/frlg-content-fixture.exe" > "$work/content.sav"
for scenario in "${scenarios[@]}"; do
    "$work/frlg-fixture.exe" "$scenario" > "$work/$scenario.sav"
done

passed=0
failed=0
usage=$'Usage: frlg-inspect [--report | --content] SAVE\n       frlg-inspect --set-money VALUE SAVE [OUTPUT]\n'
help=$'Usage: frlg-inspect [--report | --content] SAVE\n       frlg-inspect --set-money VALUE SAVE [OUTPUT]\nRead-only FRLG checksum inspection; game identity is not verified.\n--report prints the per-section status of both save blocks.\n--content prints decoded content of both save blocks.\n--set-money produces a verified copy with a new amount of money.\n'
note=$'Game identity, language, and content are not verified.\n'
no_slot=$'error: no slot has complete matching checksums\n'
slot_empty='checksummed sections: international 0/14, Japanese 0/14; save index unavailable; complete checksum ranges: none; section 4 checksums: not observed; invalid sector signature'
slot_both='checksummed sections: international 14/14, Japanese 14/14; save index 17; complete checksum ranges: international and Japanese (ambiguous); section 4 checksums: international and Japanese (ambiguous)'
slot_partial='checksummed sections: international 13/14, Japanese 13/14; save index 17; complete checksum ranges: none; section 4 checksums: international and Japanese (ambiguous)'
slot_neither="${slot_partial/section 4 checksums: international and Japanese (ambiguous)/section 4 checksums: neither}"

# Encode both byte streams and the exit status without writing capture files.
# The record separator is encoded together with stderr, so even embedded NULs
# and trailing newlines in either stream participate in the comparison.
capture() {
    {
        local err_and_status
        err_and_status="$( { local status=0; "$work/frlg-inspect.exe" "$@" 2>&1 >&3 || status=$?;
            printf '\036%s' "$status"; } | base64 | tr -d '\r\n')"
        printf '\037%s' "$err_and_status"
    } 3>&1 | base64 | tr -d '\r\n'
}

run_test() {
    local name="$1" expected_status="$2" expected_out="$3" expected_err="$4" input="$5"
    shift 5
    local expected actual bad=0
    expected="$( { printf '%s\037' "$expected_out";
        printf '%s\036%s' "$expected_err" "$expected_status" | base64 | tr -d '\r\n';
    } | base64 | tr -d '\r\n')"
    actual="$(capture "$@")"
    if [[ "$actual" != "$expected" ]]; then
        printf 'FAIL %s: exit status or exact stdout/stderr differs\n' "$name"
        bad=1
    fi
    if [[ -n "$input" ]] && ! cmp -s "$input" <("$work/frlg-fixture.exe" "$name"); then
        printf 'FAIL %s: synthetic input changed\n' "$name"
        bad=1
    fi
    if (( bad == 0 )); then
        printf 'PASS %s\n' "$name"
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
}

fixture_test() {
    local name="$1" status="$2" out="$3" err="$4"
    local input="$work/$name.sav"
    run_test "$name" "$status" "$out" "$err" "$input" "$input"
}

run_test help 0 "$help" '' '' --help
run_test no-arguments 2 '' "$usage" ''
run_test extra-arguments 2 '' "$usage" '' one two
run_test missing-file 1 '' $'error: cannot read input file\n' '' "$work/missing.sav"
fixture_test healthy 0 "Slot 0: $slot_both"$'\n'"Slot 1: $slot_empty"$'\n'"$note" ''
report_header=$'FRLG save report (read-only; game identity and content are not verified)\nBlock 0\n  save index: 17\n'
report_tail=$'  warnings: none\nBlock 1\n  save index: unavailable\n  checksummed sections: international 0/14, Japanese 0/14\n  warnings: invalid sector signature\n'
report_healthy="$report_header"$'  checksummed sections: international 14/14, Japanese 14/14\n'
report_neither="$report_header"$'  checksummed sections: international 13/14, Japanese 13/14\n'
for id in {0..13}; do
    report_healthy+="  section $id: sector $id, international and Japanese (ambiguous)"$'\n'
    if (( id == 4 )); then
        report_neither+="  section $id: sector $id, neither"$'\n'
    else
        report_neither+="  section $id: sector $id, international and Japanese (ambiguous)"$'\n'
    fi
done
report_healthy+="$report_tail"
report_neither+="$report_tail"
run_test report-healthy 0 "$report_healthy" '' '' --report "$work/healthy.sav"
run_test report-neither 1 "$report_neither" "$no_slot" '' --report "$work/neither.sav"
run_test report-missing-path 2 '' "$usage" '' --report
run_test content-missing-path 2 '' "$usage" '' --content
run_test modes-conflict 2 '' "$usage" '' --report --content
run_test modes-conflict-reverse 2 '' "$usage" '' --content --report
content_expected=$'Save content (read-only; game identity, language and revision are not verified)\nBlock 0\ncontent: available\nplayer name: つゃカ [jp]\nplayer name codes: 12 34 56\ngender: female\ntrainer id: 12345 secret 6789\nplay time: 3h 25m 10s\nmoney: 12345\nparty: 1\npokedex owned: 12 seen: 30\nboxes occupied: 1 0 0 0 0 0 0 0 0 0 0 0 0 0\nparty 1: species 25 level 5 hp 12/20 nickname 　　　　　　　　　　 checksum ok\nbox 1 slot 1: species 94 nickname 　　　　　　　　　　 checksum ok\nSave content (read-only; game identity, language and revision are not verified)\nBlock 1\ncontent: unavailable (Missing section)\n'
run_test content-fixture 0 "$content_expected" '' '' --content "$work/content.sav"
if ! cmp -s "$work/content.sav" <("$work/frlg-content-fixture.exe"); then
    printf 'FAIL content-fixture: synthetic input changed\n'
    failed=$((failed + 1))
fi
run_test content-no-complete 1 $'Save content (read-only; game identity, language and revision are not verified)\nBlock 0\ncontent: unavailable (Section checksum failed)\nSave content (read-only; game identity, language and revision are not verified)\nBlock 1\ncontent: unavailable (Missing section)\n' "$no_slot" '' --content "$work/neither.sav"
fixture_test both 0 "Slot 0: $slot_both"$'\n'"Slot 1: ${slot_both/save index 17/save index 18}"$'\n'"$note" ''
fixture_test fallback 0 "Slot 0: $slot_partial; invalid sector signature"$'\n'"Slot 1: ${slot_both/save index 17/save index 18}"$'\n'"$note" ''
fixture_test wrap 0 "Slot 0: ${slot_both/save index 17/save index 4294967295}"$'\n'"Slot 1: ${slot_both/save index 17/save index 0}"$'\n'"$note" ''
fixture_test mixed 0 "Slot 0: ${slot_both/save index 17/save index mixed (possible partial or link save)}"$'\n'"Slot 1: $slot_empty"$'\n'"$note" ''
fixture_test mixed-last 0 "Slot 0: ${slot_both/save index 17/save index mixed (possible partial or link save)}"$'\n'"Slot 1: $slot_empty"$'\n'"$note" ''
fixture_test partial 1 "Slot 0: ${slot_partial/save index 17/save index mixed (possible partial or link save)}"$'\n'"Slot 1: $slot_empty"$'\n'"$note" "$no_slot"
fixture_test international 0 $'Slot 0: checksummed sections: international 14/14, Japanese 13/14; save index 17; complete checksum ranges: international only; section 4 checksums: international only\n'"Slot 1: $slot_empty"$'\n'"$note" ''
fixture_test japanese 0 $'Slot 0: checksummed sections: international 13/14, Japanese 14/14; save index 17; complete checksum ranges: Japanese only; section 4 checksums: Japanese only\n'"Slot 1: $slot_empty"$'\n'"$note" ''
fixture_test neither 1 "Slot 0: $slot_neither"$'\n'"Slot 1: $slot_empty"$'\n'"$note" "$no_slot"
fixture_test partial-international 1 $'Slot 0: checksummed sections: international 13/14, Japanese 12/14; save index 17; complete checksum ranges: none; section 4 checksums: international only\n'"Slot 1: $slot_empty"$'\n'"$note" "$no_slot"
fixture_test partial-japanese 1 $'Slot 0: checksummed sections: international 12/14, Japanese 13/14; save index 17; complete checksum ranges: none; section 4 checksums: Japanese only\n'"Slot 1: $slot_empty"$'\n'"$note" "$no_slot"
fixture_test section4-missing 1 "Slot 0: ${slot_partial/section 4 checksums: international and Japanese (ambiguous)/section 4 checksums: not observed}; invalid sector signature"$'\n'"Slot 1: $slot_empty"$'\n'"$note" "$no_slot"
fixture_test overflow 0 "Slot 0: $slot_both"$'\n'"Slot 1: $slot_empty"$'\n'"$note" ''
fixture_test carry 0 "Slot 0: $slot_both"$'\n'"Slot 1: $slot_empty"$'\n'"$note" ''
fixture_test checksum 1 "Slot 0: $slot_partial"$'\n'"Slot 1: $slot_empty"$'\n'"$note" "$no_slot"
fixture_test payload 1 "Slot 0: $slot_partial"$'\n'"Slot 1: $slot_empty"$'\n'"$note" "$no_slot"
fixture_test short 1 '' $'error: expected exactly 131072 bytes\n'
fixture_test long 1 '' $'error: expected exactly 131072 bytes\n'
for name in signature signature-last; do
    fixture_test "$name" 1 "Slot 0: $slot_partial; invalid sector signature"$'\n'"Slot 1: $slot_empty"$'\n'"$note" "$no_slot"
done
for name in id id-high; do
    fixture_test "$name" 1 "Slot 0: $slot_partial; section ID out of range"$'\n'"Slot 1: $slot_empty"$'\n'"$note" "$no_slot"
done
for name in duplicate duplicate-last; do
    fixture_test "$name" 1 "Slot 0: $slot_partial; duplicate section ID"$'\n'"Slot 1: $slot_empty"$'\n'"$note" "$no_slot"
done
for id in {0..13}; do
    expected_slot="$slot_partial"
    if (( id == 4 )); then expected_slot="$slot_neither"; fi
    fixture_test "tail-$id" 1 "Slot 0: $expected_slot"$'\n'"Slot 1: $slot_empty"$'\n'"$note" "$no_slot"
done
money_out=$'block edited: 0\nmoney: 327686 -> 5000\nbytes changed: 5\nverification: the copy is complete and reads back the new amount\n'
run_test set-money 0 "$money_out" '' "$work/healthy.sav" --set-money 5000 "$work/healthy.sav"
run_test set-money-argument 2 '' "$usage" '' --set-money abc "$work/healthy.sav"
printf '%s passed, %s failed\n' "$passed" "$failed"
[[ "$failed" -eq 0 ]]
