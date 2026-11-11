#include "pksm/frlg_pokemon.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace pksm {
namespace {
std::uint16_t u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (std::uint16_t(p[1]) << 8));
}
std::uint32_t u32(const std::uint8_t* p) {
    return std::uint32_t(u16(p)) | (std::uint32_t(u16(p + 2)) << 16);
}

PokemonSummary decode(const std::uint8_t* record, std::size_t length) {
    PokemonSummary result;
    // The layout is fixed: a boxed record holds the identity, the names and the
    // encrypted substructures, and a party record adds the party statistics on
    // top. A shorter buffer cannot be read, so it is reported as unavailable
    // instead of being read out of range.
    constexpr std::size_t box_record_length = 0x50;
    constexpr std::size_t party_record_length = 0x64;
    if (record == nullptr || length < box_record_length) {
        result.status = PokemonReadStatus::unavailable_content;
        return result;
    }
    result.personality = u32(record);
    result.ot_id = u32(record + 4);

    // pret/pokefirered src/pokemon.c:2797-2814 and PKHeX
    // PKHeX.Core/PKM/Util/PokeCrypto.cs:98-107,340-345: each secure
    // u32 is XORed with personality XOR full OT ID.
    // https://raw.githubusercontent.com/pret/pokefirered/master/src/pokemon.c
    // https://raw.githubusercontent.com/kwsch/PKHeX/master/PKHeX.Core/PKM/Util/PokeCrypto.cs
    std::array<std::uint8_t, 48> decrypted{};
    const auto key = result.personality ^ result.ot_id;
    for (std::size_t i = 0; i < decrypted.size(); ++i)
        decrypted[i] = static_cast<std::uint8_t>(record[0x20 + i] ^ (key >> (8 * (i % 4))));

    // pokemon.c:2817-2858,2863-2892 maps canonical types to stored
    // positions by personality % 24; PokeCrypto.cs:62-69,354-389 confirms
    // the permutation direction. pokemon.c:3083-3085 and pokemon.h:8-15,
    // 91-125 identify species as the first u16 of canonical type 0 (growth).
    // https://raw.githubusercontent.com/pret/pokefirered/master/include/pokemon.h
    constexpr std::uint8_t growth_position[24] = {
        0,0,0,0,0,0,1,1,2,3,2,3,1,1,2,3,2,3,1,1,2,3,2,3
    };
    static_assert(sizeof(growth_position) == 24, "The permutation table must cover every row");
    constexpr std::size_t substructure_length = 12;
    result.species = u16(decrypted.data() + substructure_length * growth_position[result.personality % 24]);

    // An unused slot keeps zeroed encrypted data, so the decrypted species
    // reads as 0, while its metadata bytes may still hold residues. Emptiness
    // therefore follows the species and not the whole record.
    if (result.species == 0) {
        result.status = PokemonReadStatus::empty_record;
        result.species = 0;
        result.personality = 0;
        result.ot_id = 0;
        return result;
    }

    // pokemon.c:2069-2090 and PokeCrypto.cs:405-409 sum the decrypted
    // 0x20-0x4f u16 words modulo 65536 and compare with 0x1c.
    std::uint16_t sum = 0;
    for (std::size_t i = 0; i < decrypted.size(); i += 2)
        sum = static_cast<std::uint16_t>(sum + u16(decrypted.data() + i));
    result.checksum_valid = sum == u16(record + 0x1c);

    result.status = PokemonReadStatus::ok;
    result.present = true;
    std::copy_n(record + 8, result.nickname.size(), result.nickname.begin());
    result.language = record[0x12];
    if (length >= party_record_length) {
        result.level = record[0x54];
        result.current_hp = u16(record + 0x56);
        result.max_hp = u16(record + 0x58);
    }
    return result;
}
}  // namespace

std::array<PokemonSummary, 6> read_frlg_party(const FrlgContent& content) {
    std::array<PokemonSummary, 6> result{};
    constexpr std::size_t offset = 0x38, size = 100;
    if (!content.available || content.save_block1.size() < offset + size * result.size())
        return result;
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = decode(content.save_block1.data() + offset + i * size, size);
    return result;
}

PokemonSummary read_frlg_box_mon(const FrlgContent& content, std::size_t box, std::size_t slot) {
    PokemonSummary result;
    if (box >= 14 || slot >= 30) {
        result.status = PokemonReadStatus::invalid_index;
        return result;
    }
    const std::size_t offset = 4 + 80 * (box * 30 + slot);
    if (!content.available || content.storage.size() < offset + 80)
        return result;
    return decode(content.storage.data() + offset, 80);
}
}  // namespace pksm
