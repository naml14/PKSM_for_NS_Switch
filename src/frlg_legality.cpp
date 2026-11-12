#include "pksm/frlg_legality.hpp"

#include "pksm/frlg_learnsets.hpp"
#include "pksm/frlg_species_tables.hpp"

#include <cstring>

namespace pksm {
namespace {
// The layout of a record of the third generation and the place of each
// canonical substructure.
//
// pret/pokefirered include/pokemon.h, struct BoxPokemon, and src/pokemon.c,
// the function GetSubstruct, which decides where the four canonical
// substructures are stored from the personality value modulo twenty four.
// https://raw.githubusercontent.com/pret/pokefirered/master/include/pokemon.h
// https://raw.githubusercontent.com/pret/pokefirered/master/src/pokemon.c
constexpr std::size_t box_record_length = 0x50;
constexpr std::size_t personality_offset = 0x00;
constexpr std::size_t ot_id_offset = 0x04;
constexpr std::size_t secure_offset = 0x20;
constexpr std::size_t secure_length = 48;
constexpr std::size_t substruct_length = 12;

constexpr std::uint8_t substruct_positions[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {0, 3, 2, 1},
    {1, 0, 2, 3}, {1, 0, 3, 2}, {2, 0, 1, 3}, {3, 0, 1, 2}, {2, 0, 3, 1}, {3, 0, 2, 1},
    {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3}, {3, 1, 0, 2}, {2, 3, 0, 1}, {3, 2, 0, 1},
    {1, 2, 3, 0}, {1, 3, 2, 0}, {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0}
};

std::uint16_t read16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8);
}

std::uint32_t read32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::size_t substruct_at(std::uint32_t personality, std::size_t canonical) {
    return substruct_positions[personality % 24][canonical] * substruct_length;
}

// The moves of a species that the game would let it have: the ones it learns
// when it goes up a level up to that level, the ones a machine teaches and
// the ones a hatchling starts with.
bool species_can_have_move(std::uint16_t species, std::uint8_t level,
                           std::uint16_t move) {
    for (std::size_t i = 0; i < frlg_level_up_move_count(species); ++i) {
        const auto& learned = frlg_level_up_move(species, i);
        if (learned.move == move && learned.level != 0 && learned.level <= level) return true;
    }
    for (std::size_t i = 0; i < frlg_egg_move_count(species); ++i)
        if (frlg_egg_move(species, i) == move) return true;
    // A machine the species can learn teaches the move it carries.
    for (std::uint8_t machine = 1; machine <= 58; ++machine) {
        if (frlg_tmhm_move(machine) != move) continue;
        if (frlg_learns_tmhm(species, machine)) return true;
    }
    return false;
}
}  // namespace

bool frlg_species_can_have_move(std::uint16_t species, std::uint8_t level,
                                std::uint16_t move) noexcept {
    return species_can_have_move(species, level, move);
}

LegalityReport check_frlg_legality(const std::uint8_t* record, std::size_t length) {
    LegalityReport report;
    if (record == nullptr || length < box_record_length) return report;

    const auto personality = read32(record + personality_offset);
    const auto ot_id = read32(record + ot_id_offset);
    const auto key = personality ^ ot_id;
    std::uint8_t decrypted[secure_length];
    for (std::size_t i = 0; i < secure_length; ++i)
        decrypted[i] = static_cast<std::uint8_t>(
            record[secure_offset + i] ^ static_cast<std::uint8_t>(key >> (8 * (i % 4))));

    const auto species = read16(decrypted + substruct_at(personality, 0));
    if (species == 0) return report;

    const auto experience = read32(decrypted + substruct_at(personality, 0) + 4);
    const auto rate = frlg_growth_rate(species);
    const auto level = frlg_level_for_exp(rate, experience);
    report.level = level;
    report.personality = static_cast<std::uint8_t>(personality % 100);

    // The experience has to be the one of that level: at least the minimum of
    // the level and less than the minimum of the next one. A record can hold
    // any value in that range, not only the minimum.
    if (rate != GrowthRate::none && level < 100) {
        const auto minimum = frlg_exp_for_level(rate, level);
        const auto next = frlg_exp_for_level(rate, static_cast<std::uint8_t>(level + 1));
        if (experience < minimum || experience >= next)
            report.issues.push_back(LegalityIssue::experience_not_of_the_level);
    }
    if (report.personality > 31)
        report.issues.push_back(LegalityIssue::personality_not_from_a_breeding);

    const auto ev_base = substruct_at(personality, 2);
    const std::uint16_t effort[6] = {decrypted[ev_base + 0], decrypted[ev_base + 1],
                                      decrypted[ev_base + 2], decrypted[ev_base + 3],
                                      decrypted[ev_base + 4], decrypted[ev_base + 5]};
    std::uint16_t total = 0;
    for (const auto value : effort) {
        if (value > 252) report.issues.push_back(LegalityIssue::effort_out_of_range);
        total = static_cast<std::uint16_t>(total + value);
    }
    if (total > 510) report.issues.push_back(LegalityIssue::effort_out_of_range);

    const auto iv_base = substruct_at(personality, 3) + 4;
    const std::uint32_t bits = static_cast<std::uint32_t>(decrypted[iv_base]) |
                               (static_cast<std::uint32_t>(decrypted[iv_base + 1]) << 8) |
                               (static_cast<std::uint32_t>(decrypted[iv_base + 2]) << 16) |
                               (static_cast<std::uint32_t>(decrypted[iv_base + 3] & 0x3F) << 24);
    const std::uint8_t individual[6] = {
        static_cast<std::uint8_t>(bits & 0x1F), static_cast<std::uint8_t>((bits >> 5) & 0x1F),
        static_cast<std::uint8_t>((bits >> 10) & 0x1F), static_cast<std::uint8_t>((bits >> 15) & 0x1F),
        static_cast<std::uint8_t>((bits >> 20) & 0x1F), static_cast<std::uint8_t>((bits >> 25) & 0x1F)
    };
    for (const auto value : individual)
        if (value > 31) report.issues.push_back(LegalityIssue::individual_value_out_of_range);

    const auto moves_base = substruct_at(personality, 1);
    for (std::size_t slot = 0; slot < 4; ++slot) {
        const auto move = read16(decrypted + moves_base + slot * 2);
        if (move == 0) continue;
        if (!species_can_have_move(species, level, move)) {
            report.issues.push_back(LegalityIssue::move_not_learnable);
            if (report.offending_move == 0) report.offending_move = move;
        }
    }

    report.legal = report.issues.empty();
    return report;
}

}  // namespace pksm
