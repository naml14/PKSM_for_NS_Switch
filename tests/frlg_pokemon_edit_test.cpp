#include "pksm/frlg_pokemon_edit.hpp"
#include "pksm/frlg_save.hpp"
#include "pksm/frlg_species_tables.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
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
std::uint32_t get32(const std::vector<std::uint8_t>& v, std::size_t p) {
    return get16(v, p) | (static_cast<std::uint32_t>(get16(v, p + 2)) << 16);
}
std::size_t address(std::size_t block, std::size_t section, std::size_t offset) {
    return (block * 14 + (section * 5) % 14) * 0x1000 + offset;
}
std::vector<std::uint8_t> record() {
    std::vector<std::uint8_t> r(100), plain(48);
    put32(r, 0, 2400); put32(r, 4, 0x11223344);
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
            put32(v, base + 0xff8, 0x08012025); // The first block has the higher save counter, so it is the one the game loads.
            put32(v, base + 0xffc, static_cast<std::uint32_t>(b == 0 ? 2 : 1));
        }
        for (std::size_t i = 0; i < 100; ++i) v[address(b, 1, 0x38 + i)] = r[i];
        for (const std::size_t pos : {std::size_t(4 + 80 * 31), std::size_t(3924)})
            for (std::size_t i = 0; i < 80; ++i) v[address(b, 5 + (pos + i) / 0xf80, (pos + i) % 0xf80)] = r[i];
        for (std::size_t id = 0; id < 14; ++id) {
            const auto base = address(b, id, 0);
            put16(v, base + 0xff6, frlg_sector_checksum(v, base, frlg_section_used_length(id, true)));
        }
    }
    return v;
}
PokemonEditRequest level_request() { PokemonEditRequest r; r.box = 1; r.slot = 1; r.edit.set_level = true; r.edit.level = 42; return r; }
void verify(const std::vector<std::uint8_t>& before, const PokemonEditResult& r, std::size_t block,
            std::size_t first, std::size_t start, std::size_t size) {
    require(r.ok && r.reason.empty() && r.image.size() == before.size(), "edit failed");
    std::size_t changes = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const auto p = address(block, first + (start + i) / 0xf80, (start + i) % 0xf80);
        changes += before[p] != r.image[p];
    }
    require(changes == r.changed_bytes && changes > 0, "record change count differs");
    for (std::size_t b = 0; b < 2; ++b) for (std::size_t id = 0; id < 14; ++id) {
        const auto base = address(b, id, 0);
        require(get16(r.image, base + 0xff6) == frlg_sector_checksum(r.image, base, frlg_section_used_length(id, true)), "sector checksum differs");
        for (std::size_t j = 0; j < 0x1000; ++j) {
            const bool record_byte = b == block && id >= first && (id - first) * 0xf80 + j >= start && (id - first) * 0xf80 + j < start + size && j < 0xf80;
            const bool touched = b == block && id >= first && id - first >= start / 0xf80 && id - first <= (start + size - 1) / 0xf80;
            if (!record_byte && !(touched && (j == 0xff6 || j == 0xff7))) require(before[base + j] == r.image[base + j], "unrelated byte changed");
        }
    }
}
}
int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"box level", [] { auto v = image(); auto before = v; auto r = edit_frlg_pokemon(v, 0, level_request()); verify(v, r, 0, 5, 4 + 80 * 31, 80); require(r.old_level == 5 && r.new_level == 42 && r.old_pid == r.new_pid && v == before, "level or input differs"); }},
        {"party level", [] { auto v = image(); auto q = level_request(); q.place = PokemonPlace::party; auto r = edit_frlg_pokemon(v, 0, q); verify(v, r, 0, 1, 0x38, 100); require(r.image[address(0, 1, 0x38 + 0x54)] == 42, "party level differs"); }},
        {"cross-section record", [] { auto v = image(); auto q = level_request(); q.slot = 19; q.edit.set_evs = true; q.edit.evs.hp = 12; auto r = edit_frlg_pokemon(v, 0, q); verify(v, r, 0, 5, 3924, 80); require(r.new_level == 42, "cross-section level differs"); require(get16(v, address(0, 5, 0xff6)) != get16(r.image, address(0, 5, 0xff6)) && get16(v, address(0, 6, 0xff6)) != get16(r.image, address(0, 6, 0xff6)), "both checksums must change"); }},
        {"empty and invalid positions", [] { auto v = image(); auto q = level_request(); q.slot = 2; for (int i = 0; i < 4; ++i) { auto r = edit_frlg_pokemon(v, 0, q); require(!r.ok && !r.reason.empty() && r.image.empty(), "invalid accepted"); if (i == 0) q.box = 14; if (i == 1) { q.box = 0; q.slot = 30; } if (i == 2) { q.place = PokemonPlace::party; q.party = 6; } } }},
        {"pid permutation", [] { auto v = image(); auto q = level_request(); q.edit.set_level = false; q.edit.set_pid = true; q.edit.pid = 2408; auto r = edit_frlg_pokemon(v, 0, q); require(r.ok && r.old_level == 5 && r.new_level == 5 && r.new_pid == 2408, "pid change failed"); auto p = address(0, 5, 4 + 80 * 31); auto key = get32(r.image, p) ^ get32(r.image, p + 4); auto species = static_cast<std::uint16_t>((r.image[p + 0x20 + 24] ^ (key & 255)) | ((r.image[p + 0x20 + 25] ^ ((key >> 8) & 255)) << 8)); require(species == 4, "species differs"); }},
        {"zero pid rejected", [] { auto v = image(); auto q = level_request(); q.edit.set_pid = true; q.edit.pid = 0; auto r = edit_frlg_pokemon(v, 0, q); require(!r.ok && !r.reason.empty() && r.image.empty(), "zero pid accepted"); }},
        {"the block the game would not load is refused", [] {
            auto v = image();
            auto r = edit_frlg_pokemon(v, 1, level_request());
            require(!r.ok && !r.reason.empty() && r.image.empty(), "the wrong block was accepted");
        }},
        {"damaged block rejected", [] { auto v = image(); v[address(0, 8, 0)] ^= 1; auto r = edit_frlg_pokemon(v, 0, level_request()); require(!r.ok && !r.reason.empty() && r.image.empty(), "damaged block accepted"); }}
    };
    int passed = 0, failed = 0;
    for (const auto& t : tests) try { t.second(); ++passed; std::cout << "PASS: " << t.first << '\n'; } catch (const std::exception& e) { ++failed; std::cout << "FAIL: " << t.first << ": " << e.what() << '\n'; }
    std::cout << passed << " passed, " << failed << " failed\n"; return failed ? 1 : 0;
}
