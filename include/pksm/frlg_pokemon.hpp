#pragma once

#include "pksm/frlg_content.hpp"

#include <array>
#include <cstdint>

namespace pksm {

enum class PokemonReadStatus {
    ok,
    empty_record,
    invalid_index,
    unavailable_content,
};

struct PokemonSummary {
    PokemonReadStatus status = PokemonReadStatus::unavailable_content;
    bool present = false;
    bool checksum_valid = false;
    std::uint16_t species = 0;
    std::uint32_t personality = 0;
    std::uint32_t ot_id = 0;
    std::array<std::uint8_t, 10> nickname{};
    std::uint8_t language = 0;
    std::uint8_t level = 0;
    std::uint16_t current_hp = 0;
    std::uint16_t max_hp = 0;
};

std::array<PokemonSummary, 6> read_frlg_party(const FrlgContent& content);
PokemonSummary read_frlg_box_mon(const FrlgContent& content, std::size_t box, std::size_t slot);

}  // namespace pksm
