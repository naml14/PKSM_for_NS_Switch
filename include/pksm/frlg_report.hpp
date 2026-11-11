#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pksm {

// Reports checksum observations without interpreting the image contents.
std::string format_frlg_report(const std::vector<std::uint8_t>& image);

} // namespace pksm
