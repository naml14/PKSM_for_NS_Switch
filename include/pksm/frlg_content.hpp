#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pksm {

struct FrlgContent {
    bool available = false;
    std::string unavailable_reason;
    // Raw Gen 3 codes; language-specific glyph decoding is not verified.
    std::array<std::uint8_t, 8> player_name{};
    std::uint8_t gender = 0;
    std::uint16_t trainer_id = 0;
    std::uint16_t secret_id = 0;
    std::uint16_t play_hours = 0;
    std::uint8_t play_minutes = 0;
    std::uint8_t play_seconds = 0;
    std::uint32_t money = 0;
    std::uint8_t party_count = 0;
    std::uint32_t dex_owned = 0;
    std::uint32_t dex_seen = 0;
    std::array<std::uint32_t, 14> box_occupied{};
    std::vector<std::uint8_t> save_block1;
    std::vector<std::uint8_t> save_block2;
    std::vector<std::uint8_t> storage;
};

// Reads one block (0 or 1) of a 128 KiB FRLG image. Never modifies the image.
FrlgContent read_frlg_content(const std::vector<std::uint8_t>& image, std::size_t block);

}  // namespace pksm
