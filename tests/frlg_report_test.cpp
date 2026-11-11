#include "pksm/frlg_report.hpp"
#include "pksm/frlg_save.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace pksm;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void put16(std::vector<std::uint8_t>& image, std::size_t offset, std::uint16_t value) {
    image.at(offset) = static_cast<std::uint8_t>(value);
    image.at(offset + 1) = static_cast<std::uint8_t>(value >> 8);
}

void put32(std::vector<std::uint8_t>& image, std::size_t offset, std::uint32_t value) {
    put16(image, offset, static_cast<std::uint16_t>(value));
    put16(image, offset + 2, static_cast<std::uint16_t>(value >> 16));
}

constexpr std::size_t sector_size = 0x1000;
constexpr std::size_t slot_size = frlg_section_count * sector_size;

// Zeroed payloads make every checksum range agree, which the report states as
// an ambiguous match instead of guessing a language or a game revision.
std::vector<std::uint8_t> report_image() {
    std::vector<std::uint8_t> image(frlg_image_size, 0);
    for (std::size_t sector = 0; sector < frlg_section_count; ++sector) {
        const std::size_t base = sector * sector_size;
        put16(image, base + 0xFF4, static_cast<std::uint16_t>(sector));
        put32(image, base + 0xFF8, 0x08012025);
        put32(image, base + 0xFFC, 17);
    }
    // The second block keeps an invalid signature so the report shows a broken
    // block next to a healthy one.
    for (std::size_t sector = 0; sector < frlg_section_count; ++sector) {
        const std::size_t base = slot_size + sector * sector_size;
        put16(image, base + 0xFF4, static_cast<std::uint16_t>(sector));
        put32(image, base + 0xFFC, 18);
    }
    return image;
}

std::string expected_report() {
    std::string text =
        "FRLG save report (read-only; game identity and content are not verified)\n"
        "Block 0\n"
        "  save index: 17\n"
        "  checksummed sections: international 14/14, Japanese 14/14\n";
    for (std::size_t id = 0; id < frlg_section_count; ++id) {
        text += "  section " + std::to_string(id) + ": sector " + std::to_string(id) +
                ", international and Japanese (ambiguous)\n";
    }
    text +=
        "  warnings: none\n"
        "Block 1\n"
        "  save index: unavailable\n"
        "  checksummed sections: international 0/14, Japanese 0/14\n"
        "  warnings: invalid sector signature\n";
    return text;
}
} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"healthy block is reported section by section", [] {
            auto image = report_image();
            const auto original = image;
            require(format_frlg_report(image) == expected_report(), "report differs");
            require(image == original, "report modified the image");
        }},
        {"duplicate section ID is reported", [] {
            auto image = report_image();
            put16(image, sector_size + 0xFF4, 0);
            const auto report = format_frlg_report(image);
            require(report.find("warnings: duplicate section ID") != std::string::npos,
                    "duplicate warning missing");
            require(report.find("  section 0: sector 0,") != std::string::npos,
                    "first sector position missing");
        }},
        {"out of range section ID is reported", [] {
            auto image = report_image();
            put16(image, 0xFF4, 0xFFFF);
            const auto report = format_frlg_report(image);
            require(report.find("warnings: section ID out of range") != std::string::npos,
                    "out of range warning missing");
            require(report.find("  section 1: sector 1,") != std::string::npos,
                    "valid sections were dropped");
            require(report.find("  section 0:") == std::string::npos,
                    "out of range sector was reported as a section");
        }},
        {"unsupported image size is reported", [] {
            const std::vector<std::uint8_t> image(frlg_image_size - 1, 0);
            const auto report = format_frlg_report(image);
            require(report.find("Unsupported image size") != std::string::npos,
                    "unsupported size not reported");
            require(report.find("Block 0") == std::string::npos,
                    "unsupported image produced block details");
        }},
        {"mixed save indices are reported", [] {
            auto image = report_image();
            put32(image, sector_size + 0xFFC, 18);
            const auto report = format_frlg_report(image);
            require(report.find("  save index: mixed (possible partial or link save)") != std::string::npos,
                    "mixed save index not reported");
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
