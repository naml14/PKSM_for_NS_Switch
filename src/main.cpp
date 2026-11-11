#include "pksm/frlg_save.hpp"
#include "pksm/frlg_report.hpp"
#include "pksm/frlg_content_report.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {
const char* checksum_ranges(bool international, bool japanese) {
    if (international && japanese) return "international and Japanese (ambiguous)";
    if (international) return "international only";
    if (japanese) return "Japanese only";
    return "neither";
}
} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // Use the same output bytes on Windows and other hosts, including redirected output.
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    constexpr const char* usage = "Usage: frlg-inspect [--report | --content] SAVE\n";
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << usage << "Read-only FRLG checksum inspection; game identity is not verified.\n"
                  << "--report prints the per-section status of both save blocks.\n"
                  << "--content prints decoded content of both save blocks.\n";
        return 0;
    }
    const bool report = argc == 3 && std::string(argv[1]) == "--report";
    const bool content = argc == 3 && std::string(argv[1]) == "--content";
    if ((!report && !content && argc != 2) ||
        (argc == 2 && (std::string(argv[1]) == "--report" || std::string(argv[1]) == "--content")) ||
        ((report || content) && (std::string(argv[2]) == "--report" || std::string(argv[2]) == "--content"))) {
        std::cerr << usage;
        return 2;
    }

    std::ifstream file(argv[(report || content) ? 2 : 1], std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "error: cannot read input file\n";
        return 1;
    }
    // One extra byte detects oversized inputs without allocating their full size.
    std::vector<std::uint8_t> image(pksm::frlg_image_size + 1);
    file.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (file.bad()) {
        std::cerr << "error: cannot read input file\n";
        return 1;
    }
    if (file.gcount() != static_cast<std::streamsize>(pksm::frlg_image_size)) {
        std::cerr << "error: expected exactly 131072 bytes\n";
        return 1;
    }
    image.resize(pksm::frlg_image_size);
    const pksm::SaveInspection result = pksm::inspect_frlg_save(image);
    bool any_complete = false;
    if (report || content) {
        if (report) std::cout << pksm::format_frlg_report(image);
        else for (std::size_t i = 0; i < result.slots.size(); ++i) {
            std::cout << "Save content (read-only; game identity, language and revision are not verified)\nBlock "
                      << i << '\n' << pksm::format_frlg_content(pksm::read_frlg_content(image, i), i);
        }
        for (const auto& slot : result.slots) {
            any_complete = any_complete || slot.international_sections == pksm::frlg_section_count ||
                           slot.japanese_sections == pksm::frlg_section_count;
        }
        if (!any_complete) {
            std::cerr << "error: no slot has complete matching checksums\n";
            return 1;
        }
        return 0;
    }
    for (std::size_t i = 0; i < result.slots.size(); ++i) {
        const auto& slot = result.slots[i];
        const bool international = slot.international_sections == 14;
        const bool japanese = slot.japanese_sections == 14;
        any_complete = any_complete || international || japanese;
        std::cout << "Slot " << i << ": checksummed sections: international "
                  << slot.international_sections << "/14, Japanese " << slot.japanese_sections
                  << "/14; save index ";
        if (!slot.has_save_index) {
            std::cout << "unavailable";
        } else if (slot.mixed_save_indices) {
            std::cout << "mixed (possible partial or link save)";
        } else {
            std::cout << slot.save_index;
        }
        std::cout << "; complete checksum ranges: ";
        if (international || japanese) std::cout << checksum_ranges(international, japanese);
        else std::cout << "none";
        std::cout << "; section 4 checksums: ";
        if (slot.section_four_observed) {
            std::cout << checksum_ranges(slot.section_four_international, slot.section_four_japanese);
        } else {
            std::cout << "not observed";
        }
        if (slot.invalid_signature) std::cout << "; invalid sector signature";
        if (slot.section_id_out_of_range) std::cout << "; section ID out of range";
        if (slot.duplicate_section_id) std::cout << "; duplicate section ID";
        std::cout << '\n';
    }
    std::cout << "Game identity, language, and content are not verified.\n";
    if (!any_complete) {
        std::cerr << "error: no slot has complete matching checksums\n";
        return 1;
    }
    return 0;
}
