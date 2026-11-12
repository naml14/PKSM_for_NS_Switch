#include "pksm/frlg_pokemon_editor.hpp"

#include "pksm/frlg_content.hpp"
#include "pksm/frlg_pokemon.hpp"
#include "pksm/frlg_pokemon_traits.hpp"
#include "pksm/frlg_save.hpp"
#include "pksm/frlg_species_tables.hpp"
#include "pksm/frlg_text.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace pksm {
namespace {
// The geometry of the records of a Pokemon inside a save.
//
// The boxes are in the sections five to thirteen, with a header of four bytes
// and records of eighty bytes; the team is in the section one, at the offset
// 0x38, with records of a hundred bytes. A record does not fit inside one
// section, so it can touch two of them.
//
// pret/pokefirered src/pokemon.c, the function GetSubstruct, and the
// structure of a save block.
// https://raw.githubusercontent.com/pret/pokefirered/master/src/pokemon.c
constexpr std::size_t box_record_length = 80;
constexpr std::size_t party_record_length = 100;
constexpr std::size_t storage_header = 4;
constexpr std::size_t box_slots = 30;
constexpr std::size_t box_count = 14;
constexpr std::size_t section_length = 0xF80;
constexpr std::size_t team_offset = 0x38;
constexpr std::size_t team_sections = 1;

std::string number(int value) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return std::string(buffer);
}

std::uint32_t read32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::uint8_t iv_at(const PokemonIv& ivs, std::size_t index) {
    const std::uint8_t values[6] = {ivs.hp, ivs.attack, ivs.defense,
                                     ivs.speed, ivs.special_attack, ivs.special_defense};
    return values[index];
}

void set_iv_at(PokemonIv& ivs, std::size_t index, std::uint8_t value) {
    std::uint8_t values[6] = {ivs.hp, ivs.attack, ivs.defense,
                               ivs.speed, ivs.special_attack, ivs.special_defense};
    values[index] = value;
    ivs.hp = values[0];
    ivs.attack = values[1];
    ivs.defense = values[2];
    ivs.speed = values[3];
    ivs.special_attack = values[4];
    ivs.special_defense = values[5];
}

std::uint8_t ev_at(const PokemonEv& evs, std::size_t index) {
    const std::uint8_t values[6] = {evs.hp, evs.attack, evs.defense,
                                     evs.speed, evs.special_attack, evs.special_defense};
    return values[index];
}

void set_ev_at(PokemonEv& evs, std::size_t index, std::uint8_t value) {
    std::uint8_t values[6] = {evs.hp, evs.attack, evs.defense,
                               evs.speed, evs.special_attack, evs.special_defense};
    values[index] = value;
    evs.hp = values[0];
    evs.attack = values[1];
    evs.defense = values[2];
    evs.speed = values[3];
    evs.special_attack = values[4];
    evs.special_defense = values[5];
}

bool is_iv_field(PokemonField field) {
    const auto value = static_cast<std::size_t>(field);
    return value >= static_cast<std::size_t>(PokemonField::iv_hp) &&
           value <= static_cast<std::size_t>(PokemonField::iv_special_defense);
}

std::size_t field_index(PokemonField field) {
    const auto value = static_cast<std::size_t>(field);
    if (is_iv_field(field)) return value - static_cast<std::size_t>(PokemonField::iv_hp);
    return value - static_cast<std::size_t>(PokemonField::ev_hp);
}
}  // namespace

const char* pokemon_field_name(PokemonField field) noexcept {
    switch (field) {
        case PokemonField::level: return "Level";
        case PokemonField::nature: return "Nature";
        case PokemonField::gender: return "Gender";
        case PokemonField::shiny: return "Shiny";
        case PokemonField::nickname: return "Nickname";
        case PokemonField::iv_hp: return "IV health";
        case PokemonField::iv_attack: return "IV attack";
        case PokemonField::iv_defense: return "IV defense";
        case PokemonField::iv_speed: return "IV speed";
        case PokemonField::iv_special_attack: return "IV special attack";
        case PokemonField::iv_special_defense: return "IV special defense";
        case PokemonField::ev_hp: return "EV health";
        case PokemonField::ev_attack: return "EV attack";
        case PokemonField::ev_defense: return "EV defense";
        case PokemonField::ev_speed: return "EV speed";
        case PokemonField::ev_special_attack: return "EV special attack";
        case PokemonField::ev_special_defense: return "EV special defense";
    }
    return "";
}

bool PokemonEditor::begin(const std::vector<std::uint8_t>& image, std::size_t block) {
    choices_.clear();
    selected_ = 0;
    open_ = false;
    changed_ = false;
    if (image.size() != frlg_image_size || block >= 2) return false;
    // The image and the block are kept so that the record of the chosen
    // Pokemon can be found later.
    image_ = image;
    block_ = block;
    const auto content = read_frlg_content(image, block);
    if (!content.available) return false;
    const auto party = read_frlg_party(content);
    for (std::size_t i = 0; i < party.size(); ++i) {
        if (party[i].status != PokemonReadStatus::ok) continue;
        PokemonChoice choice;
        choice.in_party = true;
        choice.slot = i;
        choice.species = party[i].species;
        choice.level = party[i].level;
        const auto name = decode_frlg_name(party[i].nickname.data(), party[i].nickname.size(),
                                          party[i].language);
        choice.name = name.text;
        choices_.push_back(choice);
    }
    for (std::size_t box = 0; box < box_count; ++box) {
        for (std::size_t slot = 0; slot < box_slots; ++slot) {
            const auto mon = read_frlg_box_mon(content, box, slot);
            if (mon.status != PokemonReadStatus::ok) continue;
            PokemonChoice choice;
            choice.in_party = false;
            choice.box = box;
            choice.slot = slot;
            choice.species = mon.species;
            const auto name = decode_frlg_name(mon.nickname.data(), mon.nickname.size(),
                                              mon.language);
            choice.name = name.text;
            choices_.push_back(choice);
        }
    }
    return true;
}

void PokemonEditor::select_next() noexcept {
    if (choices_.empty()) return;
    selected_ = (selected_ + 1) % choices_.size();
}

void PokemonEditor::select_previous() noexcept {
    if (choices_.empty()) return;
    selected_ = (selected_ + choices_.size() - 1) % choices_.size();
}

bool PokemonEditor::read_record(const std::vector<std::uint8_t>& image, std::size_t block,
                                std::string& reason) {
    record_.clear();
    if (image.size() != frlg_image_size || block >= 2) {
        reason = "The save has an unexpected size";
        return false;
    }
    const auto inspection = inspect_frlg_save(image);
    const auto& sections = inspection.slots[block].sections;
    const auto& choice = choices_[selected_];
    std::size_t start = 0;
    std::size_t length = 0;
    std::size_t first = 0;
    if (choice.in_party) {
        if (choice.slot >= 6) {
            reason = "The team slot does not exist";
            return false;
        }
        start = team_offset + party_record_length * choice.slot;
        length = party_record_length;
        first = team_sections;
    } else {
        if (choice.box >= box_count || choice.slot >= box_slots) {
            reason = "The box slot does not exist";
            return false;
        }
        start = storage_header + box_record_length * (choice.box * box_slots + choice.slot);
        length = box_record_length;
        first = 5;
    }
    record_.assign(length, 0);
    for (std::size_t i = 0; i < length; ++i) {
        const auto logical = start + i;
        const auto id = first + logical / section_length;
        const auto within = logical % section_length;
        if (id >= frlg_section_count ||
            within >= frlg_section_used_length(id, sections[id].checksum_international)) {
            reason = "The record is outside the sections of the save";
            return false;
        }
        const auto sector = (block * frlg_section_count + sections[id].sector_position) * 0x1000;
        record_[i] = image[sector + within];
    }
    return true;
}

bool PokemonEditor::open(std::string& reason) {
    open_ = false;
    changed_ = false;
    if (choices_.empty()) {
        reason = "There is no Pokemon to edit";
        return false;
    }
    if (!read_record(image_, block_, reason)) return false;
    state_ = read_frlg_pokemon_state(record_.data(), record_.size());
    if (state_.species == 0) {
        reason = "The record holds no Pokemon";
        return false;
    }
    edit_ = PokemonEdit{};
    field_ = PokemonField::level;
    nickname_ = decode_frlg_name(state_.nickname.data(), state_.nickname.size(),
                                 record_[0x12]).text;
    proposed_nature_ = state_.nature;
    proposed_gender_ = state_.gender;
    proposed_shiny_ = state_.shiny;
    refresh_legality();
    open_ = true;
    return true;
}

void PokemonEditor::next_field() noexcept {
    field_ = static_cast<PokemonField>((static_cast<std::size_t>(field_) + 1) % pokemon_field_count);
}

void PokemonEditor::previous_field() noexcept {
    field_ = static_cast<PokemonField>(
        (static_cast<std::size_t>(field_) + pokemon_field_count - 1) % pokemon_field_count);
}

bool PokemonEditor::search_personality(std::uint8_t nature, std::uint8_t gender, bool shiny,
                                       std::uint32_t& pid, std::string& reason) const {
    PokemonTraitRequest request{};
    request.nature = nature;
    request.gender = static_cast<PokemonGender>(gender);
    request.shiny = shiny;
    request.gender_ratio = static_cast<std::uint8_t>(frlg_gender_ratio(state_.species));
    request.trainer_id = read32(record_.data() + 4);
    // A breeding can only produce a personality value of thirty one or less.
    request.bred = true;
    if (!frlg_find_pid(request, pid)) {
        reason = "No personality value gives those traits in a breeding";
        return false;
    }
    return true;
}

bool PokemonEditor::change(int direction, std::string& reason) {
    if (!open_) {
        reason = "No Pokemon is being edited";
        return false;
    }
    const auto step = direction >= 0 ? 1 : -1;
    switch (field_) {
        case PokemonField::level: {
            const int value = static_cast<int>(edit_.set_level ? edit_.level : state_.level) + step;
            if (value < 1 || value > 100) {
                reason = "The level goes from 1 to 100";
                return false;
            }
            edit_.set_level = true;
            edit_.level = static_cast<std::uint8_t>(value);
            break;
        }
        case PokemonField::nature: {
            const int value = (static_cast<int>(proposed_nature_) + step + 25) % 25;
            std::uint32_t pid = 0;
            if (!search_personality(static_cast<std::uint8_t>(value), proposed_gender_,
                                    proposed_shiny_, pid, reason))
                return false;
            edit_.set_pid = true;
            edit_.pid = pid;
            proposed_nature_ = static_cast<std::uint8_t>(value);
            break;
        }
        case PokemonField::gender: {
            const int value = (static_cast<int>(proposed_gender_) + step + 3) % 3;
            if (value == 2 && static_cast<std::uint8_t>(frlg_gender_ratio(state_.species)) != 255) {
                reason = "This species has no genderless form";
                return false;
            }
            std::uint32_t pid = 0;
            if (!search_personality(proposed_nature_, static_cast<std::uint8_t>(value),
                                    proposed_shiny_, pid, reason))
                return false;
            edit_.set_pid = true;
            edit_.pid = pid;
            proposed_gender_ = static_cast<std::uint8_t>(value);
            break;
        }
        case PokemonField::shiny: {
            const bool shiny = proposed_shiny_ ? (step < 0) : (step > 0);
            std::uint32_t pid = 0;
            if (!search_personality(proposed_nature_, proposed_gender_, shiny, pid, reason))
                return false;
            edit_.set_pid = true;
            edit_.pid = pid;
            proposed_shiny_ = shiny;
            break;
        }
        case PokemonField::nickname:
            reason = "The nickname is written with the on screen keyboard";
            return false;
        default: {
            // The individual values and the effort values.
            const auto is_iv = static_cast<std::size_t>(field_) >=
                               static_cast<std::size_t>(PokemonField::iv_hp) &&
                               static_cast<std::size_t>(field_) <=
                               static_cast<std::size_t>(PokemonField::iv_special_defense);
            const auto index = static_cast<std::size_t>(field_) -
                               (is_iv ? static_cast<std::size_t>(PokemonField::iv_hp)
                                      : static_cast<std::size_t>(PokemonField::ev_hp));
            if (is_iv) {
                const int value = static_cast<int>(edit_.set_ivs ? iv_at(edit_.ivs, index)
                                                                 : iv_at(state_.ivs, index)) + step;
                if (value < 0 || value > 31) {
                    reason = "An individual value goes from 0 to 31";
                    return false;
                }
                edit_.set_ivs = true;
                set_iv_at(edit_.ivs, index, static_cast<std::uint8_t>(value));
            } else {
                const int value = static_cast<int>(edit_.set_evs ? ev_at(edit_.evs, index)
                                                                 : ev_at(state_.evs, index)) + step;
                if (value < 0 || value > 252) {
                    reason = "An effort value goes from 0 to 252";
                    return false;
                }
                edit_.set_evs = true;
                set_ev_at(edit_.evs, index, static_cast<std::uint8_t>(value));
            }
            break;
        }
    }
    changed_ = true;
    refresh_legality();
    return true;
}

bool PokemonEditor::set_nickname(const std::string& text, std::string& reason) {
    if (!open_) {
        reason = "No Pokemon is being edited";
        return false;
    }
    std::array<std::uint8_t, 10> encoded{};
    const auto written = encode_frlg_name(encoded.data(), encoded.size(), text);
    if (written == 0 && !text.empty()) {
        reason = "The name has characters that neither table has";
        return false;
    }
    edit_.set_nickname = true;
    edit_.nickname = encoded;
    changed_ = true;
    return true;
}

std::string PokemonEditor::value(PokemonField field) const {
    switch (field) {
        case PokemonField::level: return number(state_.level);
        case PokemonField::nature: return frlg_nature_name(state_.nature);
        case PokemonField::gender: return state_.gender == 1 ? "female"
                                         : state_.gender == 2 ? "genderless" : "male";
        case PokemonField::shiny: return state_.shiny ? "yes" : "no";
        case PokemonField::nickname: return nickname_;
        default: break;
    }
    if (is_iv_field(field)) {
        const auto index = field_index(field);
        return number(iv_at(state_.ivs, index));
    }
    const auto index = field_index(field);
    return number(ev_at(state_.evs, index));
}

std::string PokemonEditor::proposed(PokemonField field) const {
    switch (field) {
        case PokemonField::level:
            return number(edit_.set_level ? edit_.level : state_.level);
        case PokemonField::nature: return frlg_nature_name(proposed_nature_);
        case PokemonField::gender:
            return proposed_gender_ == 1 ? "female"
                   : proposed_gender_ == 2 ? "genderless" : "male";
        case PokemonField::shiny: return proposed_shiny_ ? "yes" : "no";
        case PokemonField::nickname:
            if (!edit_.set_nickname) return nickname_;
            return decode_frlg_name(edit_.nickname.data(), edit_.nickname.size(), 0).text;
        default: break;
    }
    if (is_iv_field(field)) {
        const auto index = field_index(field);
        return number(edit_.set_ivs ? iv_at(edit_.ivs, index) : iv_at(state_.ivs, index));
    }
    const auto index = field_index(field);
    return number(edit_.set_evs ? ev_at(edit_.evs, index) : ev_at(state_.evs, index));
}

void PokemonEditor::refresh_legality() {
    legality_ = check_frlg_legality(record_.data(), record_.size());
}

PokemonEditResult PokemonEditor::apply(const std::vector<std::uint8_t>& image,
                                       std::size_t block) const {
    PokemonEditResult result;
    if (!open_) {
        result.reason = "No Pokemon is being edited";
        return result;
    }
    PokemonEditRequest request;
    request.place = choices_[selected_].in_party ? PokemonPlace::party : PokemonPlace::box;
    request.box = choices_[selected_].box;
    request.slot = choices_[selected_].slot;
    request.party = choices_[selected_].slot;
    request.edit = edit_;
    return edit_frlg_pokemon(image, block, request);
}

}  // namespace pksm
