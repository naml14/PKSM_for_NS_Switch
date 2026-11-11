#include "pksm/frlg_content.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
void put16(Bytes& b, std::size_t p, std::uint16_t v) { b.at(p) = v & 255; b.at(p+1) = v >> 8; }
void put32(Bytes& b, std::size_t p, std::uint32_t v) { put16(b,p,v & 65535); put16(b,p+2,v >> 16); }
std::uint32_t get32(const Bytes& b, std::size_t p) {
    return std::uint32_t(b.at(p)) | (std::uint32_t(b.at(p+1)) << 8) |
           (std::uint32_t(b.at(p+2)) << 16) | (std::uint32_t(b.at(p+3)) << 24);
}
void checksum(Bytes& b, std::size_t id, std::size_t length) {
    const auto base = id * 0x1000;
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < length; i += 4) sum += get32(b,base+i);
    put16(b,base+0xff6,static_cast<std::uint16_t>(sum+(sum>>16)));
}
Bytes fixture(bool japanese = false) {
    Bytes b(0x20000);
    for (std::size_t i=0; i<14; ++i) {
        auto base=i*0x1000;
        put16(b,base+0xff4,static_cast<std::uint16_t>(i));
        put32(b,base+0xff8,0x08012025); put32(b,base+0xffc,42);
    }
    for (std::size_t i=0;i<8;++i) b[i]=static_cast<std::uint8_t>(i+1);
    b[8]=1; put32(b,10,0x56781234); put16(b,14,321); b[16]=59; b[17]=58;
    b[0x28]=0xff; b[0x28+48]=0x81; b[0x5c]=0x55;
    put32(b,0xf20,0x12345678); b[0x1000+0x34]=6;
    put32(b,0x1000+0x290,0x12345678 ^ 987654);
    b[5*0x1000+4+80*31+7]=1;
    if (japanese) b[4*0x1000+0xee4]=1;
    for (std::size_t i=0;i<14;++i)
        checksum(b,i,i==0?0xf24:i==4?(japanese?0xec0:0xee8):i==13?0x7d0:0xf80);
    return b;
}
int total=0, failed=0;
void check(bool b, const char* name) { ++total; if (!b) { ++failed; std::cerr << "FAIL: " << name << '\n'; } }
void unavailable(const Bytes& b, std::size_t block, const char* label) {
    const auto before=b; auto c=pksm::read_frlg_content(b,block);
    check(!c.available && !c.unavailable_reason.empty(),label);
    check(b==before,"unavailable input unchanged");
}
}
int main() {
    auto b=fixture(); const auto before=b; auto c=pksm::read_frlg_content(b,0);
    check(c.available && c.unavailable_reason.empty(),"available");
    check(b==before,"input unchanged");
    check(c.player_name==std::array<std::uint8_t,8>{1,2,3,4,5,6,7,8} && c.gender==1,"name gender");
    check(c.trainer_id==0x1234 && c.secret_id==0x5678,"ids");
    check(c.play_hours==321 && c.play_minutes==59 && c.play_seconds==58,"time");
    check(c.dex_owned==10 && c.dex_seen==4,"dex");
    check(c.party_count==6,"party");
    check(get32(c.save_block1,0x290)==(0x12345678u^987654u) && c.money==987654,"money xor");
    check(c.box_occupied[0]==0 && c.box_occupied[1]==1 && c.box_occupied[13]==0,"boxes");
    check(c.save_block1.size()==0x3d68 && c.save_block2.size()==0xf24 && c.storage.size()==0x83d0,"lengths");
    auto j=fixture(true); auto jc=pksm::read_frlg_content(j,0);
    check(jc.available && jc.save_block1.size()==0x3d40,"japanese length");
    auto short_image=b; short_image.pop_back(); unavailable(short_image,0,"size");
    auto missing=b; missing[0xff8]^=1; unavailable(missing,0,"missing");
    auto duplicate=b; put16(duplicate,0x1000+0xff4,0); unavailable(duplicate,0,"duplicate");
    auto invalid=b; invalid[5*0x1000]^=1; unavailable(invalid,0,"checksum");
    unavailable(b,2,"block");
    std::cout << total-failed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
