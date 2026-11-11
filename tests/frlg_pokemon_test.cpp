#include "pksm/frlg_pokemon.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
using Status = pksm::PokemonReadStatus;
int total = 0, failed = 0;
void check(bool value, const char* name) {
    ++total;
    if (!value) { ++failed; std::cerr << "FAIL: " << name << '\n'; }
}
void put16(Bytes& b, std::size_t p, std::uint16_t v) {
    b[p] = static_cast<std::uint8_t>(v); b[p+1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(Bytes& b, std::size_t p, std::uint32_t v) {
    put16(b, p, static_cast<std::uint16_t>(v)); put16(b, p+2, static_cast<std::uint16_t>(v >> 16));
}
// Synthetic round trips do not prove correctness against a real save: this
// encryptor and the decoder share the same format assumptions.
Bytes record(std::uint32_t personality, bool party) {
    constexpr std::uint32_t ot = 0x12345678;
    // Each row maps canonical type to its stored position.
    constexpr unsigned positions[24][4] = {
        {0,1,2,3},{0,1,3,2},{0,2,1,3},{0,3,1,2},{0,2,3,1},{0,3,2,1},
        {1,0,2,3},{1,0,3,2},{2,0,1,3},{3,0,1,2},{2,0,3,1},{3,0,2,1},
        {1,2,0,3},{1,3,0,2},{2,1,0,3},{3,1,0,2},{2,3,0,1},{3,2,0,1},
        {1,2,3,0},{1,3,2,0},{2,1,3,0},{3,1,2,0},{2,3,1,0},{3,2,1,0}};
    Bytes b(party ? 100 : 80), plain(48);
    put32(b,0,personality); put32(b,4,ot);
    for (unsigned i=0;i<10;++i) b[8+i]=static_cast<std::uint8_t>(i+1);
    b[0x12]=3;
    for (unsigned i=0;i<24;++i) put16(plain,2*i,static_cast<std::uint16_t>(i+1));
    put16(plain,0,387);
    // 1+...+24, replacing 1 with 387 = 686.
    put16(b,0x1c,686);
    const auto key=personality^ot;
    for (unsigned type=0;type<4;++type) {
        for (unsigned i=0;i<12;++i) {
            const auto shift=8*(i%4);
            b[0x20+12*positions[personality%24][type]+i]=
                static_cast<std::uint8_t>(plain[type*12+i] ^ (key >> shift));
        }
    }
    if (party) { b[0x54]=42; put16(b,0x56,123); put16(b,0x58,200); }
    return b;
}
}
int main() {
    pksm::FrlgContent c; c.available=true; c.save_block1.resize(0x38+600); c.storage.resize(4+80*420);
    auto party=record(11,true); std::copy(party.begin(),party.end(),c.save_block1.begin()+0x38+500);
    auto box=record(0,false); std::copy(box.begin(),box.end(),c.storage.begin()+4);
    const auto before_party=c.save_block1, before_storage=c.storage;
    auto ps=pksm::read_frlg_party(c);
    check(ps[0].status==Status::empty_record && !ps[0].present,"empty party");
    check(ps[5].status==Status::ok && ps[5].present && ps[5].checksum_valid && ps[5].species==387 && ps[5].level==42 && ps[5].current_hp==123 && ps[5].max_hp==200,"party beyond count");
    auto s=pksm::read_frlg_box_mon(c,0,0);
    check(s.status==Status::ok && s.present && s.checksum_valid && s.species==387 && s.personality==0 && s.ot_id==0x12345678 && s.language==3 && s.nickname==std::array<std::uint8_t,10>{1,2,3,4,5,6,7,8,9,10} && s.level==0 && s.current_hp==0 && s.max_hp==0,"box fields and known checksum");
    check(pksm::read_frlg_box_mon(c,0,1).status==Status::empty_record,"empty box");
    check(c.save_block1==before_party && c.storage==before_storage,"input unchanged");
    c.storage[4+0x1c]^=1; s=pksm::read_frlg_box_mon(c,0,0);
    check(s.status==Status::ok && s.present && !s.checksum_valid && s.species==387,"bad checksum retains fields");
    for (unsigned i=0;i<24;++i) {
        auto r=record(i,false); std::copy(r.begin(),r.end(),c.storage.begin()+4+80*419);
        s=pksm::read_frlg_box_mon(c,13,29);
        check(s.status==Status::ok && s.checksum_valid && s.species==387 && s.personality==i,"permutation");
    }
    for (auto pair : {std::array<std::size_t,2>{14,0}, {0,30}, {999,0}}) {
        s=pksm::read_frlg_box_mon(c,pair[0],pair[1]);
        check(s.status==Status::invalid_index && !s.present && !s.checksum_valid && !s.species && !s.personality && !s.ot_id && !s.language && !s.level && !s.current_hp && !s.max_hp && s.nickname==std::array<std::uint8_t,10>{},"invalid index defaults");
    }
    c.available=false;
    check(pksm::read_frlg_box_mon(c,0,0).status==Status::unavailable_content,"unavailable box");
    check(pksm::read_frlg_box_mon(c,14,0).status==Status::invalid_index,"invalid precedence");
    ps=pksm::read_frlg_party(c);
    check(std::all_of(ps.begin(),ps.end(),[](const auto& x){ return x.status==Status::unavailable_content; }),"unavailable party");
    c.available=true; c.storage.resize(83); c.save_block1.resize(0x38+599);
    check(pksm::read_frlg_box_mon(c,0,0).status==Status::unavailable_content,"short storage");
    ps=pksm::read_frlg_party(c);
    check(std::all_of(ps.begin(),ps.end(),[](const auto& x){ return x.status==Status::unavailable_content; }),"short party area");

    // An unused slot keeps zeroed encrypted data while its metadata bytes may
    // still hold residues, so emptiness must follow the species and not the
    // whole record. The real save showed exactly this shape.
    {
        c.save_block1.resize(0x38+600);
        Bytes empty(100);
        empty[0x12]=2;   // language residue
        empty[8]='A';    // nickname residue
        empty[0x14]=3;   // original trainer name residue
        std::copy(empty.begin(),empty.end(),c.save_block1.begin()+0x38+100);
        ps=pksm::read_frlg_party(c);
        check(ps[1].status==Status::empty_record && !ps[1].present && !ps[1].checksum_valid,
              "empty slot with metadata residues");
        check(ps[1].species==0 && ps[1].personality==0 && ps[1].ot_id==0,
              "empty slot exposes no fields");
    }
    std::cout << total-failed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
