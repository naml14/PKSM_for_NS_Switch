#include "pksm/frlg_content_report.hpp"
#include "pksm/frlg_pokemon.hpp"
#include "pksm/frlg_text.hpp"

#include <iomanip>
#include <sstream>

namespace pksm {
std::string format_frlg_content(const FrlgContent& content, std::size_t block) {
    (void)block;
    std::ostringstream out;
    if (!content.available) {
        out << "content: unavailable (" << content.unavailable_reason << ")\n";
        return out.str();
    }
    out << "content: available\n";
    const auto player_name = decode_frlg_name(content.player_name.data(), content.player_name.size(), 1);
    if (!player_name.text.empty()) out << "player name: " << player_name.text << (player_name.fallback ? " [jp]" : "") << '\n';
    out << "player name codes:";
    bool any = false;
    for (auto code : content.player_name) {
        if (code == 0xff) break;
        out << ' ' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << unsigned(code);
        any = true;
    }
    if (!any) out << " none";
    const char* gender = content.gender == 0 ? "male" : content.gender == 1 ? "female" : "unknown";
    out << std::dec << "\ngender: " << gender
        << "\ntrainer id: " << content.trainer_id << " secret " << content.secret_id
        << "\nplay time: " << content.play_hours << "h " << unsigned(content.play_minutes) << "m " << unsigned(content.play_seconds) << "s"
        << "\nmoney: " << content.money << "\nparty: " << unsigned(content.party_count)
        << "\npokedex owned: " << content.dex_owned << " seen: " << content.dex_seen << "\nboxes occupied:";
    for (auto count : content.box_occupied) out << ' ' << count;
    out << '\n';
    const auto party = read_frlg_party(content);
    for (std::size_t i=0; i<party.size(); ++i) {
        const auto& mon=party[i];
        if (mon.status == PokemonReadStatus::ok) {
            out << "party " << i+1 << ": species " << mon.species << " level " << unsigned(mon.level)
                << " hp " << mon.current_hp << '/' << mon.max_hp;
            const auto nickname = decode_frlg_name(mon.nickname.data(), mon.nickname.size(), mon.language);
            if (!nickname.text.empty()) out << " nickname " << nickname.text << (nickname.fallback ? " [jp]" : "");
            out << " checksum " << (mon.checksum_valid ? "ok" : "bad") << '\n';
        }
    }
    for (std::size_t box=0; box<14; ++box) for (std::size_t slot=0; slot<30; ++slot) {
        const auto mon=read_frlg_box_mon(content, box, slot);
        if (mon.status == PokemonReadStatus::ok) {
            out << "box " << box+1 << " slot " << slot+1 << ": species " << mon.species;
            const auto nickname = decode_frlg_name(mon.nickname.data(), mon.nickname.size(), mon.language);
            if (!nickname.text.empty()) out << " nickname " << nickname.text << (nickname.fallback ? " [jp]" : "");
            out << " checksum " << (mon.checksum_valid ? "ok" : "bad") << '\n';
        }
    }
    return out.str();
}
}  // namespace pksm
