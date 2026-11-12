#pragma once

#include <cstdint>

namespace pksm {

enum class PokemonGender : std::uint8_t { male = 0, female = 1, genderless = 2 };

struct PokemonTraits {
    std::uint8_t nature = 0;
    PokemonGender gender = PokemonGender::male;
    bool shiny = false;
};

// The ratio is the species' game byte: 0 male, 254 female, 255 genderless;
// otherwise the low personality byte is compared with the ratio.
PokemonTraits frlg_traits_from_pid(std::uint32_t pid, std::uint32_t trainer_id,
                                  std::uint8_t gender_ratio) noexcept;
const char* frlg_nature_name(std::uint8_t nature) noexcept;

struct PokemonTraitRequest {
    std::uint8_t nature = 0;
    PokemonGender gender = PokemonGender::male;
    bool shiny = false;
    std::uint8_t gender_ratio = 127;
    std::uint32_t trainer_id = 0;
    // A record that was bred takes the personality of a parent, so the
    // remainder of dividing the personality value by one hundred has to be at
    // most thirty one. The search refuses to return a value that a breeding
    // could not have produced.
    bool bred = false;
};

bool frlg_find_pid(const PokemonTraitRequest& request, std::uint32_t& pid) noexcept;

}  // namespace pksm
