#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pksm/frlg_legality.hpp"
#include "pksm/frlg_pokemon_edit.hpp"
#include "pksm/frlg_pokemon_write.hpp"

namespace pksm {

// The values a Pokemon shows and that can be changed.
enum class PokemonField : std::uint8_t {
    level, nature, gender, shiny, nickname,
    iv_hp, iv_attack, iv_defense, iv_speed, iv_special_attack, iv_special_defense,
    ev_hp, ev_attack, ev_defense, ev_speed, ev_special_attack, ev_special_defense
};

// How many fields the editor has.
constexpr std::size_t pokemon_field_count = 17;

// The name of a field, in english.
const char* pokemon_field_name(PokemonField field) noexcept;

// A Pokemon the user can choose.
struct PokemonChoice {
    bool in_party = false;
    std::size_t box = 0;
    std::size_t slot = 0;  // the slot in the party, or the slot in the box
    std::string name;
    std::uint16_t species = 0;
    std::uint8_t level = 0;
};

// The editor of one Pokemon: the list of the ones that can be chosen, the
// values of the chosen one and the changes that have been asked for.
//
// The editor never modifies the image it is given. The traits that come from
// the personality value are not stored in the record, so changing one of them
// means looking for a personality value that gives the three of them at once.
class PokemonEditor {
public:
    // Fills the choices with the Pokemon of the block the game loads: the
    // team first and then the boxes, only the ones that are occupied.
    bool begin(const std::vector<std::uint8_t>& image, std::size_t block);

    const std::vector<PokemonChoice>& choices() const noexcept { return choices_; }
    std::size_t selected() const noexcept { return selected_; }
    void select_next() noexcept;
    void select_previous() noexcept;

    // Opens the chosen Pokemon for editing. It fails when the record cannot
    // be read.
    bool open(std::string& reason);

    PokemonField field() const noexcept { return field_; }
    void next_field() noexcept;
    void previous_field() noexcept;

    // Changes the value of the field. The direction is one for more and minus
    // one for less. It fails when the value cannot be there, for example when
    // no personality value gives the traits that were asked for.
    bool change(int direction, std::string& reason);

    // Sets the nickname from its text, with the encoder of the character
    // table. It fails when the text has characters that neither table has.
    bool set_nickname(const std::string& text, std::string& reason);

    // The value of a field as the record has it.
    std::string value(PokemonField field) const;
    // The value a field would have after the change.
    std::string proposed(PokemonField field) const;

    bool changed() const noexcept { return changed_; }
    // What the edit asks for, ready to be applied.
    const PokemonEdit& edit() const noexcept { return edit_; }
    // The legality of the record as it is now.
    const LegalityReport& legality() const noexcept { return legality_; }

    // Applies the edit to a copy of the image.
    PokemonEditResult apply(const std::vector<std::uint8_t>& image, std::size_t block) const;

private:
    // The bytes of the record that is being edited, and where it came from.
    bool read_record(const std::vector<std::uint8_t>& image, std::size_t block,
                     std::string& reason);
    // Looks for a personality value that gives the wanted traits.
    bool search_personality(std::uint8_t nature, std::uint8_t gender, bool shiny,
                            std::uint32_t& pid, std::string& reason) const;
    void refresh_legality();

    std::vector<PokemonChoice> choices_;
    std::size_t selected_ = 0;
    std::vector<std::uint8_t> image_;
    std::size_t block_ = 0;
    std::vector<std::uint8_t> record_;
    PokemonState state_{};
    PokemonEdit edit_{};
    PokemonField field_ = PokemonField::level;
    LegalityReport legality_{};
    std::uint8_t proposed_nature_ = 0;
    std::uint8_t proposed_gender_ = 0;
    bool proposed_shiny_ = false;
    std::string nickname_;
    bool open_ = false;
    bool changed_ = false;
};

}  // namespace pksm
