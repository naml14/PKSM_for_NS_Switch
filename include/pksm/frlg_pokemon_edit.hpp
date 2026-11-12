#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pksm/frlg_pokemon_write.hpp"

namespace pksm {

enum class PokemonPlace { box, party };

struct PokemonEditRequest {
    PokemonPlace place = PokemonPlace::box;
    std::size_t box = 0;
    std::size_t slot = 0;
    std::size_t party = 0;
    PokemonEdit edit;
};

struct PokemonEditResult {
    bool ok = false;
    std::string reason;
    std::vector<std::uint8_t> image;
    std::uint8_t old_level = 0;
    std::uint8_t new_level = 0;
    std::uint32_t old_pid = 0;
    std::uint32_t new_pid = 0;
    std::size_t changed_bytes = 0;
};

PokemonEditResult edit_frlg_pokemon(const std::vector<std::uint8_t>& image,
                                     std::size_t block,
                                     const PokemonEditRequest& request);

}  // namespace pksm
