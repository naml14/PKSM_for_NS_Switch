#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pksm {

// The result of editing one value of a save block. The copy is only returned
// when the edit was verified, so a caller can trust every field of it.
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

// Sets the money of the block the game will load, in a copy of the image. The
// original is never modified. The copy is returned only after it was verified:
// the checksum of the affected section is recomputed, the copy is inspected
// again, the money is read back and every byte that differs from the original
// has to fall inside the changed field and the recomputed checksum.
MoneyEdit set_frlg_money(const std::vector<std::uint8_t>& original, std::uint32_t value);

}  // namespace pksm
