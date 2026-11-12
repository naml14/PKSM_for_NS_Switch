#include "pksm/frlg_pokemon_editor.hpp"
#include "pksm/frlg_content.hpp"
#include "pksm/frlg_pokemon.hpp"
#include "pksm/frlg_save.hpp"
#include "pksm/frlg_species_tables.hpp"
#include "pksm/frlg_text.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace pksm;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void put16(std::vector<std::uint8_t>& v, std::size_t p, std::uint16_t n) {
    v.at(p) = static_cast<std::uint8_t>(n); v.at(p + 1) = static_cast<std::uint8_t>(n >> 8);
}
void put32(std::vector<std::uint8_t>& v, std::size_t p, std::uint32_t n) {
    put16(v, p, static_cast<std::uint16_t>(n)); put16(v, p + 2, static_cast<std::uint16_t>(n >> 16));
}
std::uint16_t get16(const std::vector<std::uint8_t>& v, std::size_t p) {
    return static_cast<std::uint16_t>(v.at(p) | (static_cast<unsigned>(v.at(p + 1)) << 8));
}
std::size_t address(std::size_t block, std::size_t section, std::size_t offset) {
    return (block * 14 + (section * 5) % 14) * 0x1000 + offset;
}
std::vector<std::uint8_t> record() {
    std::vector<std::uint8_t> r(100), plain(48);
    put32(r, 0, 2400); put32(r, 4, 0x11223344);
    r[0x12] = 2;
    encode_frlg_name(r.data() + 8, 10, "CHAR");
    put16(plain, 0, 4); put32(plain, 4, frlg_exp_for_level(GrowthRate::medium_slow, 5));
    std::uint16_t sum = 0;
    for (std::size_t i = 0; i < 48; i += 2) sum = static_cast<std::uint16_t>(sum + get16(plain, i));
    put16(r, 0x1c, sum);
    const auto key = 2400u ^ 0x11223344u;
    for (std::size_t i = 0; i < 48; ++i) r[0x20 + i] = static_cast<std::uint8_t>(plain[i] ^ (key >> (8 * (i % 4))));
    r[0x54] = 5;
    return r;
}
std::vector<std::uint8_t> image() {
    std::vector<std::uint8_t> v(frlg_image_size);
    const auto r = record();
    for (std::size_t b = 0; b < 2; ++b) {
        for (std::size_t id = 0; id < 14; ++id) {
            const auto base = address(b, id, 0);
            put16(v, base + 0xff4, static_cast<std::uint16_t>(id));
            put32(v, base + 0xff8, 0x08012025);
            put32(v, base + 0xffc, static_cast<std::uint32_t>(b == 0 ? 2 : 1));
        }
        for (std::size_t i = 0; i < 100; ++i) v[address(b, 1, 0x38 + i)] = r[i];
        const std::size_t pos = 4 + 80 * 31;
        for (std::size_t i = 0; i < 80; ++i) v[address(b, 5 + (pos + i) / 0xf80, (pos + i) % 0xf80)] = r[i];
        for (std::size_t id = 0; id < 14; ++id) {
            const auto base = address(b, id, 0);
            put16(v, base + 0xff6, frlg_sector_checksum(v, base, frlg_section_used_length(id, true)));
        }
    }
    return v;
}
PokemonEditor opened(const std::vector<std::uint8_t>& v) {
    PokemonEditor editor;
    require(editor.begin(v, 0), "begin failed");
    std::string reason;
    if (!editor.open(reason)) throw std::runtime_error("open failed: " + reason);
    return editor;
}
void field(PokemonEditor& editor, PokemonField target) {
    for (std::size_t i = 0; i < pokemon_field_count && editor.field() != target; ++i) editor.next_field();
    require(editor.field() == target, "field not found");
}
void step(PokemonEditor& editor, int direction) {
    std::string reason;
    if (!editor.change(direction, reason)) throw std::runtime_error("change failed: " + reason);
}
void bound(PokemonEditor& editor, PokemonField f, int direction, int count, const char* expected) {
    field(editor, f);
    for (int i = 0; i < count; ++i) step(editor, direction);
    require(editor.proposed(f) == expected, "boundary value differs");
    std::string reason;
    require(!editor.change(direction, reason) && !reason.empty(), "out of range accepted");
    require(editor.proposed(f) == expected, "rejected change modified value");
}
}
int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"choices and selection", [] {
            auto v = image(); PokemonEditor e; require(e.begin(v, 0), "begin failed");
            const auto& c = e.choices(); require(c.size() == 2, "expected two occupied records");
            require(c[0].in_party && c[0].slot == 0 && c[0].species == 4 && c[0].level == 5 && c[0].name == "CHAR", "party choice differs");
            require(!c[1].in_party && c[1].box == 1 && c[1].slot == 1 && c[1].species == 4 && c[1].level == 5 && c[1].name == "CHAR", "box choice differs");
            require(e.selected() == 0, "initial selection differs"); e.select_previous(); require(e.selected() == 1, "previous does not wrap");
            e.select_next(); require(e.selected() == 0, "next does not wrap");
        }},
        {"open values", [] {
            auto e = opened(image()); require(!e.changed(), "opened editor marked changed");
            require(e.value(PokemonField::level) == "5" && e.value(PokemonField::nature) == "Hardy", "level or nature differs");
            require(e.value(PokemonField::gender) == "male" && e.value(PokemonField::shiny) == "no", "gender or shiny differs");
            for (std::size_t i = static_cast<std::size_t>(PokemonField::iv_hp); i < pokemon_field_count; ++i) {
                auto f = static_cast<PokemonField>(i); require(e.value(f) == "0" && e.proposed(f) == "0", "IV or EV differs");
            }
        }},
        {"level bounds", [] {
            auto e = opened(image()); bound(e, PokemonField::level, -1, 4, "1"); bound(e, PokemonField::level, 1, 99, "100");
            require(e.changed(), "change not marked");
        }},
        {"IV and EV bounds", [] {
            auto e = opened(image()); bound(e, PokemonField::iv_hp, 1, 31, "31"); bound(e, PokemonField::ev_hp, 1, 252, "252");
        }},
        {"personality traits", [] {
            for (auto f : {PokemonField::nature, PokemonField::gender, PokemonField::shiny}) {
                auto e = opened(image()); field(e, f); const auto before = e.proposed(f); step(e, 1);
                require(e.proposed(f) != before, "trait did not change");
                require(e.edit().set_pid && e.edit().pid != 2400 && e.edit().pid % 100 <= 31, "personality differs from request");
            }
        }},
        {"impossible gender", [] {
            auto e = opened(image()); field(e, PokemonField::gender); step(e, 1);
            const auto before = e.proposed(PokemonField::gender); const auto pid = e.edit().pid;
            std::string reason; require(!e.change(1, reason) && !reason.empty(), "genderless accepted");
            require(e.proposed(PokemonField::gender) == before && e.edit().pid == pid, "rejected gender changed editor");
        }},
        {"nickname", [] {
            auto e = opened(image()); std::string reason; require(e.set_nickname("ASH", reason), "nickname rejected");
            std::array<std::uint8_t, 10> expected{}; require(encode_frlg_name(expected.data(), expected.size(), "ASH") == 3, "encoding failed");
            require(e.edit().set_nickname && e.edit().nickname == expected, "encoded nickname differs");
            require(!e.set_nickname("\xF0\x9F\x98\x80", reason) && !reason.empty(), "unsupported nickname accepted");
            require(e.edit().nickname == expected, "rejected nickname changed edit");
        }},
        {"field navigation", [] {
            auto e = opened(image()); require(e.field() == PokemonField::level, "initial field differs");
            e.previous_field(); require(e.field() == PokemonField::ev_special_defense, "previous field does not wrap");
            e.next_field(); require(e.field() == PokemonField::level, "next field does not wrap");
            e.next_field(); require(e.field() == PokemonField::nature, "next field differs"); e.previous_field(); step(e, 1);
            require(e.proposed(PokemonField::level) == "6", "change affected wrong field");
        }},
        {"apply copies image", [] {
            auto v = image(); const auto before = v; auto e = opened(v); step(e, 1); auto result = e.apply(v, 0);
            require(result.ok && v == before && result.image != before, "apply failed or modified input");
            const auto party = read_frlg_party(read_frlg_content(result.image, 0));
            require(!party.empty() && party[0].level == 6, "applied level differs");
        }}
    };
    int passed = 0, failed = 0;
    for (const auto& t : tests) try { t.second(); ++passed; std::cout << "PASS: " << t.first << '\n'; } catch (const std::exception& e) { ++failed; std::cout << "FAIL: " << t.first << ": " << e.what() << '\n'; }
    std::cout << passed << " passed, " << failed << " failed\n"; return failed ? 1 : 0;
}
