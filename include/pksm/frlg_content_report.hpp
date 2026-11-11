#pragma once

#include "pksm/frlg_content.hpp"

#include <cstddef>
#include <string>

namespace pksm {

// Renders one block without decoding language-dependent player names.
std::string format_frlg_content(const FrlgContent& content, std::size_t block);

}  // namespace pksm
