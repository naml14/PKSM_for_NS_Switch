#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace pksm {

// The game accepts at most this much money, so the editor never goes above it.
inline constexpr std::uint32_t money_maximum = 999999;

// The amount of money as the user edits it on the console.
struct MoneyEditor {
    std::uint32_t value = 0;
    std::size_t digit = 0;  // 0 is the most significant digit
    static constexpr std::size_t digit_count = 6;
};

// What one button press asks the editor to do.
enum class MoneyEditorAction { left, right, up, down };

// Applies one action to the editor. The selected digit never moves out of the
// six digits, and the value stays between zero and the maximum the game
// accepts.
void apply_money_editor_action(MoneyEditor& editor, MoneyEditorAction action) noexcept;

// The value as six digits, with leading zeros.
std::string format_money(std::uint32_t value);

}  // namespace pksm
