#include "pksm/frlg_write.hpp"

#include "pksm/frlg_save.hpp"

#include <utility>

namespace pksm {
namespace {
// The money is a 32 bit value inside SaveBlock1, at this offset.
constexpr std::size_t money_offset_in_block1 = 0x290;
// The key that obfuscates the money is a 32 bit value inside SaveBlock2.
constexpr std::size_t key_offset_in_block2 = 0xF20;
// SaveBlock1 is built from the sections 1 to 4, each this long.
constexpr std::size_t block1_section_length = 0xF80;
// The section of SaveBlock1 that holds the money, and of SaveBlock2 the key.
constexpr std::size_t money_section = 1;
constexpr std::size_t key_section = 0;
// Every sector is this long and its checksum sits here.
constexpr std::size_t sector_length = 0x1000;
constexpr std::size_t sector_checksum_offset = 0xFF6;

std::uint32_t read32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void write32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}
}  // namespace

BlockSelection select_frlg_block(const std::vector<std::uint8_t>& image) {
    BlockSelection selection;
    if (image.size() != frlg_image_size) {
        selection.reason = "Unsupported image size";
        return selection;
    }
    const auto inspection = inspect_frlg_save(image);

    // The game loads the block with the higher save counter, so that is the one
    // to edit. Two blocks with the same counter are refused: the game would pick
    // one of them and the edit could be ignored.
    std::size_t chosen = 0;
    bool have_chosen = false;
    for (std::size_t i = 0; i < inspection.slots.size(); ++i) {
        const auto& slot = inspection.slots[i];
        if (!slot.has_save_index || slot.mixed_save_indices) continue;
        if (slot.international_sections != frlg_section_count &&
            slot.japanese_sections != frlg_section_count) continue;
        if (!have_chosen) {
            chosen = i;
            have_chosen = true;
            continue;
        }
        if (slot.save_index == inspection.slots[chosen].save_index) {
            selection.reason = "Ambiguous block counter";
            return selection;
        }
        if (slot.save_index > inspection.slots[chosen].save_index) chosen = i;
    }
    if (!have_chosen) {
        selection.reason = "No block with a complete matching checksum";
        return selection;
    }
    selection.ok = true;
    selection.block = chosen;
    return selection;
}

MoneyEdit set_frlg_money(const std::vector<std::uint8_t>& original, std::uint32_t value) {
    MoneyEdit edit;
    if (original.size() != frlg_image_size) {
        edit.reason = "Unsupported image size";
        return edit;
    }
    if (value > money_maximum) {
        edit.reason = "The amount is above the maximum the game accepts";
        return edit;
    }
    const auto inspection = inspect_frlg_save(original);
    const auto selection = select_frlg_block(original);
    if (!selection.ok) {
        edit.reason = selection.reason;
        return edit;
    }
    const std::size_t chosen = selection.block;

    const auto& sections = inspection.slots[chosen].sections;
    const auto block_base = chosen * frlg_section_count * sector_length;
    const auto money_sector = block_base + sections[money_section].sector_position * sector_length;
    const auto money_at = money_sector + (money_offset_in_block1 % block1_section_length);
    const auto key_sector = block_base + sections[key_section].sector_position * sector_length;
    const auto key = read32(original, key_sector + key_offset_in_block2);

    edit.block = chosen;
    edit.old_money = read32(original, money_at) ^ key;
    edit.new_money = value;

    auto copy = original;
    write32(copy, money_at, value ^ key);

    // The section that changed has to carry a fresh checksum, or the game
    // rejects the save.
    const auto used = frlg_section_used_length(money_section, sections[money_section].checksum_international);
    const auto checksum = frlg_sector_checksum(copy, money_sector, used);
    copy[money_sector + sector_checksum_offset] = static_cast<std::uint8_t>(checksum);
    copy[money_sector + sector_checksum_offset + 1] = static_cast<std::uint8_t>(checksum >> 8);

    // Verify the copy before it is handed over.
    const auto verified = inspect_frlg_save(copy);
    const auto& slot = verified.slots[chosen];
    if (slot.international_sections != frlg_section_count &&
        slot.japanese_sections != frlg_section_count) {
        edit.reason = "Verification failed";
        return edit;
    }
    if ((read32(copy, money_at) ^ key) != value) {
        edit.reason = "Verification failed";
        return edit;
    }
    const std::size_t permitted[] = {money_at, money_at + 1, money_at + 2, money_at + 3,
                                     money_sector + sector_checksum_offset,
                                     money_sector + sector_checksum_offset + 1};
    for (std::size_t i = 0; i < copy.size(); ++i) {
        if (copy[i] == original[i]) continue;
        bool allowed = false;
        for (const auto offset : permitted) {
            if (i == offset) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            edit.reason = "Verification failed";
            return edit;
        }
        edit.changed_offsets.push_back(i);
    }
    edit.changed_bytes = edit.changed_offsets.size();
    edit.image = std::move(copy);
    edit.ok = true;
    return edit;
}

}  // namespace pksm
