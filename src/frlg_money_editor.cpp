#include "pksm/frlg_money_editor.hpp"

#include <cstdio>

namespace pksm {
namespace {

// The place value of a digit, counted from the most significant one.
std::uint32_t place_value(std::size_t digit) {
    std::uint32_t value = 100000;
    for (std::size_t i = 0; i < digit && value > 0; ++i) value /= 10;
    return value;
}

}  // namespace

void apply_money_editor_action(MoneyEditor& editor, MoneyEditorAction action) noexcept {
    if (editor.digit >= MoneyEditor::digit_count) editor.digit = 0;
    switch (action) {
        case MoneyEditorAction::left:
            editor.digit = (editor.digit + MoneyEditor::digit_count - 1) % MoneyEditor::digit_count;
            return;
        case MoneyEditorAction::right:
            editor.digit = (editor.digit + 1) % MoneyEditor::digit_count;
            return;
        case MoneyEditorAction::up: {
            const auto place = place_value(editor.digit);
            if (place == 0) return;
            if (editor.value > money_maximum - place) {
                editor.value = money_maximum;
                return;
            }
            editor.value += place;
            return;
        }
        case MoneyEditorAction::down: {
            const auto place = place_value(editor.digit);
            if (place == 0) return;
            editor.value = editor.value < place ? 0 : editor.value - place;
            return;
        }
    }
}

std::string format_money(std::uint32_t value) {
    char buffer[16];
    const auto shown = value > money_maximum ? money_maximum : value;
    std::snprintf(buffer, sizeof(buffer), "%06u", shown);
    return std::string(buffer);
}

}  // namespace pksm
