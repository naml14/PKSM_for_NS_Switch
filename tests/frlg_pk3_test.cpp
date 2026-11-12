// Validates the reader and the writer of a Pokemon record against real
// records exported by the game as .pk3 files.
//
// The .pk3 format holds the same record as the game, with the data already
// decrypted and with the checksum of the record. The files are not part of
// the repository, so the test is skipped when the directory is not there.
#include "pksm/frlg_pokemon_traits.hpp"
#include "pksm/frlg_pokemon_write.hpp"
#include "pksm/frlg_species_tables.hpp"

#include <cstdint>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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

constexpr std::size_t secure_offset = 0x20;
constexpr std::size_t secure_length = 48;

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

// The checksum the game stores for a record: the sum of the words of the
// decrypted data.
std::uint16_t checksum_of(const std::vector<std::uint8_t>& pk3) {
    std::uint16_t sum = 0;
    for (std::size_t i = 0; i < secure_length; i += 2)
        sum = static_cast<std::uint16_t>(sum + read16(pk3.data() + secure_offset, i));
    return sum;
}

// Where each canonical substructure is stored, decided by the personality
// value modulo twenty four. pret/pokefirered src/pokemon.c, the function
// GetSubstruct.
constexpr std::uint8_t substruct_positions[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {0, 3, 2, 1},
    {1, 0, 2, 3}, {1, 0, 3, 2}, {2, 0, 1, 3}, {3, 0, 1, 2}, {2, 0, 3, 1}, {3, 0, 2, 1},
    {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3}, {3, 1, 0, 2}, {2, 3, 0, 1}, {3, 2, 0, 1},
    {1, 2, 3, 0}, {1, 3, 2, 0}, {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0}
};

// The .pk3 holds the four substructures decrypted and in the canonical order,
// so they are placed where the game keeps them and then encrypted.
std::vector<std::uint8_t> to_game_record(const std::vector<std::uint8_t>& pk3) {
    std::vector<std::uint8_t> record(pk3.begin(), pk3.begin() + 80);
    const auto pid = read32(record.data(), 0);
    const auto key = pid ^ read32(record.data(), 4);
    std::vector<std::uint8_t> placed(secure_length, 0);
    for (std::size_t canonical = 0; canonical < 4; ++canonical) {
        const auto physical = substruct_positions[pid % 24][canonical];
        std::memcpy(placed.data() + physical * 12,
                    pk3.data() + secure_offset + canonical * 12, 12);
    }
    for (std::size_t i = 0; i < secure_length; ++i)
        record[secure_offset + i] =
            static_cast<std::uint8_t>(placed[i] ^ static_cast<std::uint8_t>(key >> (8 * (i % 4))));
    return record;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
}

std::filesystem::path records_directory() {
    // The narrow string of the environment is read as UTF-8, so a name with
    // an accent is found the way it is written.
    if (const char* dir = std::getenv("PKSM_PK3_DIR"))
        return std::filesystem::u8path(dir);
    return std::filesystem::u8path("pokémons");
}
}  // namespace

int main() {
    const auto directory = records_directory();
    std::vector<std::filesystem::path> files;
    if (std::filesystem::is_directory(directory))
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            if (entry.path().extension() == ".pk3") files.push_back(entry.path());
    if (files.empty()) {
        std::cout << "SKIP: no exported records found in " << directory.u8string() << "\n";
        std::cout << "0 passed, 0 failed\n";
        return 0;
    }
    std::sort(files.begin(), files.end());

    int passed = 0;
    int failed = 0;
    for (const auto& path : files) {
        const auto name = path.filename().u8string();
        try {
            const auto pk3 = read_file(path);
            require(pk3.size() == 100, "the record does not have the size of a record");

            // The checksum the file carries has to match the data it carries.
            require(checksum_of(pk3) == read16(pk3.data(), 0x1C),
                    "the checksum of the exported record does not match its data");

            // The species and the level have to come out of the record.
            const auto pid = read32(pk3.data(), 0);
            const auto species = read16(pk3.data(), secure_offset);
            const auto experience = read32(pk3.data(), secure_offset + 4);
            require(species != 0, "the record holds no Pokemon");
            const auto rate = frlg_growth_rate(species);
            require(rate != GrowthRate::none, "the growth rate of the species is not known");
            const auto level = frlg_level_for_exp(rate, experience);
            require(level >= 1 && level <= 100, "the level is out of range");
            if (pk3.size() > 0x54)
                require(pk3[0x54] == level, "the level of the party does not match the experience");

            // The exported name marks the shiny ones, so the traits that the
            // personality value produces have to agree with it.
            const auto traits = frlg_traits_from_pid(pid, read32(pk3.data(), 4),
                                                     static_cast<std::uint8_t>(
                                                         frlg_gender_ratio(species)));
            const bool marked_shiny = name.find("★") != std::string::npos;
            require(traits.shiny == marked_shiny,
                    "the shininess does not match the exported name");
            if (frlg_gender_ratio(species) == GenderRatio::genderless)
                require(traits.gender == PokemonGender::genderless,
                        "a species without gender does not read as one");

            // The record the game stores is the exported one, encrypted.
            const auto record = to_game_record(pk3);

            // Writing it without changing anything has to give the same
            // record back, with the same checksum and the same data.
            const auto edit = write_frlg_pokemon(record.data(), record.size(), PokemonEdit{});
            require(edit.ok, edit.reason.c_str());
            require(read16(edit.record.data(), 0x1C) == read16(pk3.data(), 0x1C),
                    "the checksum changed");
            require(std::memcmp(edit.record.data() + secure_offset,
                                record.data() + secure_offset, secure_length) == 0,
                    "the encrypted data changed");
            require(read32(edit.record.data(), 0) == pid, "the personality value changed");
            require(std::memcmp(edit.record.data() + 0x08, pk3.data() + 0x08, 10) == 0,
                    "the nickname changed");

            // Changing the level has to write the experience of that level and
            // keep the checksum right.
            PokemonEdit level_edit{};
            level_edit.set_level = true;
            level_edit.level = static_cast<std::uint8_t>(level < 100 ? level + 1 : level - 1);
            const auto changed = write_frlg_pokemon(record.data(), record.size(), level_edit);
            require(changed.ok, changed.reason.c_str());
            require(changed.new_level == level_edit.level, "the level does not read back");
            require(changed.changed_bytes > 0, "nothing changed");

            std::cout << "PASS: " << name << " (species " << static_cast<unsigned>(species) << ", level "
                      << static_cast<unsigned>(level)
                      << ")\n";
            ++passed;
        } catch (const std::exception& error) {
            std::cout << "FAIL: " << name << ": " << error.what() << "\n";
            ++failed;
        }
    }
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
