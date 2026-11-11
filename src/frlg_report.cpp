#include "pksm/frlg_report.hpp"
#include "pksm/frlg_save.hpp"

#include <sstream>

namespace pksm {
namespace {
const char* ranges(bool international, bool japanese) {
    if (international && japanese) return "international and Japanese (ambiguous)";
    if (international) return "international only";
    if (japanese) return "Japanese only";
    return "neither";
}
} // namespace

std::string format_frlg_report(const std::vector<std::uint8_t>& image) {
    const auto inspection = inspect_frlg_save(image);
    if (!inspection.supported_size) return "Unsupported image size\n";
    std::ostringstream out;
    out << "FRLG save report (read-only; game identity and content are not verified)\n";
    for (std::size_t i = 0; i < inspection.slots.size(); ++i) {
        const auto& slot = inspection.slots[i];
        out << "Block " << i << "\n  save index: ";
        if (!slot.has_save_index) out << "unavailable";
        else if (slot.mixed_save_indices) out << "mixed (possible partial or link save)";
        else out << slot.save_index;
        out << "\n  checksummed sections: international " << slot.international_sections
            << "/14, Japanese " << slot.japanese_sections << "/14\n";
        for (std::size_t id = 0; id < slot.sections.size(); ++id) {
            const auto& section = slot.sections[id];
            if (!section.present) continue;
            out << "  section " << id << ": sector " << section.sector_position << ", "
                << ranges(section.checksum_international, section.checksum_japanese) << '\n';
        }
        out << "  warnings: ";
        bool warning = false;
        const auto append = [&](bool present, const char* text) {
            if (present) {
                if (warning) out << "; ";
                out << text;
                warning = true;
            }
        };
        append(slot.invalid_signature, "invalid sector signature");
        append(slot.section_id_out_of_range, "section ID out of range");
        append(slot.duplicate_section_id, "duplicate section ID");
        if (!warning) out << "none";
        out << '\n';
    }
    return out.str();
}
} // namespace pksm
