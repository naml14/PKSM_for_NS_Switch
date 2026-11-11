#include "pksm/frlg_content_report.hpp"
#include <iostream>
#include <string>

int main() {
    pksm::FrlgContent c;
    c.unavailable_reason = "Missing section";
    int failed = 0;
    auto check = [&](bool ok, const char* name) { if (!ok) { ++failed; std::cerr << "FAIL " << name << '\n'; } };
    check(pksm::format_frlg_content(c, 1) == "content: unavailable (Missing section)\n", "unavailable");
    c.available = true; c.player_name = {0x12,0x34,0x56,0xff,0,0,0,0}; c.gender = 1;
    c.trainer_id=12345; c.secret_id=6789; c.play_hours=3; c.play_minutes=25; c.play_seconds=10;
    c.money=12345; c.party_count=1; c.dex_owned=12; c.dex_seen=30; c.box_occupied[0]=1;
    c.save_block1.resize(0x38+600); c.storage.resize(4+80*420);
    c.save_block1[0x38+0x20]=25; c.save_block1[0x38+0x54]=5;
    c.save_block1[0x38+0x56]=12; c.save_block1[0x38+0x58]=20;
    c.storage[4+0x20]=94; c.storage[4+0x1c]=94;
    c.save_block1[0x38+8]=0xbb; c.save_block1[0x38+9]=0xff; c.save_block1[0x38+0x12]=1;
    c.storage[4+8]=0x12; c.storage[4+9]=0x34; c.storage[4+10]=0xff;
    const std::string expected = "content: available\nplayer name: つゃカ [jp]\nplayer name codes: 12 34 56\ngender: female\ntrainer id: 12345 secret 6789\nplay time: 3h 25m 10s\nmoney: 12345\nparty: 1\npokedex owned: 12 seen: 30\nboxes occupied: 1 0 0 0 0 0 0 0 0 0 0 0 0 0\nparty 1: species 25 level 5 hp 12/20 nickname A checksum bad\nbox 1 slot 1: species 94 nickname つゃ checksum ok\n";
    check(pksm::format_frlg_content(c, 0) == expected, "full report");
    c.storage[4+8]=0x60; c.storage[4+9]=0x6f; c.storage[4+10]=0x8b; c.storage[4+11]=0xff; c.storage[4+0x12]=1;
    check(pksm::format_frlg_content(c, 0).find("box 1 slot 1: species 94 nickname タマゴ [jp] checksum ok\n") != std::string::npos, "Japanese fallback");
    c.player_name[0]=0xff; c.gender=2;
    auto s=pksm::format_frlg_content(c, 0);
    check(s.find("content: available\nplayer name codes: none\ngender: unknown\n") != std::string::npos, "empty name and unknown gender");
    c.save_block1[0x38+8]=0xff; c.storage[4+0x12]=1; c.storage[4+8]=0xff;
    s=pksm::format_frlg_content(c, 0);
    check(s.find("party 1: species 25 level 5 hp 12/20 checksum bad\nbox 1 slot 1: species 94 checksum ok\n") != std::string::npos, "empty nicknames");
    c.gender=0; check(pksm::format_frlg_content(c, 0).find("gender: male\n") != std::string::npos, "male");
    std::cout << 6-failed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
