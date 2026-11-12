#include "pksm/frlg_pokemon_edit.hpp"
#include "pksm/frlg_content.hpp"
#include "pksm/frlg_legality.hpp"
#include "pksm/frlg_save.hpp"

#include <algorithm>
#include <array>

namespace pksm {
namespace {
std::uint16_t u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8));
}
std::uint32_t u32(const std::uint8_t* p) {
    return u16(p) | (static_cast<std::uint32_t>(u16(p + 2)) << 16);
}
struct Decoded {
    std::array<std::uint8_t, 48> data{};
    std::uint32_t pid = 0;
    std::uint16_t species = 0;
    bool checksum = false;
};
Decoded decode(const std::uint8_t* p) {
    constexpr std::uint8_t growth[24] = {0,0,0,0,0,0,1,1,2,3,2,3,1,1,2,3,2,3,1,1,2,3,2,3};
    Decoded d;
    d.pid = u32(p);
    const auto key = d.pid ^ u32(p + 4);
    for (std::size_t i = 0; i < d.data.size(); ++i)
        d.data[i] = static_cast<std::uint8_t>(p[0x20 + i] ^ (key >> (8 * (i % 4))));
    d.species = u16(d.data.data() + 12 * growth[d.pid % 24]);
    std::uint16_t sum = 0;
    for (std::size_t i = 0; i < d.data.size(); i += 2) sum = static_cast<std::uint16_t>(sum + u16(d.data.data() + i));
    d.checksum = sum == u16(p + 0x1c);
    return d;
}
bool requested_fields(const std::uint8_t* record, const Decoded& d, const PokemonEdit& e) {
    constexpr std::uint8_t positions[24][4] = {
        {0,1,2,3},{0,1,3,2},{0,2,1,3},{0,3,1,2},{0,2,3,1},{0,3,2,1},
        {1,0,2,3},{1,0,3,2},{2,0,1,3},{3,0,1,2},{2,0,3,1},{3,0,2,1},
        {1,2,0,3},{1,3,0,2},{2,1,0,3},{3,1,0,2},{2,3,0,1},{3,2,0,1},
        {1,2,3,0},{1,3,2,0},{2,1,3,0},{3,1,2,0},{2,3,1,0},{3,2,1,0}
    };
    if (e.set_pid && d.pid != e.pid) return false;
    if (e.set_nickname && !std::equal(e.nickname.begin(), e.nickname.end(), record + 8)) return false;
    const auto* ev = d.data.data() + 12 * positions[d.pid % 24][2];
    const auto* iv = d.data.data() + 12 * positions[d.pid % 24][3];
    if (e.set_evs) {
        const std::uint8_t values[] = {e.evs.hp,e.evs.attack,e.evs.defense,e.evs.speed,e.evs.special_attack,e.evs.special_defense};
        if (!std::equal(values, values + 6, ev)) return false;
    }
    if (e.set_ivs) {
        const auto bits = u32(iv + 4);
        const std::uint8_t values[] = {e.ivs.hp,e.ivs.attack,e.ivs.defense,e.ivs.speed,e.ivs.special_attack,e.ivs.special_defense};
        for (std::size_t i = 0; i < 6; ++i) if (((bits >> (5 * i)) & 31u) != values[i]) return false;
    }
    if (e.set_origin) {
        const auto& o = e.origin;
        if (((iv[7] & 0x40) != 0) != o.egg || iv[1] != o.location ||
            (iv[2] & 0x7f) != o.level || ((iv[2] >> 7) & 0x0f) != o.game ||
            (iv[3] & 0x0f) != o.ball || ((iv[3] & 0x10) != 0) != o.trainer_female) return false;
    }
    return true;
}
}
PokemonEditResult edit_frlg_pokemon(const std::vector<std::uint8_t>& image, std::size_t block,
                                     const PokemonEditRequest& request) {
    PokemonEditResult result;
    const auto fail = [&](const char* reason) { result.reason = reason; return result; };
    if (image.size() != frlg_image_size) return fail("Unsupported image size");
    if (block >= 2) return fail("Invalid block");
    if (request.edit.set_pid && request.edit.pid == 0) return fail("The requested personality value is zero");
    const auto content = read_frlg_content(image, block);
    if (!content.available) { result.reason = content.unavailable_reason; return result; }
    const auto inspection = inspect_frlg_save(image);
    const auto& sections = inspection.slots[block].sections;
    std::size_t start = 0, length = 0, first = 0;
    if (request.place == PokemonPlace::box) {
        if (request.box >= 14 || request.slot >= 30) return fail("Invalid box or slot");
        start = 4 + 80 * (request.box * 30 + request.slot); length = 80; first = 5;
    } else if (request.place == PokemonPlace::party) {
        if (request.party >= 6) return fail("Invalid party slot");
        start = 0x38 + 100 * request.party; length = 100; first = 1;
    } else return fail("Invalid Pokemon place");
    std::array<std::size_t, 100> offsets{};
    std::array<std::uint8_t, 100> original{};
    std::array<bool, frlg_section_count> touched{};
    for (std::size_t i = 0; i < length; ++i) {
        const auto logical = start + i;
        const auto id = first + logical / 0xf80;
        const auto within = logical % 0xf80;
        if (id >= frlg_section_count || within >= frlg_section_used_length(id, sections[id].checksum_international)) return fail("Record outside section");
        offsets[i] = (block * frlg_section_count + sections[id].sector_position) * 0x1000 + within;
        original[i] = image[offsets[i]];
        touched[id] = true;
    }
    const auto before = decode(original.data());
    if (before.species == 0) return fail("The record holds no Pokemon");
    const auto written = write_frlg_pokemon(original.data(), length, request.edit);
    if (!written.ok) { result.reason = written.reason; return result; }
    auto copy = image;
    for (std::size_t i = 0; i < length; ++i) copy[offsets[i]] = written.record[i];
    for (std::size_t id = 0; id < frlg_section_count; ++id) if (touched[id]) {
        const auto base = (block * frlg_section_count + sections[id].sector_position) * 0x1000;
        const auto sum = frlg_sector_checksum(copy, base, frlg_section_used_length(id, sections[id].checksum_international));
        copy[base + 0xff6] = static_cast<std::uint8_t>(sum);
        copy[base + 0xff7] = static_cast<std::uint8_t>(sum >> 8);
    }
    std::array<std::uint8_t, 100> readback{};
    for (std::size_t i = 0; i < length; ++i) readback[i] = copy[offsets[i]];
    const auto after = decode(readback.data());
    if (!after.checksum || after.species != before.species || !requested_fields(readback.data(), after, request.edit)) return fail("Record verification failed");
    const auto level = check_frlg_legality(readback.data(), length).level;
    if (level != written.new_level || (request.edit.set_level && level != request.edit.level)) return fail("Level verification failed");
    for (std::size_t id = 0; id < frlg_section_count; ++id) if (touched[id]) {
        const auto base = (block * frlg_section_count + sections[id].sector_position) * 0x1000;
        if (u16(copy.data() + base + 0xff6) != frlg_sector_checksum(copy, base, frlg_section_used_length(id, sections[id].checksum_international))) return fail("Sector checksum verification failed");
    }
    result.ok = true;
    result.image = std::move(copy);
    result.old_level = written.old_level; result.new_level = written.new_level;
    result.old_pid = written.old_pid; result.new_pid = written.new_pid; result.changed_bytes = written.changed_bytes;
    return result;
}
}  // namespace pksm
