#include "pksm/frlg_pokemon_traits.hpp"

#include <cstdint>
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

void check_request(const PokemonTraitRequest& request) {
    std::uint32_t pid = 0;
    require(frlg_find_pid(request, pid), "no PID found");
    const auto traits = frlg_traits_from_pid(pid, request.trainer_id, request.gender_ratio);
    require(traits.nature == request.nature, "wrong nature");
    require(traits.gender == request.gender, "wrong gender");
    require(traits.shiny == request.shiny, "wrong shininess");
}
}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"nature and names", [] {
            // 123456789 % 25 = 14 (Naive).
            // https://raw.githubusercontent.com/pret/pokefirered/master/src/pokemon.c
            // https://raw.githubusercontent.com/pret/pokefirered/master/include/constants/pokemon.h
            require(frlg_traits_from_pid(123456789, 0, 127).nature == 14, "wrong remainder");
            const char* names[] = {"Hardy", "Lonely", "Brave", "Adamant", "Naughty", "Bold",
                "Docile", "Relaxed", "Impish", "Lax", "Timid", "Hasty", "Serious", "Jolly",
                "Naive", "Modest", "Mild", "Quiet", "Bashful", "Rash", "Calm", "Gentle",
                "Sassy", "Careful", "Quirky"};
            for (std::uint8_t i = 0; i < 25; ++i)
                require(std::string(frlg_nature_name(i)) == names[i], "wrong name");
            require(std::string(frlg_nature_name(25)).empty(), "invalid name");
        }},
        {"gender ratios", [] {
            // Low byte 30 is female for 31, 63 and 127; equality is male.
            // https://raw.githubusercontent.com/pret/pokefirered/master/src/pokemon.c
            for (const std::uint8_t ratio : {std::uint8_t{31}, std::uint8_t{63}, std::uint8_t{127}}) {
                require(frlg_traits_from_pid(30, 0, ratio).gender == PokemonGender::female, "female threshold");
                require(frlg_traits_from_pid(ratio, 0, ratio).gender == PokemonGender::male, "male threshold");
            }
            require(frlg_traits_from_pid(0, 0, 0).gender == PokemonGender::male, "fixed male");
            require(frlg_traits_from_pid(255, 0, 254).gender == PokemonGender::female, "fixed female");
            require(frlg_traits_from_pid(0, 0, 255).gender == PokemonGender::genderless, "genderless");
        }},
        {"shiny xor", [] {
            // PID 0x12345678 and OT 0x1234567f give 7; OT 0x12345670 gives 8.
            // https://raw.githubusercontent.com/pret/pokefirered/master/include/pokemon.h
            // https://raw.githubusercontent.com/pret/pokefirered/master/src/pokemon.c
            require(frlg_traits_from_pid(0x12345678, 0x1234567f, 127).shiny, "expected shiny");
            require(!frlg_traits_from_pid(0x12345678, 0x12345670, 127).shiny, "expected non-shiny");
        }},
        {"search exact traits", [] {
            for (const bool shiny : {false, true}) {
                check_request({0, PokemonGender::male, shiny, 0, 0x12345678});
                check_request({24, PokemonGender::female, shiny, 254, 0x87654321});
                check_request({12, PokemonGender::genderless, shiny, 255, 0xffffffff});
                for (const std::uint8_t ratio : {std::uint8_t{31}, std::uint8_t{63}, std::uint8_t{127}}) {
                    check_request({7, PokemonGender::female, shiny, ratio, 0x12345678});
                    check_request({18, PokemonGender::male, shiny, ratio, 0x12345678});
                }
            }
        }},
        {"impossible requests preserve output", [] {
            std::uint32_t pid = 0xdeadbeef;
            require(!frlg_find_pid({0, PokemonGender::female, false, 0, 0}, pid), "fixed male accepted female");
            require(pid == 0xdeadbeef, "output changed");
            require(!frlg_find_pid({0, PokemonGender::genderless, true, 127, 0}, pid), "genderless accepted");
            require(!frlg_find_pid({25, PokemonGender::male, false, 127, 0}, pid), "invalid nature accepted");
            require(pid == 0xdeadbeef, "output changed on rejection");
        }}
    };
    std::size_t failed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS: " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
