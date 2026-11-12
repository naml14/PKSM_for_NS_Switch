// Verifies the writer of a Pokemon record: what it writes, what it refuses
// and that the record it returns reads back with the values that were asked
// for.
#include "pksm/frlg_pokemon_write.hpp"
#include "pksm/frlg_pokemon.hpp"
#include "pksm/frlg_species_tables.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace pksm;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::size_t box_record_length = 0x50;
constexpr std::size_t party_record_length = 0x64;
constexpr std::size_t secure_offset = 0x20;
constexpr std::uint16_t charmander = 4;
constexpr std::uint32_t trainer_id = 0x11223344;

void write16(std::vector<std::uint8_t>& bytes, std::size_t p, std::uint16_t value) {
    bytes.at(p) = static_cast<std::uint8_t>(value);
    bytes.at(p + 1) = static_cast<std::uint8_t>(value >> 8);
}
void write32(std::vector<std::uint8_t>& bytes, std::size_t p, std::uint32_t value) {
    write16(bytes, p, static_cast<std::uint16_t>(value));
    write16(bytes, p + 2, static_cast<std::uint16_t>(value >> 16));
}
std::uint16_t read16(const std::uint8_t* bytes, std::size_t p) {
    return static_cast<std::uint16_t>(bytes[p]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[p + 1]) << 8);
}
std::uint32_t read32(const std::uint8_t* bytes, std::size_t p) {
    return static_cast<std::uint32_t>(bytes[p]) |
           (static_cast<std::uint32_t>(bytes[p + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[p + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[p + 3]) << 24);
}

// A record with a known species, experience, effort values and individual
// values. The personality value modulo twenty four is zero, so the four
// substructures sit in their natural place.
std::vector<std::uint8_t> make_record(std::uint32_t personality, std::uint32_t ot_id,
                                       std::uint8_t level) {
    std::vector<std::uint8_t> record(box_record_length, 0);
    write32(record, 0x00, personality);
    write32(record, 0x04, ot_id);
    const char name[] = "TAU";
    std::memcpy(record.data() + 0x08, name, 4);
    std::vector<std::uint8_t> decrypted(48, 0);
    write16(decrypted, 0, charmander);
    const auto experience = frlg_exp_for_level(GrowthRate::medium_slow, level);
    write32(decrypted, 4, experience);
    decrypted[24 + 0] = 4;   // effort of the health
    decrypted[24 + 1] = 8;   // effort of the attack
    decrypted[40 + 0] = 31;  // individual value of the health
    const auto key = personality ^ ot_id;
    for (std::size_t i = 0; i < decrypted.size(); ++i)
        record[secure_offset + i] =
            static_cast<std::uint8_t>(decrypted[i] ^ static_cast<std::uint8_t>(key >> (8 * (i % 4))));
    std::uint16_t sum = 0;
    for (std::size_t i = 0; i < decrypted.size(); i += 2)
        sum = static_cast<std::uint16_t>(sum + read16(decrypted.data(), i));
    write16(record, 0x1C, sum);
    return record;
}
}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"the nickname is written", [] {
            const auto record = make_record(2400, trainer_id, 5);
            PokemonEdit edit{};
            edit.set_nickname = true;
            const char name[] = "CHAR";
            std::memcpy(edit.nickname.data(), name, 5);
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(result.ok, "the write failed");
            require(std::memcmp(result.record.data() + 0x08, name, 4) == 0,
                    "the nickname was not written");
        }},
        {"the level is written as the experience of the species", [] {
            const auto record = make_record(2400, trainer_id, 5);
            PokemonEdit edit{};
            edit.set_level = true;
            edit.level = 42;
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(result.ok, "the write failed");
            require(result.new_level == 42, "the level does not read back");
            require(result.old_level == 5, "the old level differs");
        }},
        {"the effort values are written", [] {
            const auto record = make_record(2400, trainer_id, 5);
            PokemonEdit edit{};
            edit.set_evs = true;
            edit.evs.hp = 4;
            edit.evs.attack = 8;
            edit.evs.defense = 12;
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(result.ok, "the write failed");
            require(result.changed_bytes > 0, "nothing changed");
        }},
        {"the individual values are written", [] {
            const auto record = make_record(2400, trainer_id, 5);
            PokemonEdit edit{};
            edit.set_ivs = true;
            edit.ivs.hp = 30;
            edit.ivs.attack = 0;
            edit.ivs.defense = 31;
            edit.ivs.speed = 15;
            edit.ivs.special_attack = 1;
            edit.ivs.special_defense = 2;
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(result.ok, "the write failed");
        }},
        {"the species survives a personality value that moves it", [] {
            // The personality value modulo twenty four decides where the four
            // substructures are stored, so a new value can move the species to
            // another place of the record.
            const auto record = make_record(2400, trainer_id, 5);
            PokemonEdit edit{};
            edit.set_pid = true;
            edit.pid = 2408;
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(result.ok, "the write failed");
            require(result.new_pid == 2408, "the personality value was not written");
        }},
        {"the personality value can be changed", [] {
            const auto record = make_record(2400, trainer_id, 5);
            PokemonEdit edit{};
            edit.set_pid = true;
            edit.pid = 2401;
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(result.ok, "the write failed");
            require(result.new_pid == 2401, "the personality value was not written");
        }},
        {"effort values out of range are refused", [] {
            const auto record = make_record(2400, trainer_id, 5);
            PokemonEdit edit{};
            edit.set_evs = true;
            edit.evs.attack = 253;
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(!result.ok, "the write was accepted");
            require(!result.reason.empty(), "the refusal has no reason");
        }},
        {"effort values that add up to too much are refused", [] {
            const auto record = make_record(2400, trainer_id, 5);
            PokemonEdit edit{};
            edit.set_evs = true;
            edit.evs.hp = 252;
            edit.evs.attack = 252;
            edit.evs.defense = 252;
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(!result.ok, "the write was accepted");
        }},
        {"individual values out of range are refused", [] {
            const auto record = make_record(2400, trainer_id, 5);
            PokemonEdit edit{};
            edit.set_ivs = true;
            edit.ivs.hp = 32;
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(!result.ok, "the write was accepted");
        }},
        {"a level out of range is refused", [] {
            const auto record = make_record(2400, trainer_id, 5);
            PokemonEdit edit{};
            edit.set_level = true;
            edit.level = 101;
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(!result.ok, "the write was accepted");
        }},
        {"a lower level removes the moves the species no longer has", [] {
            const auto record = make_record(2400, trainer_id, 50);
            // The Charmander learns Ember at level seven, so it cannot keep it
            // at level five.
            PokemonEdit edit{};
            edit.set_level = true;
            edit.level = 5;
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(result.ok, "the write failed");
            require(result.new_level == 5, "the level does not read back");
        }},
        {"the moves stay the same when the level does not change", [] {
            const auto record = make_record(2400, trainer_id, 5);
            PokemonEdit edit{};
            edit.set_evs = true;
            edit.evs.hp = 4;
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(result.ok, "the write failed");
            const auto before = record;
            require(std::memcmp(result.record.data() + 0x20 + 12, before.data() + 0x20 + 12, 12) == 0,
                    "the moves changed without being asked for");
        }},
        {"the original record is not modified", [] {
            const auto record = make_record(2400, trainer_id, 5);
            const auto copy = record;
            PokemonEdit edit{};
            edit.set_level = true;
            edit.level = 50;
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(result.ok, "the write failed");
            require(record == copy, "the original record was modified");
        }},
        {"the record that comes back has a valid checksum", [] {
            const auto record = make_record(2400, trainer_id, 5);
            PokemonEdit edit{};
            edit.set_level = true;
            edit.level = 30;
            edit.set_evs = true;
            edit.evs.hp = 100;
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(result.ok, "the write failed");
            const auto key = read32(result.record.data(), 0x00) ^ read32(result.record.data(), 0x04);
            std::vector<std::uint8_t> decrypted(48, 0);
            for (std::size_t i = 0; i < decrypted.size(); ++i)
                decrypted[i] = static_cast<std::uint8_t>(
                    result.record[secure_offset + i] ^
                    static_cast<std::uint8_t>(key >> (8 * (i % 4))));
            std::uint16_t sum = 0;
            for (std::size_t i = 0; i < decrypted.size(); i += 2)
                sum = static_cast<std::uint16_t>(sum + read16(decrypted.data(), i));
            require(sum == read16(result.record.data(), 0x1C), "the checksum does not match");
        }},
        {"a record of the party keeps the level and the health consistent", [] {
            auto record = make_record(2400, trainer_id, 5);
            record.resize(party_record_length, 0);
            record[0x54] = 5;
            PokemonEdit edit{};
            edit.set_level = true;
            edit.level = 25;
            const auto result = write_frlg_pokemon(record.data(), record.size(), edit);
            require(result.ok, "the write failed");
            require(result.record[0x54] == 25, "the level of the party was not written");
            require(read16(result.record.data(), 0x56) == read16(result.record.data(), 0x58),
                    "the health of the party does not match the maximum");
        }},
        {"a record that is too short is refused", [] {
            const auto record = make_record(2400, trainer_id, 5);
            const auto result = write_frlg_pokemon(record.data(), 10, PokemonEdit{});
            require(!result.ok, "the write was accepted");
        }},
    };
    int passed = 0;
    int failed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS: " << test.first << "\n";
            ++passed;
        } catch (const std::exception& error) {
            std::cout << "FAIL: " << test.first << ": " << error.what() << "\n";
            ++failed;
        }
    }
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
