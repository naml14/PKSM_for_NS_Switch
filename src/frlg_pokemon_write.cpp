#include "pksm/frlg_pokemon_write.hpp"

#include "pksm/frlg_legality.hpp"
#include "pksm/frlg_pokemon_traits.hpp"
#include "pksm/frlg_species_tables.hpp"

#include <algorithm>
#include <cstring>

namespace pksm {
namespace {
// The layout of a record of the third generation.
//
// pret/pokefirered include/pokemon.h, struct BoxPokemon: the personality
// value, the identifier of the original trainer, the nickname, the language,
// the sanity flags, the name of the original trainer, the markings, the
// checksum and the four encrypted substructures.
// https://raw.githubusercontent.com/pret/pokefirered/master/include/pokemon.h
constexpr std::size_t box_record_length = 0x50;
constexpr std::size_t party_record_length = 0x64;
constexpr std::size_t personality_offset = 0x00;
constexpr std::size_t ot_id_offset = 0x04;
constexpr std::size_t nickname_offset = 0x08;
constexpr std::size_t checksum_offset = 0x1C;
constexpr std::size_t secure_offset = 0x20;
constexpr std::size_t secure_length = 48;
constexpr std::size_t substruct_length = 12;
constexpr std::size_t party_level_offset = 0x54;
constexpr std::size_t party_hp_offset = 0x56;
constexpr std::size_t party_max_hp_offset = 0x58;

// pret/pokefirered src/pokemon.c, the macro SUBSTRUCT_CASE and the function
// GetSubstruct: the personality value modulo twenty four decides where the
// four canonical substructures are stored. The first number of each row is
// the place of the canonical type zero, the one that holds the species, the
// experience and the friendship; the third is the place of the canonical type
// two, the one that holds the effort values; the fourth is the place of the
// canonical type three, the one that holds the individual values.
// https://raw.githubusercontent.com/pret/pokefirered/master/src/pokemon.c
constexpr std::uint8_t substruct_positions[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {0, 3, 2, 1},
    {1, 0, 2, 3}, {1, 0, 3, 2}, {2, 0, 1, 3}, {3, 0, 1, 2}, {2, 0, 3, 1}, {3, 0, 2, 1},
    {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3}, {3, 1, 0, 2}, {2, 3, 0, 1}, {3, 2, 0, 1},
    {1, 2, 3, 0}, {1, 3, 2, 0}, {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0}
};

// The limits of the effort values. pret/pokefirered include/constants/pokemon.h
// gives the total of five hundred and ten; the limit of two hundred and fifty
// two per stat is the one a legal record can hold.
constexpr std::uint16_t ev_total_limit = 510;
constexpr std::uint16_t ev_per_stat_limit = 252;

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

void write16(std::uint8_t* bytes, std::uint16_t value) {
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8);
}

void write32(std::uint8_t* bytes, std::uint32_t value) {
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8);
    bytes[2] = static_cast<std::uint8_t>(value >> 16);
    bytes[3] = static_cast<std::uint8_t>(value >> 24);
}

// The key of the encryption: each word of the secure data is xored with the
// personality value and with the identifier of the original trainer.
// pret/pokefirered src/pokemon.c, EncryptBoxMon and DecryptBoxMon.
std::uint32_t encryption_key(const std::uint8_t* record) {
    return read32(record + personality_offset) ^ read32(record + ot_id_offset);
}

void decrypt(const std::uint8_t* record, std::uint8_t* out) {
    const auto key = encryption_key(record);
    for (std::size_t i = 0; i < secure_length; ++i)
        out[i] = static_cast<std::uint8_t>(record[secure_offset + i] ^
                                            static_cast<std::uint8_t>(key >> (8 * (i % 4))));
}

void encrypt(const std::uint8_t* decrypted, std::uint8_t* out, std::uint32_t key) {
    for (std::size_t i = 0; i < secure_length; ++i)
        out[i] = static_cast<std::uint8_t>(decrypted[i] ^
                                           static_cast<std::uint8_t>(key >> (8 * (i % 4))));
}

// The checksum of a record: the sum of the words of the decrypted data.
// pret/pokefirered src/pokemon.c, CalculateBoxMonChecksum.
std::uint16_t record_checksum(const std::uint8_t* decrypted) {
    std::uint16_t sum = 0;
    for (std::size_t i = 0; i < secure_length; i += 2)
        sum = static_cast<std::uint16_t>(sum + read16(decrypted + i));
    return sum;
}

std::size_t substruct_at(std::uint32_t personality, std::size_t canonical) {
    return substruct_positions[personality % 24][canonical] * substruct_length;
}

std::uint16_t species_of(const std::uint8_t* decrypted, std::uint32_t personality) {
    return read16(decrypted + substruct_at(personality, 0));
}

std::uint32_t experience_of(const std::uint8_t* decrypted, std::uint32_t personality) {
    return read32(decrypted + substruct_at(personality, 0) + 4);
}

void set_experience(std::uint8_t* decrypted, std::uint32_t personality, std::uint32_t value) {
    write32(decrypted + substruct_at(personality, 0) + 4, value);
}

void read_evs(const std::uint8_t* decrypted, std::uint32_t personality, PokemonEv& evs) {
    const auto base = substruct_at(personality, 2);
    evs.hp = decrypted[base + 0];
    evs.attack = decrypted[base + 1];
    evs.defense = decrypted[base + 2];
    evs.speed = decrypted[base + 3];
    evs.special_attack = decrypted[base + 4];
    evs.special_defense = decrypted[base + 5];
}

void set_evs(std::uint8_t* decrypted, std::uint32_t personality, const PokemonEv& evs) {
    const auto base = substruct_at(personality, 2);
    decrypted[base + 0] = evs.hp;
    decrypted[base + 1] = evs.attack;
    decrypted[base + 2] = evs.defense;
    decrypted[base + 3] = evs.speed;
    decrypted[base + 4] = evs.special_attack;
    decrypted[base + 5] = evs.special_defense;
}

// The individual values live in thirty bits that start at the offset four of
// the canonical type three: the health takes the first five bits, the attack
// the next five, and so on. The two flags that follow them share the last
// byte, so only the thirty bits of the values are read and written.
//
// pret/pokefirered include/pokemon.h, struct PokemonSubstruct3: the comments
// of the game give the byte each field starts at, the health at 0x04, the
// attack at 0x04, the defence at 0x05, the speed at 0x05, the special attack
// at 0x05, the special defence at 0x06 and the flags at 0x07.
void read_ivs(const std::uint8_t* decrypted, std::uint32_t personality, PokemonIv& ivs) {
    const auto base = substruct_at(personality, 3) + 4;
    const std::uint32_t bits = static_cast<std::uint32_t>(decrypted[base]) |
                               (static_cast<std::uint32_t>(decrypted[base + 1]) << 8) |
                               (static_cast<std::uint32_t>(decrypted[base + 2]) << 16) |
                               (static_cast<std::uint32_t>(decrypted[base + 3] & 0x3F) << 24);
    ivs.hp = static_cast<std::uint8_t>(bits & 0x1F);
    ivs.attack = static_cast<std::uint8_t>((bits >> 5) & 0x1F);
    ivs.defense = static_cast<std::uint8_t>((bits >> 10) & 0x1F);
    ivs.speed = static_cast<std::uint8_t>((bits >> 15) & 0x1F);
    ivs.special_attack = static_cast<std::uint8_t>((bits >> 20) & 0x1F);
    ivs.special_defense = static_cast<std::uint8_t>((bits >> 25) & 0x1F);
}

void set_ivs(std::uint8_t* decrypted, std::uint32_t personality, const PokemonIv& ivs) {
    const auto base = substruct_at(personality, 3) + 4;
    std::uint32_t bits = static_cast<std::uint32_t>(decrypted[base]) |
                         (static_cast<std::uint32_t>(decrypted[base + 1]) << 8) |
                         (static_cast<std::uint32_t>(decrypted[base + 2]) << 16) |
                         (static_cast<std::uint32_t>(decrypted[base + 3] & 0x3F) << 24);
    bits = (bits & ~0x3FFFFFFFu) | (ivs.hp & 0x1F) | ((ivs.attack & 0x1F) << 5) |
           ((ivs.defense & 0x1F) << 10) | ((ivs.speed & 0x1F) << 15) |
           ((ivs.special_attack & 0x1F) << 20) | ((ivs.special_defense & 0x1F) << 25);
    decrypted[base] = static_cast<std::uint8_t>(bits);
    decrypted[base + 1] = static_cast<std::uint8_t>(bits >> 8);
    decrypted[base + 2] = static_cast<std::uint8_t>(bits >> 16);
    decrypted[base + 3] = static_cast<std::uint8_t>((decrypted[base + 3] & 0xC0) | (bits >> 24));
}

// The origin of a record lives in the canonical type three: the place and the
// level it was found at, the game it came from, the ball and the gender of
// the original trainer. pret/pokefirered include/pokemon.h, struct
// PokemonSubstruct3: the met location at 0x01, the met level and the met game
// at 0x02, the ball and the gender of the trainer at 0x03, and the flag of an
// egg at 0x07.
void read_origin(const std::uint8_t* decrypted, std::uint32_t personality,
                 PokemonOrigin& origin) {
    const auto base = substruct_at(personality, 3);
    origin.egg = ((decrypted[base + 7] >> 6) & 1) != 0;
    origin.location = decrypted[base + 1];
    origin.level = static_cast<std::uint8_t>(decrypted[base + 2] & 0x7F);
    origin.game = static_cast<std::uint8_t>((decrypted[base + 2] >> 7) & 0x0F);
    origin.ball = static_cast<std::uint8_t>(decrypted[base + 3] & 0x0F);
    origin.trainer_female = ((decrypted[base + 3] >> 4) & 1) != 0;
}

void set_origin(std::uint8_t* decrypted, std::uint32_t personality,
                const PokemonOrigin& origin) {
    const auto base = substruct_at(personality, 3);
    // The flag of an egg shares its byte with the ability, and the ability is
    // left as it was.
    decrypted[base + 7] = static_cast<std::uint8_t>(
        (decrypted[base + 7] & 0xBF) | (origin.egg ? 0x40 : 0x00));
    decrypted[base + 1] = origin.location;
    decrypted[base + 2] = static_cast<std::uint8_t>(
        (origin.level & 0x7F) | ((origin.game & 0x0F) << 7));
    decrypted[base + 3] = static_cast<std::uint8_t>(
        (origin.ball & 0x0F) | (origin.trainer_female ? 0x10 : 0x00));
}

// A Pokemon only keeps the moves it can have at its level. When the level
// goes down, the moves the species no longer learns at that level are removed
// and the rest are left alone.
void adjust_moves(std::uint8_t* decrypted, std::uint32_t personality,
                  std::uint16_t species, std::uint8_t level) {
    const auto base = substruct_at(personality, 1);
    for (std::size_t slot = 0; slot < 4; ++slot) {
        const auto move = read16(decrypted + base + slot * 2);
        if (move == 0) continue;
        if (frlg_species_can_have_move(species, level, move)) continue;
        write16(decrypted + base + slot * 2, 0);
        decrypted[base + 8 + slot] = 0;
    }
}

// The health of a Pokemon at a level. pret/pokefirered src/pokemon.c, the
// macro CALC_STAT: the base health of the species, the individual value and
// the effort value of the health, the level, and ten more.
std::uint16_t max_health(std::uint16_t species, const PokemonIv& ivs, const PokemonEv& evs,
                          std::uint8_t level) {
    const auto base = frlg_base_hp(species);
    const auto value = ((2 * base + ivs.hp + evs.hp / 4) * level) / 100 + level + 10;
    return static_cast<std::uint16_t>(value);
}

bool evs_are_legal(const PokemonEv& evs) {
    const std::uint16_t values[6] = {evs.hp, evs.attack, evs.defense,
                                     evs.speed, evs.special_attack, evs.special_defense};
    std::uint16_t total = 0;
    for (const auto value : values) {
        if (value > ev_per_stat_limit) return false;
        total = static_cast<std::uint16_t>(total + value);
    }
    if (total > ev_total_limit) return false;
    // The game gives the effort of a stat only when none of the other three
    // that compete with it has any, and it zeroes the others when it does.
    const bool physical = evs.attack > 0 || evs.defense > 0;
    const bool special = evs.special_attack > 0 || evs.special_defense > 0;
    const bool mixed = evs.speed > 0 && (physical || special);
    return !mixed;
}

bool ivs_are_legal(const PokemonIv& ivs) {
    const std::uint8_t values[6] = {ivs.hp, ivs.attack, ivs.defense,
                                     ivs.speed, ivs.special_attack, ivs.special_defense};
    for (const auto value : values)
        if (value > 31) return false;
    return true;
}
}  // namespace

PokemonWriteResult write_frlg_pokemon(const std::uint8_t* record, std::size_t length,
                                      const PokemonEdit& edit) {
    PokemonWriteResult result;
    if (record == nullptr || length < box_record_length) {
        result.reason = "The record is too short to be read";
        return result;
    }
    const bool in_party = length >= party_record_length;

    std::array<std::uint8_t, 100> copy{};
    std::memcpy(copy.data(), record, std::min(length, copy.size()));

    const auto old_pid = read32(copy.data() + personality_offset);
    const auto old_ot_id = read32(copy.data() + ot_id_offset);
    auto pid = old_pid;
    if (edit.set_pid) pid = edit.pid;

    // The species and the growth rate decide the experience of every level.
    std::array<std::uint8_t, secure_length> decrypted{};
    decrypt(copy.data(), decrypted.data());
    const auto species = species_of(decrypted.data(), old_pid);
    if (species == 0) {
        result.reason = "The record holds no Pokemon";
        return result;
    }
    const auto rate = frlg_growth_rate(species);

    PokemonIv ivs{};
    read_ivs(decrypted.data(), old_pid, ivs);
    PokemonEv evs{};
    read_evs(decrypted.data(), old_pid, evs);

    if (edit.set_ivs) {
        if (!ivs_are_legal(edit.ivs)) {
            result.reason = "An individual value is out of range";
            return result;
        }
        ivs = edit.ivs;
    }
    if (edit.set_evs) {
        if (!evs_are_legal(edit.evs)) {
            result.reason = "The effort values are out of range or compete with each other";
            return result;
        }
        evs = edit.evs;
    }

    // The level is not stored: the experience is what the game reads, and the
    // level comes out of it.
    std::uint8_t level = 0;
    if (edit.set_level) {
        if (edit.level < 1 || edit.level > 100) {
            result.reason = "The level is out of range";
            return result;
        }
        if (rate == GrowthRate::none) {
            result.reason = "The growth rate of the species is not known";
            return result;
        }
        level = edit.level;
    } else {
        const auto experience = experience_of(decrypted.data(), old_pid);
        level = frlg_level_for_exp(rate, experience);
    }
    result.old_level = frlg_level_for_exp(rate, experience_of(decrypted.data(), old_pid));
    result.new_level = level;
    result.old_pid = old_pid;
    result.new_pid = pid;

    // The personality value decides where the substructures are stored, so
    // changing it moves the data. The fields are read and written with the
    // old personality value and the data is placed again with the new one.
    if (pid != old_pid) {
        // The substructures have to travel to their new places. The canonical
        // type zero holds the species and the experience, the type two the
        // effort values and the type three the individual values. Each one
        // goes to the place the NEW personality value decides.
        std::array<std::uint8_t, secure_length> moved{};
        std::memcpy(moved.data() + substruct_at(pid, 0),
                    decrypted.data() + substruct_at(old_pid, 0), substruct_length);
        std::memcpy(moved.data() + substruct_at(pid, 1),
                    decrypted.data() + substruct_at(old_pid, 1), substruct_length);
        std::memcpy(moved.data() + substruct_at(pid, 2),
                    decrypted.data() + substruct_at(old_pid, 2), substruct_length);
        std::memcpy(moved.data() + substruct_at(pid, 3),
                    decrypted.data() + substruct_at(old_pid, 3), substruct_length);
        decrypted = moved;
    }

    if (edit.set_level) {
        set_experience(decrypted.data(), pid, frlg_exp_for_level(rate, level));
        // The moves only go when the level goes down: a move the species can
        // still have is left alone, and a level that goes up teaches nothing
        // here.
        if (level < result.old_level) adjust_moves(decrypted.data(), pid, species, level);
    }
    if (edit.set_evs) set_evs(decrypted.data(), pid, evs);
    if (edit.set_ivs) set_ivs(decrypted.data(), pid, ivs);
    if (edit.set_origin) set_origin(decrypted.data(), pid, edit.origin);
    if (edit.set_nickname) std::memcpy(copy.data() + nickname_offset, edit.nickname.data(), 10);

    write32(copy.data() + personality_offset, pid);

    // The checksum is recomputed and the data is encrypted again, the same way
    // the game does it after a change.
    write16(copy.data() + checksum_offset, record_checksum(decrypted.data()));
    encrypt(decrypted.data(), copy.data() + secure_offset, pid ^ old_ot_id);

    // A record of the party carries the level and the health, and they have
    // to agree with the new level.
    // The level and the health of a record of the party follow the level, and
    // only when the level changes: a record whose nickname is edited keeps the
    // health it had.
    if (in_party && edit.set_level) {
        copy[party_level_offset] = level;
        const auto health = max_health(species, ivs, evs, level);
        write16(copy.data() + party_hp_offset, health);
        write16(copy.data() + party_max_hp_offset, health);
    }

    // The record is read back and every field has to be the one that was
    // asked for.
    std::array<std::uint8_t, secure_length> check{};
    decrypt(copy.data(), check.data());
    if (read32(copy.data() + personality_offset) != pid) {
        result.reason = "The personality value did not survive the write";
        return result;
    }
    if (record_checksum(check.data()) != read16(copy.data() + checksum_offset)) {
        result.reason = "The checksum does not match the data";
        return result;
    }
    if (species_of(check.data(), pid) != species) {
        result.reason = "The species did not survive the write";
        return result;
    }
    if (edit.set_level &&
        frlg_level_for_exp(rate, experience_of(check.data(), pid)) != level) {
        result.reason = "The level does not read back";
        return result;
    }
    if (edit.set_evs) {
        PokemonEv written{};
        read_evs(check.data(), pid, written);
        if (written.hp != evs.hp || written.attack != evs.attack ||
            written.defense != evs.defense || written.speed != evs.speed ||
            written.special_attack != evs.special_attack ||
            written.special_defense != evs.special_defense) {
            result.reason = "The effort values do not read back";
            return result;
        }
    }
    if (edit.set_ivs) {
        PokemonIv written{};
        read_ivs(check.data(), pid, written);
        if (written.hp != ivs.hp || written.attack != ivs.attack ||
            written.defense != ivs.defense || written.speed != ivs.speed ||
            written.special_attack != ivs.special_attack ||
            written.special_defense != ivs.special_defense) {
            result.reason = "The individual values do not read back";
            return result;
        }
    }
    if (edit.set_nickname &&
        std::memcmp(copy.data() + nickname_offset, edit.nickname.data(), 10) != 0) {
        result.reason = "The nickname did not survive the write";
        return result;
    }

    for (std::size_t i = 0; i < length && i < copy.size(); ++i)
        if (copy[i] != record[i]) ++result.changed_bytes;
    result.record = copy;
    result.ok = true;
    return result;
}

PokemonState read_frlg_pokemon_state(const std::uint8_t* record, std::size_t length) {
    PokemonState state;
    if (record == nullptr || length < box_record_length) return state;
    const auto personality = read32(record + personality_offset);
    const auto key = personality ^ read32(record + ot_id_offset);
    std::array<std::uint8_t, secure_length> decrypted{};
    for (std::size_t i = 0; i < secure_length; ++i)
        decrypted[i] = static_cast<std::uint8_t>(
            record[secure_offset + i] ^ static_cast<std::uint8_t>(key >> (8 * (i % 4))));
    const auto species = read16(decrypted.data() + substruct_at(personality, 0));
    if (species == 0) return state;
    state.species = species;
    const auto rate = frlg_growth_rate(species);
    state.level = frlg_level_for_exp(
        rate, read32(decrypted.data() + substruct_at(personality, 0) + 4));
    read_evs(decrypted.data(), personality, state.evs);
    read_ivs(decrypted.data(), personality, state.ivs);
    read_origin(decrypted.data(), personality, state.origin);
    std::memcpy(state.nickname.data(), record + nickname_offset, 10);
    const auto traits = frlg_traits_from_pid(
        personality, read32(record + ot_id_offset),
        static_cast<std::uint8_t>(frlg_gender_ratio(species)));
    state.nature = traits.nature;
    state.shiny = traits.shiny;
    state.gender = traits.gender == PokemonGender::female
                       ? 1
                       : (traits.gender == PokemonGender::genderless ? 2 : 0);
    return state;
}

PokemonOrigin read_frlg_pokemon_origin(const std::uint8_t* record, std::size_t length) {
    PokemonOrigin origin;
    if (record == nullptr || length < box_record_length) return origin;
    const auto personality = read32(record + personality_offset);
    const auto key = personality ^ read32(record + ot_id_offset);
    std::array<std::uint8_t, secure_length> decrypted{};
    for (std::size_t i = 0; i < secure_length; ++i)
        decrypted[i] = static_cast<std::uint8_t>(
            record[secure_offset + i] ^ static_cast<std::uint8_t>(key >> (8 * (i % 4))));
    read_origin(decrypted.data(), personality, origin);
    return origin;
}

}  // namespace pksm
