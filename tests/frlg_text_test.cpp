// Verifies the Gen 3 FRLG text decoding against the published character table.
#include "pksm/frlg_text.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace pksm;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string decode(const std::vector<std::uint8_t>& codes,
                   FrlgTextLanguage language = FrlgTextLanguage::international) {
    return decode_frlg_text(codes.data(), codes.size(), language);
}
} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"the real trainer name decodes", [] {
            // The trainer name of the inspected save, whose backup file is
            // named after it.
            const std::vector<std::uint8_t> codes = {0xBB, 0xE2, 0xD8, 0xE6, 0xD9, 0xE7};
            require(decode(codes) == "Andres", "trainer name differs");
        }},
        {"the Spanish letters decode", [] {
            const std::vector<std::uint8_t> upper = {0x02, 0x06, 0x5A, 0x0E, 0x12, 0x14};
            require(decode(upper) == "\xC3\x81\xC3\x89\xC3\x8D\xC3\x93\xC3\x9A\xC3\x91",
                    "uppercase Spanish letters differ");
            const std::vector<std::uint8_t> lower = {0x17, 0x1B, 0x6F, 0x23, 0x27, 0x29};
            require(decode(lower) == "\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA\xC3\xB1",
                    "lowercase Spanish letters differ");
            const std::vector<std::uint8_t> marks = {0x51, 0x52};
            require(decode(marks) == "\xC2\xBF\xC2\xA1", "inverted marks differ");
        }},
        {"the digit and letter ranges decode", [] {
            std::vector<std::uint8_t> digits;
            for (std::uint8_t code = 0xA1; code <= 0xAA; ++code) digits.push_back(code);
            require(decode(digits) == "0123456789", "digits differ");
            std::vector<std::uint8_t> upper;
            for (std::uint8_t code = 0xBB; code <= 0xD4; ++code) upper.push_back(code);
            require(decode(upper) == "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "uppercase differs");
            std::vector<std::uint8_t> lower;
            for (std::uint8_t code = 0xD5; code <= 0xEE; ++code) lower.push_back(code);
            require(decode(lower) == "abcdefghijklmnopqrstuvwxyz", "lowercase differs");
        }},
        {"the terminator ends the text", [] {
            const std::vector<std::uint8_t> codes = {0xBB, 0xFF, 0xBB};
            require(decode(codes) == "A", "terminator not honoured");
            const std::vector<std::uint8_t> only_term = {0xFF};
            require(decode(only_term).empty(), "terminator produced text");
            const std::vector<std::uint8_t> unbounded = {0xBB, 0xBC, 0xBD};
            require(decode_frlg_text_terminated(unbounded.data(), unbounded.size(),
                                                FrlgTextLanguage::international) == "ABC",
                    "terminated decode differs");
        }},
        {"an undefined code becomes a question mark", [] {
            const std::vector<std::uint8_t> codes = {0x0A, 0xBB};
            require(decode(codes) == "?A", "undefined code not reported");
        }},
        {"the language byte selects the table", [] {
            require(frlg_text_language_from_byte(0) == FrlgTextLanguage::japanese,
                    "byte 0 is not Japanese");
            require(frlg_text_language_from_byte(1) == FrlgTextLanguage::international,
                    "byte 1 is not international");
            require(frlg_text_language_from_byte(5) == FrlgTextLanguage::international,
                    "byte 5 is not international");
            const std::vector<std::uint8_t> codes = {0xBB, 0xBC};
            require(decode(codes, FrlgTextLanguage::international) == "AB",
                    "international decode differs");
            // The Japanese table holds kana: 0x60 is ta, 0x6F is ma and 0x8B is go.
            const std::vector<std::uint8_t> kana = {0x60, 0x6F, 0x8B};
            require(decode(kana, FrlgTextLanguage::japanese) ==
                        "\xE3\x82\xBF\xE3\x83\x9E\xE3\x82\xB4",
                    "Japanese decode differs");
        }},
        {"the egg name falls back to the Japanese table", [] {
            // An egg of the inspected save is named タマゴ while its language
            // byte says 1, so the named table leaves codes undefined.
            const std::vector<std::uint8_t> egg = {0x60, 0x6F, 0x8B, 0xFF};
            const auto named = decode_frlg_name(egg.data(), egg.size(), 1);
            require(named.fallback, "the fallback was not used");
            require(named.table == FrlgTextLanguage::japanese, "the wrong table was used");
            require(named.text == "\xE3\x82\xBF\xE3\x83\x9E\xE3\x82\xB4",
                    "the egg name differs");
        }},
        {"a name in its own language needs no fallback", [] {
            const std::vector<std::uint8_t> charmander = {0xBD, 0xC2, 0xBB, 0xCC, 0xC7,
                                                          0xBB, 0xC8, 0xBE, 0xBF, 0xCC};
            const auto named = decode_frlg_name(charmander.data(), charmander.size(), 7);
            require(!named.fallback, "the fallback was used");
            require(named.text == "CHARMANDER", "the name differs");
        }},
        {"the input is not modified", [] {
            std::vector<std::uint8_t> codes = {0xBB, 0xE2, 0xD8, 0xE6, 0xD9, 0xE7, 0xFF};
            const auto original = codes;
            (void)decode(codes);
            require(codes == original, "the buffer changed");
        }},
        {"a null buffer is handled", [] {
            require(decode_frlg_text(nullptr, 4, FrlgTextLanguage::international).empty(),
                    "null buffer produced text");
        }}
    };

    std::size_t failed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS: " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
