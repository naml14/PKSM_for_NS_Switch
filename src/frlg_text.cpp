#include "pksm/frlg_text.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace pksm {
namespace {

// The Gen 3 international character table, transcribed from the international
// block of the pokefirered decompilation charmap.txt. Every non-Japanese
// language shares these codes, so the Spanish revision uses them too.
// A null entry is a code the format leaves undefined; the entries that the
// format defines as byte sequences, such as PK or POKEBLOCK, carry their text.
constexpr std::array<const char16_t*, 256> international_table = {
    /* 00 */ u" ", u"À", u"Á", u"Â", u"Ç", u"È", u"É", u"Ê",
    /* 08 */ u"Ë", u"Ì", nullptr, u"Î", u"Ï", u"Ò", u"Ó", u"Ô",
    /* 10 */ u"Œ", u"Ù", u"Ú", u"Û", u"Ñ", u"ß", u"à", u"á",
    /* 18 */ nullptr, u"ç", u"è", u"é", u"ê", u"ë", u"ì", nullptr,
    /* 20 */ u"î", u"ï", u"ò", u"ó", u"ô", u"œ", u"ù", u"ú",
    /* 28 */ u"û", u"ñ", u"º", u"ª", u"ER", u"&", u"+", nullptr,
    /* 30 */ nullptr, nullptr, nullptr, nullptr, u"LV", u"=", u";", nullptr,
    /* 38 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 40 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 48 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 50 */ nullptr, u"¿", u"¡", u"PK", nullptr, u"POKEBLOCK", nullptr, nullptr,
    /* 58 */ nullptr, nullptr, u"Í", u"%", u"(", u")", nullptr, nullptr,
    /* 60 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 68 */ u"â", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, u"í",
    /* 70 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 78 */ nullptr, u"↑", u"↓", u"←", u"→", nullptr, nullptr, nullptr,
    /* 80 */ nullptr, nullptr, nullptr, nullptr, u"E", u"<", u">", nullptr,
    /* 88 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 90 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 98 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* A0 */ u"RE", u"0", u"1", u"2", u"3", u"4", u"5", u"6",
    /* A8 */ u"7", u"8", u"9", u"!", u"?", u".", u"-", u"·",
    /* B0 */ u"…", u"“", u"”", u"‘", u"’", u"♂", u"♀", u"¥",
    /* B8 */ u",", u"×", u"/", u"A", u"B", u"C", u"D", u"E",
    /* C0 */ u"F", u"G", u"H", u"I", u"J", u"K", u"L", u"M",
    /* C8 */ u"N", u"O", u"P", u"Q", u"R", u"S", u"T", u"U",
    /* D0 */ u"V", u"W", u"X", u"Y", u"Z", u"a", u"b", u"c",
    /* D8 */ u"d", u"e", u"f", u"g", u"h", u"i", u"j", u"k",
    /* E0 */ u"l", u"m", u"n", u"o", u"p", u"q", u"r", u"s",
    /* E8 */ u"t", u"u", u"v", u"w", u"x", u"y", u"z", u"▶",
    /* F0 */ u":", u"Ä", u"Ö", u"Ü", u"ä", u"ö", u"ü", nullptr,
    /* F8 */ nullptr, nullptr, nullptr, nullptr, u"＋", nullptr, nullptr, u"$",
};

constexpr std::uint8_t terminator = 0xFF;

// The Gen 3 Japanese character table, transcribed from the Japanese blocks of
// the pokefirered decompilation charmap.txt. The international revisions carry
// these glyphs as well, which is why a name can keep the Japanese spelling in
// every language, as the name of an egg does.
constexpr std::array<const char16_t*, 256> japanese_table = {
    /* 00 */ u"　", u"あ", u"い", u"う", u"え", u"お", u"か", u"き",
    /* 08 */ u"く", u"け", u"こ", u"さ", u"し", u"す", u"せ", u"そ",
    /* 10 */ u"た", u"ち", u"つ", u"て", u"と", u"な", u"に", u"ぬ",
    /* 18 */ u"ね", u"の", u"は", u"ひ", u"ふ", u"へ", u"ほ", u"ま",
    /* 20 */ u"み", u"む", u"め", u"も", u"や", u"ゆ", u"よ", u"ら",
    /* 28 */ u"り", u"る", u"れ", u"ろ", u"わ", u"を", u"ん", u"ぁ",
    /* 30 */ u"ぃ", u"ぅ", u"ぇ", u"ぉ", u"ゃ", u"ゅ", u"ょ", u"が",
    /* 38 */ u"ぎ", u"ぐ", u"げ", u"ご", u"ざ", u"じ", u"ず", u"ぜ",
    /* 40 */ u"ぞ", u"だ", u"ぢ", u"づ", u"で", u"ど", u"ば", u"び",
    /* 48 */ u"ぶ", u"べ", u"ぼ", u"ぱ", u"ぴ", u"ぷ", u"ぺ", u"ぽ",
    /* 50 */ u"っ", u"ア", u"イ", u"ウ", u"エ", u"オ", u"カ", u"キ",
    /* 58 */ u"ク", u"ケ", u"コ", u"サ", u"シ", u"ス", u"セ", u"ソ",
    /* 60 */ u"タ", u"チ", u"ツ", u"テ", u"ト", u"ナ", u"ニ", u"ヌ",
    /* 68 */ u"ネ", u"ノ", u"ハ", u"ヒ", u"フ", u"ヘ", u"ホ", u"マ",
    /* 70 */ u"ミ", u"ム", u"メ", u"モ", u"ヤ", u"ユ", u"ヨ", u"ラ",
    /* 78 */ u"リ", u"ル", u"レ", u"ロ", u"ワ", u"ヲ", u"ン", u"ァ",
    /* 80 */ u"ィ", u"ゥ", u"ェ", u"ォ", u"ャ", u"ュ", u"ョ", u"ガ",
    /* 88 */ u"ギ", u"グ", u"ゲ", u"ゴ", u"ザ", u"ジ", u"ズ", u"ゼ",
    /* 90 */ u"ゾ", u"ダ", u"ヂ", u"ヅ", u"デ", u"ド", u"バ", u"ビ",
    /* 98 */ u"ブ", u"ベ", u"ボ", u"パ", u"ピ", u"プ", u"ペ", u"ポ",
    /* A0 */ u"ッ", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* A8 */ nullptr, nullptr, nullptr, u"！", u"？", u"。", u"ー", nullptr,
    /* B0 */ u"‥", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* B8 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* C0 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* C8 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* D0 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* D8 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* E0 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* E8 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* F0 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* F8 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};

// Appends one UTF-16 code unit as UTF-8. Every character of the table is in
// the basic multilingual plane, so three bytes are enough.
void append_utf8(std::string& out, char16_t unit) {
    const auto value = static_cast<std::uint32_t>(unit);
    if (value < 0x80) {
        out.push_back(static_cast<char>(value));
    } else if (value < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (value >> 6)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (value >> 12)));
        out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    }
}

} // namespace

std::string decode_frlg_text(const std::uint8_t* codes, std::size_t length,
                              FrlgTextLanguage language) {
    std::string result;
    if (codes == nullptr) return result;
    const auto& table = language == FrlgTextLanguage::japanese ? japanese_table : international_table;
    for (std::size_t i = 0; i < length; ++i) {
        const std::uint8_t code = codes[i];
        if (code == terminator) break;
        const char16_t* entry = table[code];
        if (entry == nullptr) {
            result.push_back('?');
            continue;
        }
        for (std::size_t j = 0; entry[j] != u'\0'; ++j) append_utf8(result, entry[j]);
    }
    return result;
}

std::string decode_frlg_text_terminated(const std::uint8_t* codes, std::size_t length,
                                        FrlgTextLanguage language) {
    if (codes == nullptr) return {};
    std::size_t used = 0;
    while (used < length && codes[used] != terminator) ++used;
    return decode_frlg_text(codes, used, language);
}

FrlgTextLanguage frlg_text_language_from_byte(std::uint8_t language) {
    return language == 0 ? FrlgTextLanguage::japanese : FrlgTextLanguage::international;
}

DecodedName decode_frlg_name(const std::uint8_t* codes, std::size_t length,
                              std::uint8_t language_byte) {
    DecodedName result;
    if (codes == nullptr) return result;
    std::size_t used = 0;
    while (used < length && codes[used] != terminator) ++used;
    result.table = frlg_text_language_from_byte(language_byte);
    result.text = decode_frlg_text(codes, used, result.table);
    if (result.text.find('?') == std::string::npos) return result;
    const auto other = result.table == FrlgTextLanguage::international
                           ? FrlgTextLanguage::japanese
                           : FrlgTextLanguage::international;
    const std::string alternative = decode_frlg_text(codes, used, other);
    if (alternative.find('?') == std::string::npos) {
        result.text = alternative;
        result.table = other;
        result.fallback = true;
    }
    return result;
}

} // namespace pksm
