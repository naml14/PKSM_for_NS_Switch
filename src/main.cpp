#include "pksm/frlg_save.hpp"
#include "pksm/frlg_report.hpp"
#include "pksm/frlg_content_report.hpp"
#include "pksm/frlg_write.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
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
    constexpr const char* usage =
        "Usage: frlg-inspect [--report | --content] SAVE\n"
        "       frlg-inspect --set-money VALUE SAVE [OUTPUT]\n";
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << usage << "Read-only FRLG checksum inspection; game identity is not verified.\n"
                  << "--report prints the per-section status of both save blocks.\n"
                  << "--content prints decoded content of both save blocks.\n"
                  << "--set-money produces a verified copy with a new amount of money.\n";
        return 0;
    }
    const bool report = argc == 3 && std::string(argv[1]) == "--report";
    const bool content = argc == 3 && std::string(argv[1]) == "--content";
    const bool set_money = argc >= 4 && argc <= 5 && std::string(argv[1]) == "--set-money";
    if ((!report && !content && !set_money && argc != 2) ||
        (argc == 2 && (std::string(argv[1]) == "--report" || std::string(argv[1]) == "--content" ||
                       std::string(argv[1]) == "--set-money")) ||
        ((report || content) && (std::string(argv[2]) == "--report" || std::string(argv[2]) == "--content")) ||
        (std::string(argv[1]) == "--set-money" && !set_money)) {
        std::cerr << usage;
        return 2;
    }
    std::uint32_t money_value = 0;
    if (set_money) {
        const std::string value(argv[2]);
        errno = 0;
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos ||
            errno != 0 || end == value.c_str() || *end != '\0' || parsed > 4294967295ULL) {
            std::cerr << usage;
            return 2;
        }
        money_value = static_cast<std::uint32_t>(parsed);
    }

    const char* input_path = set_money ? argv[3] : ((report || content) ? argv[2] : argv[1]);
    std::vector<std::uint8_t> image;
    if (std::string(input_path) == "-") {
        // A dash reads the save from standard input, in binary mode.
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        image.assign(pksm::frlg_image_size + 1, 0);
        std::size_t read = 0;
        while (read < image.size()) {
            std::cin.read(reinterpret_cast<char*>(image.data() + read),
                          static_cast<std::streamsize>(image.size() - read));
            const auto got = std::cin.gcount();
            if (got <= 0) break;
            read += static_cast<std::size_t>(got);
        }
        if (read != pksm::frlg_image_size) {
            std::cerr << "error: expected exactly 131072 bytes\n";
            return 1;
        }
        image.resize(pksm::frlg_image_size);
    } else {
        std::ifstream file(input_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "error: cannot read input file\n";
            return 1;
        }
        // One extra byte detects oversized inputs without allocating their full size.
        image.assign(pksm::frlg_image_size + 1, 0);
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
    }

    if (set_money) {
        const auto edit = pksm::set_frlg_money(image, money_value);
        if (!edit.ok) {
            std::cerr << "error: " << edit.reason << '\n';
            return 1;
        }
        std::cout << "block edited: " << edit.block << '\n'
                  << "money: " << edit.old_money << " -> " << edit.new_money << '\n'
                  << "bytes changed: " << edit.changed_bytes << '\n'
                  << "verification: the copy is complete and reads back the new amount\n";
        if (argc == 5) {
            const std::string output(argv[4]);
            const std::string input(input_path);
            const bool same_file = output == input ||
                (output.size() > 2 && output.compare(0, 2, "./") == 0 && output.substr(2) == input);
            if (same_file) {
                std::cerr << "error: the output must not be the input file\n";
                return 1;
            }
            std::ofstream out(output, std::ios::binary);
            if (!out.is_open()) {
                std::cerr << "error: cannot write the output file\n";
                return 1;
            }
            out.write(reinterpret_cast<const char*>(edit.image.data()),
                      static_cast<std::streamsize>(edit.image.size()));
            if (!out) {
                std::cerr << "error: cannot write the output file\n";
                return 1;
            }
            out.close();
            std::cout << "written: " << output << '\n';
        }
        return 0;
    }
    const pksm::SaveInspection result = pksm::inspect_frlg_save(image);
    // The exit status rule is the same for every mode, so it is computed once
    // and the modes cannot drift apart.
    bool any_complete = false;
    for (const auto& slot : result.slots) {
        any_complete = any_complete || slot.international_sections == pksm::frlg_section_count ||
                       slot.japanese_sections == pksm::frlg_section_count;
    }
    if (report || content) {
        if (report) std::cout << pksm::format_frlg_report(image);
        else for (std::size_t i = 0; i < result.slots.size(); ++i) {
            std::cout << "Save content (read-only; game identity, language and revision are not verified)\nBlock "
                      << i << '\n' << pksm::format_frlg_content(pksm::read_frlg_content(image, i), i);
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
