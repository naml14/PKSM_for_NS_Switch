// Verifies the console write path: the bytes that reach the save file, the
// verification that runs after the write and the rollback when that
// verification fails.
#include "pksm/frlg_console_write.hpp"
#include "pksm/frlg_content.hpp"
#include "pksm/frlg_money_editor.hpp"
#include "pksm/frlg_save.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace pksm;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::size_t sector_size = 0x1000;
constexpr std::size_t section_count = 14;
constexpr std::size_t money_offset = 0x290;   // inside SaveBlock1, section 1
constexpr std::size_t key_offset = 0xF20;     // inside SaveBlock2, section 0
constexpr std::uint32_t key_value = 0xAABBCCDD;
constexpr std::uint32_t initial_money = 1234;

void put16(std::vector<std::uint8_t>& bytes, std::size_t p, std::uint16_t value) {
    bytes.at(p) = static_cast<std::uint8_t>(value);
    bytes.at(p + 1) = static_cast<std::uint8_t>(value >> 8);
}
void put32(std::vector<std::uint8_t>& bytes, std::size_t p, std::uint32_t value) {
    put16(bytes, p, static_cast<std::uint16_t>(value));
    put16(bytes, p + 2, static_cast<std::uint16_t>(value >> 16));
}
std::size_t used_length(std::size_t section) {
    if (section == 0) return 0xF24;
    if (section == 4) return 0xEE8;
    if (section == 13) return 0x7D0;
    return 0xF80;
}

// Two complete blocks with the given save counters and a known amount of money.
std::vector<std::uint8_t> make_image(std::uint32_t first, std::uint32_t second) {
    std::vector<std::uint8_t> image(frlg_image_size, 0);
    for (std::size_t block = 0; block < 2; ++block) {
        const auto counter = block == 0 ? first : second;
        const auto block_base = block * section_count * sector_size;
        for (std::size_t sector = 0; sector < section_count; ++sector) {
            const auto base = block_base + sector * sector_size;
            image[base] = static_cast<std::uint8_t>(block + 1);
            put32(image, base + 0x10, static_cast<std::uint32_t>(sector));
            if (sector == 0) put32(image, base + key_offset, key_value);
            if (sector == 1) put32(image, base + money_offset, initial_money ^ key_value);
            put16(image, base + 0xFF4, static_cast<std::uint16_t>(sector));
            put32(image, base + 0xFF8, 0x08012025);
            put32(image, base + 0xFFC, counter);
            const auto sum = frlg_sector_checksum(image, base, used_length(sector));
            put16(image, base + 0xFF6, sum);
        }
    }
    return image;
}

std::filesystem::path scratch_directory() {
    if (const char* work = std::getenv("PKSM_TEST_WORK"))
        return std::filesystem::path(work) / "console-write";
    return std::filesystem::temp_directory_path() / "pksm-console-write";
}

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> bytes;
    char buffer[4096];
    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
        bytes.insert(bytes.end(), buffer,
                     buffer + static_cast<std::size_t>(in.gcount()));
    }
    return bytes;
}

const std::filesystem::path& scratch() {
    static const std::filesystem::path path = [] {
        std::filesystem::path dir = scratch_directory();
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        return dir;
    }();
    return path;
}
}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"the editor starts at zero on the most significant digit", [] {
            const MoneyEditor editor{};
            require(editor.value == 0, "the value does not start at zero");
            require(editor.digit == 0, "the first digit is not selected");
        }},
        {"the selected digit moves left and right", [] {
            MoneyEditor editor{};
            apply_money_editor_action(editor, MoneyEditorAction::right);
            require(editor.digit == 1, "the digit did not move right");
            apply_money_editor_action(editor, MoneyEditorAction::right);
            require(editor.digit == 2, "the digit did not move right again");
            apply_money_editor_action(editor, MoneyEditorAction::left);
            require(editor.digit == 1, "the digit did not move left");
        }},
        {"up adds the place of the selected digit", [] {
            MoneyEditor editor{};
            apply_money_editor_action(editor, MoneyEditorAction::up);
            require(editor.value == 100000, "the most significant digit did not add");
            apply_money_editor_action(editor, MoneyEditorAction::right);
            apply_money_editor_action(editor, MoneyEditorAction::up);
            require(editor.value == 110000, "the second digit did not add");
        }},
        {"down removes it and stops at zero", [] {
            MoneyEditor editor{};
            apply_money_editor_action(editor, MoneyEditorAction::down);
            require(editor.value == 0, "the value went below zero");
            editor.value = 500;
            for (std::size_t i = 0; i < MoneyEditor::digit_count - 1; ++i)
                apply_money_editor_action(editor, MoneyEditorAction::right);
            apply_money_editor_action(editor, MoneyEditorAction::down);
            require(editor.value == 499, "the last digit did not lose its place");
        }},
        {"the value never goes above the maximum the game accepts", [] {
            MoneyEditor editor{};
            editor.value = money_maximum;
            apply_money_editor_action(editor, MoneyEditorAction::up);
            require(editor.value == money_maximum, "the value went above the maximum");
        }},
        {"the editor never carries an amount above the maximum", [] {
            MoneyEditor editor{};
            editor.value = 1500000;
            apply_money_editor_action(editor, MoneyEditorAction::down);
            require(editor.value <= money_maximum, "the editor kept an out of range amount");
        }},
        {"the amount is shown with six digits", [] {
            require(format_money(0) == "000000", "zero is not six digits");
            require(format_money(1234) == "001234", "the amount is not padded");
            require(format_money(money_maximum) == "999999", "the maximum is not six digits");
        }},
        {"the write reaches the file and the money reads back", [] {
            const auto image = make_image(17, 16);
            const auto path = scratch() / "money.sav";
            write_file(path, image);
            const auto result = commit_frlg_money(path.string(), 4242);
            require(result.ok, "the write failed");
            require(!result.rolled_back, "the write was rolled back");
            require(result.old_money == initial_money, "the old money differs");
            require(result.new_money == 4242, "the new money differs");
            const auto written = read_file(path);
            require(written.size() == frlg_image_size, "the file changed its size");
            const auto content = read_frlg_content(written, 0);
            require(content.available, "the written file cannot be read");
            require(content.money == 4242, "the money does not read back from the file");
        }},
        {"only the money and its checksum changed on disk", [] {
            const auto image = make_image(17, 16);
            const auto path = scratch() / "diff.sav";
            write_file(path, image);
            const auto result = commit_frlg_money(path.string(), 500000);
            require(result.ok, "the write failed");
            const auto written = read_file(path);
            std::size_t differences = 0;
            for (std::size_t i = 0; i < image.size(); ++i) {
                if (image[i] == written[i]) continue;
                ++differences;
                const bool in_money = i >= sector_size + money_offset &&
                                      i < sector_size + money_offset + 4;
                const bool in_checksum = i >= sector_size + 0xFF6 && i < sector_size + 0xFF8;
                require(in_money || in_checksum, "a byte outside the permitted set changed");
            }
            require(differences == result.changed_bytes, "the reported count is not the real one");
        }},
        {"every section checksum is still valid after the write", [] {
            const auto image = make_image(17, 16);
            const auto path = scratch() / "checksums.sav";
            write_file(path, image);
            require(commit_frlg_money(path.string(), 777).ok, "the write failed");
            const auto inspection = inspect_frlg_save(read_file(path));
            require(inspection.slots[0].international_sections == section_count,
                    "the edited block is not complete any more");
            require(inspection.slots[1].international_sections == section_count,
                    "the other block was damaged");
        }},
        {"a file that is not a save is refused", [] {
            const auto path = scratch() / "short.sav";
            write_file(path, std::vector<std::uint8_t>(100, 0));
            const auto result = commit_frlg_money(path.string(), 100);
            require(!result.ok, "the write was accepted");
            require(!result.reason.empty(), "the refusal has no reason");
        }},
        {"a missing file is refused", [] {
            const auto result = commit_frlg_money((scratch() / "absent.sav").string(), 100);
            require(!result.ok, "the write was accepted");
            require(!result.reason.empty(), "the refusal has no reason");
        }},
        {"the plan keeps the original bytes for the rollback", [] {
            const auto image = make_image(17, 16);
            const auto plan = plan_frlg_money_write(image, 900);
            require(plan.ok, "the plan failed");
            require(plan.original == image, "the plan does not keep the original bytes");
            require(plan.bytes.size() == image.size(), "the plan changes the size");
            require(plan.money_offset == sector_size + money_offset, "the money is in the wrong place");
        }},
        {"a write to a file that is not there is refused and creates nothing", [] {
            const auto path = scratch() / "missing-directory" / "save.sav";
            const auto result = commit_frlg_money(path.string(), 4242);
            require(!result.ok, "the write was accepted");
            require(!result.reason.empty(), "the refusal has no reason");
            require(!std::filesystem::exists(path), "the write created the file");
            require(!std::filesystem::exists(path.parent_path()),
                    "the write created the directory");
        }},
        {"a file with mixed bytes can be restored", [] {
            const auto image = make_image(17, 16);
            const auto path = scratch() / "mixed.sav";
            // A write that failed halfway leaves the first half new and the
            // second half the original.
            auto mixed = image;
            for (std::size_t i = 0; i < mixed.size() / 2; ++i) mixed[i] = 0xA5;
            write_file(path, mixed);
            require(restore_frlg_save_file(path.string(), image), "the restore failed");
            require(read_file(path) == image, "the file does not hold the original bytes");
        }},
        {"a write is refused when the file changed since the plan was prepared", [] {
            const auto image = make_image(17, 16);
            const auto path = scratch() / "changed.sav";
            write_file(path, image);
            const auto plan = plan_frlg_money_write(image, 900);
            require(plan.ok, "the plan failed");
            auto other = image;
            other[0x5000] ^= 1;
            write_file(path, other);
            const auto result = apply_frlg_money_write(path.string(), plan);
            require(!result.ok, "the write was accepted");
            require(read_file(path) == other, "the file was written anyway");
        }},
        {"a block that matches only the Japanese range can be written", [] {
            auto image = make_image(17, 16);
            // A byte that only the international checksum of the section four
            // covers: the block stays complete under the Japanese range.
            image[4 * sector_size + 0xEC0] ^= 1;
            const auto path = scratch() / "japanese.sav";
            write_file(path, image);
            const auto result = commit_frlg_money(path.string(), 4242);
            require(result.ok, "the write failed");
            const auto content = read_frlg_content(read_file(path), 0);
            require(content.available, "the written file cannot be read");
            require(content.money == 4242, "the money does not read back");
        }},
        {"a plan that changes a byte outside the permitted set is refused before the file is touched", [] {
            const auto image = make_image(17, 16);
            const auto path = scratch() / "tampered.sav";
            write_file(path, image);
            auto plan = plan_frlg_money_write(image, 900);
            require(plan.ok, "the plan failed");
            plan.bytes[0x100] ^= 0xFF;  // a byte outside the permitted set
            const auto result = apply_frlg_money_write(path.string(), plan);
            require(!result.ok, "the tampered write was accepted");
            require(read_file(path) == image, "the file was touched anyway");
        }},
        {"a write that does not verify is rolled back", [] {
            const auto image = make_image(17, 16);
            const auto path = scratch() / "rollback.sav";
            write_file(path, image);
            auto plan = plan_frlg_money_write(image, 900);
            require(plan.ok, "the plan failed");
            plan.new_money = 901;  // the bytes hold another amount, so the check fails
            const auto result = apply_frlg_money_write(path.string(), plan);
            require(!result.ok, "the write was accepted");
            require(result.rolled_back, "the write was not rolled back");
            require(read_file(path) == image, "the file does not hold the original bytes");
        }},
    };
    int passed = 0;
    int failed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS: " << test.first << "\n";
            ++passed;
        } catch (const std::exception& error) {
            std::cout << "FAIL: " << test.first << ": " << error.what() << "\n";
            ++failed;
        }
    }
    std::error_code ignored;
    std::filesystem::remove_all(scratch(), ignored);
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
