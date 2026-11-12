#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace pksm {

// The individual values of a record, each from 0 to 31.
struct PokemonIv {
    std::uint8_t hp = 0;
    std::uint8_t attack = 0;
    std::uint8_t defense = 0;
    std::uint8_t speed = 0;
    std::uint8_t special_attack = 0;
    std::uint8_t special_defense = 0;
};

// The effort values of a record, each from 0 to 252.
struct PokemonEv {
    std::uint8_t hp = 0;
    std::uint8_t attack = 0;
    std::uint8_t defense = 0;
    std::uint8_t speed = 0;
    std::uint8_t special_attack = 0;
    std::uint8_t special_defense = 0;
};

// The origin of a record: where it came from and who found it.
struct PokemonOrigin {
    bool egg = false;                // the record is an egg that has not hatched
    std::uint8_t location = 0;       // where it was found
    std::uint8_t level = 0;          // the level it was found at
    std::uint8_t game = 0;           // the game it came from
    std::uint8_t ball = 0;           // the ball it is in
    bool trainer_female = false;     // the gender of the original trainer
};

// What can be changed in a record. A field that is not asked for keeps the
// value it had.
struct PokemonEdit {
    bool set_nickname = false;
    std::array<std::uint8_t, 10> nickname{};
    bool set_level = false;
    std::uint8_t level = 0;
    bool set_evs = false;
    PokemonEv evs{};
    bool set_ivs = false;
    PokemonIv ivs{};
    bool set_pid = false;
    std::uint32_t pid = 0;
    bool set_origin = false;
    PokemonOrigin origin{};
};

// The result of editing a record.
struct PokemonWriteResult {
    bool ok = false;
    std::string reason;                     // why it failed, empty when it worked
    std::array<std::uint8_t, 100> record{};  // the new record
    std::uint8_t old_level = 0;
    std::uint8_t new_level = 0;
    std::uint32_t old_pid = 0;
    std::uint32_t new_pid = 0;
    std::size_t changed_bytes = 0;          // how many bytes differ from the original
};

// Edits a record of a Pokemon and returns the new one. The original is never
// modified. The record is only returned after it has been verified: the
// checksum is recomputed, the data is encrypted again, the record is read
// back and every field has to read the value that was asked for.
//
// A record of the party also carries the level and the health, and they are
// kept consistent with the new level.
PokemonWriteResult write_frlg_pokemon(const std::uint8_t* record, std::size_t length,
                                      const PokemonEdit& edit);

// Reads the origin of a record: where it came from and who found it.
PokemonOrigin read_frlg_pokemon_origin(const std::uint8_t* record, std::size_t length);

}  // namespace pksm
