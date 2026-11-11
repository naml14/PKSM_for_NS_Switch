// Emits independently specified synthetic fixtures; it never opens an output file.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {
constexpr std::size_t sector_size = 0x1000;
constexpr std::size_t section_count = 14;
constexpr std::size_t slot_size = section_count * sector_size;

void put16(std::vector<std::uint8_t>& image, std::size_t pos, std::uint16_t value) {
    image.at(pos) = static_cast<std::uint8_t>(value);
    image.at(pos + 1) = static_cast<std::uint8_t>(value >> 8);
}

void put32(std::vector<std::uint8_t>& image, std::size_t pos, std::uint32_t value) {
    put16(image, pos, static_cast<std::uint16_t>(value));
    put16(image, pos + 2, static_cast<std::uint16_t>(value >> 16));
}

std::size_t section_end(std::size_t id) {
    if (id == 0) return 0xF24;
    if (id == 4) return 0xEC0;
    if (id == 13) return 0x7D0;
    return 0xF80;
}

void make_slot(std::vector<std::uint8_t>& image, std::size_t slot,
               std::uint32_t index, std::size_t rotation, const std::string& kind) {
    for (std::size_t physical = 0; physical < section_count; ++physical) {
        const std::size_t id = (physical + rotation) % section_count;
        const std::size_t base = slot * slot_size + physical * sector_size;
        std::fill(image.begin() + base, image.begin() + base + sector_size, 0);

        // Independent fixed vectors: 0x01020304 + 0x00050006 -> 0x0411;
        // 0xFFFFFFFF + 2 wraps to 1; 0xFFFF0000 + 0x0000FFFF folds once to 0xFFFE.
        const bool overflow = kind == "overflow";
        const bool carry = kind == "carry";
        const std::uint32_t first = overflow ? 0xFFFFFFFF : carry ? 0xFFFF0000 : 0x01020304;
        const std::uint32_t last = overflow ? 2 : carry ? 0x0000FFFF : 0x00050006;
        put32(image, base, first);
        const std::size_t end = id == 4 && kind == "international" ? 0xEE8 : section_end(id);
        put32(image, base + end - 4, last);
        if (id == 4 && kind == "japanese") put32(image, base + 0xEE4, 0x00010000);
        // Put nonzero bytes immediately outside the longest checked data range.
        image.at(base + (id == 4 ? 0xEE8 : section_end(id))) = 0xA5;
        put16(image, base + 0xFF4, static_cast<std::uint16_t>(id));
        put16(image, base + 0xFF6, overflow ? 1 : carry ? 0xFFFE : 0x0411);
        put32(image, base + 0xFF8, 0x08012025);
        put32(image, base + 0xFFC, index);
    }
}

std::vector<std::uint8_t> fixture(const std::string& name) {
    std::vector<std::uint8_t> image(0x20000, 0xFF);
    const std::string kind = name == "partial-international" ? "international" :
                             name == "partial-japanese" ? "japanese" :
                             name == "international" || name == "japanese" ||
                             name == "overflow" || name == "carry" ? name : "both";
    make_slot(image, 0, name == "wrap" ? 0xFFFFFFFF : 17, name == "both" ? 5 : 0, kind);
    if (name == "both" || name == "fallback" || name == "wrap") {
        make_slot(image, 1, name == "wrap" ? 0 : 18, 9, "both");
    }
    if (name == "signature" || name == "fallback") image.at(0xFF8) ^= 1;
    if (name == "signature-last") image.at(13 * sector_size + 0xFF8) ^= 1;
    if (name == "section4-missing") image.at(4 * sector_size + 0xFF8) ^= 1;
    if (name == "id") put16(image, 0xFF4, 14);
    if (name == "id-high") put16(image, 0xFF4, 0xFFFF);
    if (name == "duplicate") put16(image, sector_size + 0xFF4, 0);
    if (name == "duplicate-last") put16(image, 13 * sector_size + 0xFF4, 0);
    if (name == "mixed" || name == "partial") put32(image, sector_size + 0xFFC, 18);
    if (name == "mixed-last") put32(image, 13 * sector_size + 0xFFC, 18);
    if (name == "partial" || name == "partial-international" || name == "partial-japanese") {
        image.at(7 * sector_size) ^= 1;
    }
    if (name == "checksum") image.at(0xFF6) ^= 1;
    if (name == "payload") image.at(0) ^= 1;
    if (name == "neither") put16(image, 4 * sector_size + 0xFF6, 0x1234);
    if (name.compare(0, 5, "tail-") == 0) {
        const unsigned id = static_cast<unsigned>(std::stoul(name.substr(5)));
        if (id >= section_count) throw std::runtime_error("bad section ID");
        image.at(id * sector_size + section_end(id) - 1) ^= 1;
    }
    if (name == "short") image.pop_back();
    if (name == "long") image.push_back(0);
    return image;
}
} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc != 2) {
        std::cerr << "Usage: frlg-fixture SCENARIO\n";
        return 2;
    }
    try {
        const auto image = fixture(argv[1]);
        std::cout.write(reinterpret_cast<const char*>(image.data()),
                        static_cast<std::streamsize>(image.size()));
        return std::cout ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
