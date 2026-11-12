#pragma once

#include <cstdint>

namespace pksm {

// The growth rate of a species. The values are the ones the game uses, so a
// record written with them keeps the meaning the game gives it.
enum class GrowthRate : std::uint8_t {
    medium_fast = 0,
    erratic = 1,
    fluctuating = 2,
    medium_slow = 3,
    fast = 4,
    slow = 5,
    // A species the tables do not cover has no rate.
    none = 255,};

// The gender ratio of a species, as the threshold the game stores: the
// Pokemon is female when the low byte of its personality value is below it.
enum class GenderRatio : std::uint8_t {
    always_male = 0,
    one_in_eight = 31,
    one_in_four = 63,
    equal = 127,
    always_female = 254,
    genderless = 255,
    none = 255
};

// The growth rate of a species, by its number in the game. A species that the
// tables do not cover has no rate.
GrowthRate frlg_growth_rate(std::uint16_t species) noexcept;

// The gender ratio of a species, by its number in the game.
GenderRatio frlg_gender_ratio(std::uint16_t species) noexcept;

// The base health of a species, by its number in the game. It is what the
// game uses to compute the health of a Pokemon.
std::uint8_t frlg_base_hp(std::uint16_t species) noexcept;

// The experience a Pokemon of that growth rate has when it reaches the level.
// The level goes from 1 to 100; a level outside that range gives 0.
std::uint32_t frlg_exp_for_level(GrowthRate rate, std::uint8_t level) noexcept;

// The level that matches that experience. An experience below the first level
// gives 1.
std::uint8_t frlg_level_for_exp(GrowthRate rate, std::uint32_t exp) noexcept;

}  // namespace pksm
