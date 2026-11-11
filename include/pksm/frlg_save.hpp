#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pksm {

constexpr std::size_t frlg_image_size = 128 * 1024;
constexpr std::size_t frlg_section_count = 14;

struct SectionInspection {
    bool present = false;
    bool checksum_international = false;
    bool checksum_japanese = false;
    bool duplicate = false;
    std::size_t sector_position = 0;
};

// Checksum-range matches do not establish game identity, language, or save validity.
struct SlotInspection {
    std::array<SectionInspection, frlg_section_count> sections{};
    std::size_t international_sections = 0;
    std::size_t japanese_sections = 0;
    // Section 4 possibilities are reported even when the slot is incomplete.
    bool section_four_observed = false;
    bool section_four_international = false;
    bool section_four_japanese = false;
    bool invalid_signature = false;
    bool section_id_out_of_range = false;
    bool duplicate_section_id = false;
    bool has_save_index = false;
    bool mixed_save_indices = false;
    // Meaningful only when has_save_index is true and mixed_save_indices is false.
    std::uint32_t save_index = 0;
};

struct SaveInspection {
    bool supported_size = false;
    std::array<SlotInspection, 2> slots{};
};

// Inspects the supplied bytes without modifying them or inferring game identity.
SaveInspection inspect_frlg_save(const std::vector<std::uint8_t>& image);

// The number of bytes a section's checksum covers, for the international or the
// Japanese range.
std::size_t frlg_section_used_length(std::size_t section_id, bool international);

// The checksum of the sector that starts at sector_base, over used_length
// bytes. The reader and any writer use the same function, so they cannot drift
// apart.
std::uint16_t frlg_sector_checksum(const std::vector<std::uint8_t>& image,
                                    std::size_t sector_base, std::size_t used_length);

} // namespace pksm
