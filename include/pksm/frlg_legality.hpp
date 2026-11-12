#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pksm {

// The reasons a record may not be one the game could have produced.
enum class LegalityIssue : std::uint8_t {
    // The personality value is not one a breeding could have produced: the
    // remainder of dividing it by one hundred is above thirty one.
    personality_not_from_a_breeding,
    // A move the species cannot learn at the level it has.
    move_not_learnable,
    // The experience does not match the level the record says.
    experience_not_of_the_level,
    // An effort value above the limit of the game.
    effort_out_of_range,
    // An individual value above the limit of the game.
    individual_value_out_of_range,
};

// What a record has that the game could not have produced.
struct LegalityReport {
    bool legal = false;               // no issue was found
    std::vector<LegalityIssue> issues;
    std::uint16_t offending_move = 0; // the move of the move issue, zero if none
    std::uint8_t level = 0;           // the level the record has
    std::uint8_t personality = 0;      // the remainder of dividing the personality by one hundred
};

// Whether the species can have that move at that level: because it learns it
// when it goes up a level up to that level, because it is one of the moves a
// hatchling starts with, or because a machine the species can learn teaches
// it.
bool frlg_species_can_have_move(std::uint16_t species, std::uint8_t level,
                                std::uint16_t move) noexcept;

// Checks a record of a Pokemon against what the game allows: the experience
// against its level, the effort values, the individual values, the moves the
// species can learn and the personality value a breeding could have produced.
LegalityReport check_frlg_legality(const std::uint8_t* record, std::size_t length);

}  // namespace pksm
