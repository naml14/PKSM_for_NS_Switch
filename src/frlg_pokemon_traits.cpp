#include "pksm/frlg_pokemon_traits.hpp"

#include <cstdint>

namespace pksm {

PokemonTraits frlg_traits_from_pid(std::uint32_t pid, std::uint32_t trainer_id,
                                  std::uint8_t gender_ratio) noexcept {
    // GetNatureFromPersonality and GetGenderFromSpeciesAndPersonality:
    // https://raw.githubusercontent.com/pret/pokefirered/master/src/pokemon.c
    // https://raw.githubusercontent.com/pret/pokefirered/master/include/constants/pokemon.h
    PokemonTraits traits;
    traits.nature = static_cast<std::uint8_t>(pid % 25);
    if (gender_ratio == 255) traits.gender = PokemonGender::genderless;
    else if (gender_ratio == 254) traits.gender = PokemonGender::female;
    else if (gender_ratio == 0) traits.gender = PokemonGender::male;
    else traits.gender = (pid & 0xffu) < gender_ratio ? PokemonGender::female : PokemonGender::male;

    // GET_SHINY_VALUE and IsShinyOtIdPersonality use both halves of each ID.
    // https://raw.githubusercontent.com/pret/pokefirered/master/include/pokemon.h
    // https://raw.githubusercontent.com/pret/pokefirered/master/src/pokemon.c
    const auto shiny_value = (trainer_id >> 16) ^ (trainer_id & 0xffffu) ^
                             (pid >> 16) ^ (pid & 0xffffu);
    traits.shiny = shiny_value < 8;
    return traits;
}

const char* frlg_nature_name(std::uint8_t nature) noexcept {
    // https://raw.githubusercontent.com/pret/pokefirered/master/include/constants/pokemon.h
    static constexpr const char* names[] = {
        "Hardy", "Lonely", "Brave", "Adamant", "Naughty", "Bold", "Docile", "Relaxed",
        "Impish", "Lax", "Timid", "Hasty", "Serious", "Jolly", "Naive", "Modest", "Mild",
        "Quiet", "Bashful", "Rash", "Calm", "Gentle", "Sassy", "Careful", "Quirky"
    };
    return nature < 25 ? names[nature] : "";
}

bool frlg_find_pid(const PokemonTraitRequest& request, std::uint32_t& pid) noexcept {
    if (request.nature >= 25) return false;
    if (request.gender != PokemonGender::male && request.gender != PokemonGender::female &&
        request.gender != PokemonGender::genderless) return false;
    if ((request.gender_ratio == 0 && request.gender != PokemonGender::male) ||
        (request.gender_ratio == 254 && request.gender != PokemonGender::female) ||
        (request.gender_ratio == 255 && request.gender != PokemonGender::genderless) ||
        (request.gender_ratio != 255 && request.gender == PokemonGender::genderless)) return false;

    const auto ot_xor = (request.trainer_id >> 16) ^ (request.trainer_id & 0xffffu);
    for (std::uint32_t low = 0; low <= 0xffffu; ++low) {
        if (frlg_traits_from_pid(low, request.trainer_id, request.gender_ratio).gender != request.gender)
            continue;
        if (request.shiny) {
            // For a fixed low half, exactly eight high halves yield shiny values 0..7.
            for (std::uint32_t value = 0; value < 8; ++value) {
                const auto high = ot_xor ^ low ^ value;
                const auto candidate = (high << 16) | low;
                const auto traits = frlg_traits_from_pid(candidate, request.trainer_id, request.gender_ratio);
                if (traits.nature == request.nature && traits.gender == request.gender && traits.shiny) {
                    pid = candidate;
                    return true;
                }
            }
        } else {
            // 65536 == 11 (mod 25), and 16 is the inverse of 11 modulo 25.
            const auto residue = ((request.nature + 25u - low % 25u) * 16u) % 25u;
            for (std::uint32_t high = residue; high <= 0xffffu; high += 25u) {
                const auto candidate = (high << 16) | low;
                const auto traits = frlg_traits_from_pid(candidate, request.trainer_id, request.gender_ratio);
                if (traits.nature == request.nature && traits.gender == request.gender && !traits.shiny) {
                    pid = candidate;
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace pksm
