#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${CXX:=c++}"
export CXX

# The scratch root and every directory above it must be private and owned by a
# trusted principal, the invoking user or root. A directory another local user
# can write, or owns, lets them rename the scratch root and substitute an
# attacker-owned tree whose run directory the suite then compiles and runs with
# the user's privileges. The owner can widen the permissions at any time, so
# ownership decides trust and not the current mode alone.
uid="$(id -u)"
require_trusted_chain() {
    local dir="$1" mode owner parent
    while :; do
        mode="$(stat -c %a "$dir" 2>/dev/null || true)"
        owner="$(stat -c %u "$dir" 2>/dev/null || true)"
        if [[ ! "$mode" =~ ^[0-7]+$ || ! "$owner" =~ ^[0-9]+$ ]]; then
            printf 'FAIL: cannot read the permissions of %s\n' "$dir" >&2
            return 1
        fi
        if [[ "$owner" != "$uid" && "$owner" != 0 ]]; then
            printf 'FAIL: %s is owned by another user\n' "$dir" >&2
            return 1
        fi
        if (( (8#$mode & 022) != 0 && (8#$mode & 01000) == 0 )); then
            printf 'FAIL: %s is writable by other users\n' "$dir" >&2
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
    printf 'FAIL: test scratch root must be a real directory\n' >&2
    exit 1
fi
# The scratch root must stay unwritable for anyone but its owner and must
# belong to the invoking user, otherwise another local user could replace the
# probe directory with a symbolic link and redirect what this check writes.
chmod 700 "$root" 2>/dev/null || true
root_owner="$(stat -c %u "$root")"
if [[ "$root_owner" != "$uid" ]]; then
    printf 'FAIL: test scratch root must be owned by the invoking user\n' >&2
    exit 1
fi
scratch_mode="$(stat -c %a "$root")"
if [[ ! "$scratch_mode" =~ ^[0-7]+$ ]] || (( 8#$scratch_mode & 022 )); then
    printf 'FAIL: test scratch root must not be group or world writable\n' >&2
    exit 1
fi
# The directories above the scratch root must be trusted as well.
if ! require_trusted_chain "$(dirname "$root")"; then
    exit 1
fi
set -o noclobber
probe="$(mktemp -d "$root/cleanup-check.XXXXXXXX")"
if [[ -L "$probe" ]]; then
    printf 'FAIL: test scratch directory must not be a symbolic link\n' >&2
    exit 1
fi
trap 'rm -rf -- "$probe"' EXIT
printf 'preserve unrelated scratch\n' > "$probe/sentinel"
entries_before="$(find "$root" -mindepth 1 -maxdepth 1 -printf '%f\n' | LC_ALL=C sort)"

if ! output="$(bash tests/run.sh 2>&1)"; then
    printf 'FAIL: synthetic suite failed\n%s\n' "$output" >&2
    exit 1
fi
entries_after="$(find "$root" -mindepth 1 -maxdepth 1 -printf '%f\n' | LC_ALL=C sort)"
if [[ "$entries_before" != "$entries_after" ]]; then
    printf 'FAIL: the synthetic suite left temporary entries behind\n' >&2
    exit 1
fi
if ! cmp -s "$probe/sentinel" <(printf 'preserve unrelated scratch\n'); then
    printf 'FAIL: the synthetic suite changed unrelated scratch data\n' >&2
    exit 1
fi
printf '%s\n' "${output##*$'\n'}"
printf 'PASS: the suite removes its own temporary files and preserves other scratch\n'
