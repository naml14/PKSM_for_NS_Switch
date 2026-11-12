#include "pksm/frlg_learnsets.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"Bulbasaur level moves", [] {
            // https://raw.githubusercontent.com/pret/pokefirered/master/src/data/pokemon/level_up_learnsets.h
            // Bulbasaur starts with Tackle at level 1 and learns Growl at 4.
            require(pksm::frlg_level_up_move_count(1) > 1, "missing level moves");
            require(pksm::frlg_level_up_move(1, 0).move == 33 && pksm::frlg_level_up_move(1, 0).level == 1, "wrong first move");
            require(pksm::frlg_level_up_move(1, 1).move == 45 && pksm::frlg_level_up_move(1, 1).level == 4, "wrong second move");
        }},
        {"Bulbasaur machines", [] {
            // https://raw.githubusercontent.com/pret/pokefirered/master/src/data/pokemon/tmhm_learnsets.h
            // Bulbasaur learns TM06 Toxic and HM06 Rock Smash, but not TM01.
            require(pksm::frlg_learns_tmhm(1, 6), "TM06 missing");
            require(pksm::frlg_learns_tmhm(1, 56), "HM06 missing");
            require(!pksm::frlg_learns_tmhm(1, 1), "unexpected TM01");
            require(!pksm::frlg_learns_tmhm(1, 0) && !pksm::frlg_learns_tmhm(1, 59), "invalid machine");
        }},
        {"Bulbasaur egg moves", [] {
            // https://raw.githubusercontent.com/pret/pokefirered/master/src/data/pokemon/egg_moves.h
            // Bulbasaur inherits Light Screen, Skull Bash, Safeguard, Charm,
            // Petal Dance, Magical Leaf, Grass Whistle and Curse.
            const std::vector<std::uint16_t> expected = {113, 130, 219, 204, 80, 345, 320, 174};
            require(pksm::frlg_egg_move_count(1) == expected.size(), "wrong egg count");
            for (std::size_t i = 0; i < expected.size(); ++i)
                require(pksm::frlg_egg_move(1, i) == expected[i], "wrong egg move");
        }},
        {"out of range", [] {
            require(pksm::frlg_level_up_move_count(65535) == 0, "unexpected level moves");
            require(pksm::frlg_egg_move_count(65535) == 0, "unexpected egg moves");
            require(!pksm::frlg_learns_tmhm(65535, 1), "unexpected machine");
            require(pksm::frlg_level_up_move(65535, 0).move == 0, "invalid level access");
            require(pksm::frlg_egg_move(65535, 0) == 0, "invalid egg access");
        }}
    };
    std::size_t failed = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS: " << test.first << '\n'; }
        catch (const std::exception& error) {
            ++failed; std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
