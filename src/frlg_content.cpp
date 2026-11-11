#include "pksm/frlg_content.hpp"
#include "pksm/frlg_save.hpp"

#include <algorithm>

namespace pksm {
namespace {
// The used length of every section of a save block, from the pokefirered
// decompilation. Section 4 has a shorter Japanese range.
constexpr std::size_t section2_length = 0xF24;
constexpr std::size_t section_length = 0xF80;
constexpr std::size_t section4_international_length = 0xEE8;
constexpr std::size_t section4_japanese_length = 0xEC0;
constexpr std::size_t section13_length = 0x7D0;

// The storage holds the boxes: a short header, then fixed size records.
constexpr std::size_t storage_header = 4;
constexpr std::size_t record_size = 80;
constexpr std::size_t box_count = 14;
constexpr std::size_t box_slots = 30;

std::uint16_t read16(const std::vector<std::uint8_t>& b, std::size_t p) {
    return static_cast<std::uint16_t>(b[p] | (static_cast<std::uint16_t>(b[p+1]) << 8));
}
std::uint32_t read32(const std::vector<std::uint8_t>& b, std::size_t p) {
    return static_cast<std::uint32_t>(read16(b,p)) | (static_cast<std::uint32_t>(read16(b,p+2)) << 16);
}
std::uint32_t bit_count(const std::vector<std::uint8_t>& b, std::size_t p) {
    std::uint32_t count=0;
    for (std::size_t i=0; i<49; ++i) {
        auto value=b[p+i];
        while (value) { count += value & 1u; value >>= 1; }
    }
    return count;
}
}
FrlgContent read_frlg_content(const std::vector<std::uint8_t>& image, std::size_t block) {
    FrlgContent result;
    if (image.size()!=frlg_image_size) { result.unavailable_reason="Unsupported image size"; return result; }
    if (block>=2) { result.unavailable_reason="Invalid block"; return result; }
    const auto inspection=inspect_frlg_save(image);
    const auto& sections=inspection.slots[block].sections;
    // Check duplicates first so a duplicate and its displaced missing section are distinct.
    for (const auto& section : sections) {
        if (section.duplicate) { result.unavailable_reason="Duplicate section"; return result; }
    }
    for (const auto& section : sections) {
        if (!section.present) { result.unavailable_reason="Missing section"; return result; }
        if (!section.checksum_international && !section.checksum_japanese) {
            result.unavailable_reason="Section checksum failed"; return result;
        }
    }
    // Every read below is bounded by the image and by the reassembled object, so a
    // short image or an unexpected sector position reports itself as
    // unavailable instead of reading out of range.
    auto append=[&](std::vector<std::uint8_t>& target, std::size_t id, std::size_t length) {
        const auto base=block*frlg_section_count*0x1000+sections[id].sector_position*0x1000;
        if (base>image.size() || length>image.size()-base) return false;
        target.insert(target.end(),image.begin()+base,image.begin()+base+length);
        return true;
    };
    if (!append(result.save_block2,0,section2_length)) {
        result.unavailable_reason="Section data out of range"; return result;
    }
    for (std::size_t i=1;i<=4;++i) {
        const auto length = i==4
            ? (sections[i].checksum_international?section4_international_length:section4_japanese_length)
            : section_length;
        if (!append(result.save_block1,i,length)) {
            result.unavailable_reason="Section data out of range"; return result;
        }
    }
    for (std::size_t i=5;i<=13;++i) {
        const auto length = i==13?section13_length:section_length;
        if (!append(result.storage,i,length)) {
            result.unavailable_reason="Section data out of range"; return result;
        }
    }
    const auto& b2=result.save_block2;
    std::copy_n(b2.begin(),8,result.player_name.begin());
    result.gender=b2[8]; result.trainer_id=read16(b2,10); result.secret_id=read16(b2,12);
    result.play_hours=read16(b2,14); result.play_minutes=b2[16]; result.play_seconds=b2[17];
    result.dex_owned=bit_count(b2,0x28); result.dex_seen=bit_count(b2,0x5c);
    result.party_count=result.save_block1[0x34];
    result.money=read32(result.save_block1,0x290)^read32(b2,0xf20);
    for (std::size_t box=0;box<box_count;++box) {
        for (std::size_t slot=0;slot<box_slots;++slot) {
            const auto offset = storage_header+record_size*(box*box_slots+slot);
            if (offset>result.storage.size() || record_size>result.storage.size()-offset) continue;
            const auto first = result.storage.begin()+offset;
            if (std::any_of(first,first+record_size,[](std::uint8_t b){return b!=0;})) ++result.box_occupied[box];
        }
    }
    result.available=true;
    return result;
}
} // namespace pksm
