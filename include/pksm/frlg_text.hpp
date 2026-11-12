#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace pksm {

// Gen 3 name languages. Only the distinction that changes the decoding is
// kept: the language byte 0 is Japanese and every other value uses the shared
// international table.
enum class FrlgTextLanguage { japanese, international };

// Decodes Gen 3 character codes into UTF-8. A code with no character becomes
// '?'. The 0xFF code terminates the string and is not part of the output.
std::string decode_frlg_text(const std::uint8_t* codes, std::size_t length,
                              FrlgTextLanguage language);

// Decodes up to the first 0xFF code or the end of the buffer.
std::string decode_frlg_text_terminated(const std::uint8_t* codes, std::size_t length,
                                        FrlgTextLanguage language);

// Maps a Gen 3 language byte to the table to use. Only byte 0 selects the
// Japanese table; every other value uses the shared international table.
FrlgTextLanguage frlg_text_language_from_byte(std::uint8_t language);

// A decoded name and the table that produced it.
struct DecodedName {
    std::string text;
    FrlgTextLanguage table = FrlgTextLanguage::international;
    // The table differs from the one the language byte names. A Japanese name
    // can carry a language byte that does not say so, for example the name of
    // an egg, so the other table is tried when the named one leaves a code
    // undefined.
    bool fallback = false;
};

// Decodes a name with the table its language byte names, falling back to the
// other table when the named one leaves a code undefined and the other one
// does not.
DecodedName decode_frlg_name(const std::uint8_t* codes, std::size_t length,
                              std::uint8_t language_byte);

// Encodes one name using a single table and pads unused capacity with 0xFF.
// Returns the number of character codes written, excluding padding.
std::size_t encode_frlg_name(std::uint8_t* destination, std::size_t capacity,
                             const std::string& text) noexcept;

}  // namespace pksm
