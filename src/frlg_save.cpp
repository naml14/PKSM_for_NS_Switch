#include "pksm/frlg_save.hpp"

#include <array>

namespace pksm {
namespace {
constexpr std::size_t sector_size = 0x1000;
constexpr std::size_t section_count = 14;
constexpr std::size_t slot_size = section_count * sector_size;
constexpr std::uint32_t sector_signature = 0x08012025;
constexpr std::array<std::size_t, section_count> international_lengths = {
    0xF24, 0xF80, 0xF80, 0xF80, 0xEE8, 0xF80, 0xF80,
    0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0x7D0
};
constexpr std::size_t japanese_section_four_length = 0xEC0;

std::uint16_t read16(const std::vector<std::uint8_t>& image, std::size_t offset) {
    return static_cast<std::uint16_t>(image[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(image[offset + 1]) << 8);
}

std::uint32_t read32(const std::vector<std::uint8_t>& image, std::size_t offset) {
    return static_cast<std::uint32_t>(read16(image, offset)) |
           (static_cast<std::uint32_t>(read16(image, offset + 2)) << 16);
}

std::uint16_t checksum(const std::vector<std::uint8_t>& image,
                       std::size_t base, std::size_t length) {
    std::uint32_t sum = 0;
    for (std::size_t offset = 0; offset < length; offset += 4) {
        sum += read32(image, base + offset); // Unsigned arithmetic wraps at 32 bits.
    }
    return static_cast<std::uint16_t>(sum + (sum >> 16));
}

SlotInspection inspect_slot(const std::vector<std::uint8_t>& image, std::size_t slot) {
    SlotInspection result;
    std::array<bool, section_count> seen{};
    std::array<bool, section_count> international_matches{};
    std::array<bool, section_count> japanese_matches{};
    for (std::size_t sector = 0; sector < section_count; ++sector) {
        const std::size_t base = slot * slot_size + sector * sector_size;
        if (read32(image, base + 0xFF8) != sector_signature) {
            result.invalid_signature = true;
            continue;
        }
        const std::uint16_t id = read16(image, base + 0xFF4);
        if (id >= section_count) {
            result.section_id_out_of_range = true;
            continue;
        }
        auto& section = result.sections[id];
        if (seen[id]) {
            result.duplicate_section_id = true;
            section.duplicate = true;
        } else {
            section.present = true;
            section.sector_position = sector;
        }
        seen[id] = true;

        const std::uint32_t index = read32(image, base + 0xFFC);
        if (!result.has_save_index) {
            result.has_save_index = true;
            result.save_index = index;
        } else if (index != result.save_index) {
            result.mixed_save_indices = true;
        }

        const std::uint16_t stored = read16(image, base + 0xFF6);
        const bool international = checksum(image, base, international_lengths[id]) == stored;
        const bool japanese = id == 4
            ? checksum(image, base, japanese_section_four_length) == stored
            : international;
        // A duplicated section ID contributes to the same entry, so a section
        // matches a range when any of its sectors matches it.
        if (international) section.checksum_international = true;
        if (japanese) section.checksum_japanese = true;
        if (id == 4) {
            result.section_four_observed = true;
            result.section_four_international = result.section_four_international || international;
            result.section_four_japanese = result.section_four_japanese || japanese;
        }
        if (international && !international_matches[id]) {
            international_matches[id] = true;
            ++result.international_sections;
        }
        if (japanese && !japanese_matches[id]) {
            japanese_matches[id] = true;
            ++result.japanese_sections;
        }
    }
    return result;
}
} // namespace

SaveInspection inspect_frlg_save(const std::vector<std::uint8_t>& image) {
    SaveInspection result;
    if (image.size() != frlg_image_size) return result;
    result.supported_size = true;
    for (std::size_t slot = 0; slot < result.slots.size(); ++slot) {
        result.slots[slot] = inspect_slot(image, slot);
    }
    return result;
}

} // namespace pksm
