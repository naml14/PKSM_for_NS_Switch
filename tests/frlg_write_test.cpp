// Verifies the write path: a copy with a new amount of money, produced on the
// host, that is only returned after it was checked.
#include "pksm/frlg_content.hpp"
#include "pksm/frlg_save.hpp"
#include "pksm/frlg_write.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
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
}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"the copy changes only the money and its checksum", [] {
            const auto image = make_image(17, 16);
            const auto edit = set_frlg_money(image, 999999);
            require(edit.ok, "the edit failed");
            require(edit.block == 0, "the wrong block was edited");
            require(edit.old_money == initial_money, "the old money differs");
            require(edit.new_money == 999999, "the new money differs");
            require(!edit.changed_offsets.empty() && edit.changed_bytes <= 6,
                    "the changed byte count is not within the permitted set");
            const auto block_base = static_cast<std::size_t>(edit.block) * section_count * sector_size;
            const auto money_at = block_base + sector_size + money_offset;
            for (const auto offset : edit.changed_offsets) {
                const bool in_money = offset >= money_at && offset < money_at + 4;
                const bool in_checksum = offset >= block_base + sector_size + 0xFF6 &&
                                        offset < block_base + sector_size + 0xFF8;
                require(in_money || in_checksum, "a byte outside the permitted set changed");
            }
        }},
        {"the copy is complete and reads back the new amount", [] {
            const auto image = make_image(17, 16);
            const auto edit = set_frlg_money(image, 500000);
            require(edit.ok, "the edit failed");
            const auto inspection = inspect_frlg_save(edit.image);
            const auto& slot = inspection.slots[edit.block];
            require(slot.international_sections == section_count,
                    "the edited block is not complete any more");
            const auto content = read_frlg_content(edit.image, edit.block);
            require(content.available, "the edited block cannot be read");
            require(content.money == 500000, "the money does not read back");
        }},
        {"every other field is untouched", [] {
            const auto image = make_image(17, 16);
            const auto edit = set_frlg_money(image, 777);
            require(edit.ok, "the edit failed");
            const auto before = read_frlg_content(image, edit.block);
            const auto after = read_frlg_content(edit.image, edit.block);
            require(before.available && after.available, "a block cannot be read");
            require(before.player_name == after.player_name, "the player name changed");
            require(before.trainer_id == after.trainer_id && before.secret_id == after.secret_id,
                    "the identifiers changed");
            require(before.play_hours == after.play_hours && before.play_minutes == after.play_minutes,
                    "the play time changed");
            require(before.dex_owned == after.dex_owned && before.dex_seen == after.dex_seen,
                    "the Pokedex counts changed");
            require(before.party_count == after.party_count, "the party size changed");
            require(before.box_occupied == after.box_occupied, "the box occupancy changed");
            require(before.money != after.money, "the money did not change");
        }},
        {"the original image is not modified", [] {
            const auto image = make_image(17, 16);
            const auto original = image;
            (void)set_frlg_money(image, 4242);
            require(image == original, "the original image changed");
        }},
        {"the same amount changes nothing", [] {
            const auto image = make_image(17, 16);
            const auto edit = set_frlg_money(image, initial_money);
            require(edit.ok, "the edit failed");
            require(edit.changed_bytes == 0, "the byte count is not zero");
            require(edit.changed_offsets.empty(), "the changed offsets are not empty");
            require(edit.image == image, "the copy differs from the original");
        }},
        {"a wrong size is refused", [] {
            const auto edit = set_frlg_money(std::vector<std::uint8_t>(1000, 0), 1);
            require(!edit.ok, "the edit was accepted");
            require(edit.reason == "Unsupported image size", "the reason differs");
            require(edit.image.empty(), "an image was returned");
        }},
        {"a damaged block falls back to the sound one", [] {
            auto image = make_image(17, 16);
            image[0xFF8] ^= 1;  // break the signature of the first sector of the first block
            const auto edit = set_frlg_money(image, 1);
            require(edit.ok, "the edit failed");
            require(edit.block == 1, "the sound block was not chosen");
        }},
        {"no sound block is refused", [] {
            auto image = make_image(17, 16);
            image[0xFF8] ^= 1;   // the first sector of the first block
            image[14 * sector_size + 0xFF8] ^= 1;  // and of the second one
            const auto edit = set_frlg_money(image, 1);
            require(!edit.ok, "the edit was accepted");
            require(edit.reason == "No block with a complete matching checksum", "the reason differs");
        }},
        {"two blocks with the same counter are refused", [] {
            const auto image = make_image(17, 17);
            const auto edit = set_frlg_money(image, 1);
            require(!edit.ok, "the edit was accepted");
            require(edit.reason == "Ambiguous block counter", "the reason differs");
        }},
        {"the extreme amounts work", [] {
            for (const auto value : {0u, money_maximum}) {
                const auto image = make_image(17, 16);
                const auto edit = set_frlg_money(image, value);
                require(edit.ok, "the edit failed");
                const auto content = read_frlg_content(edit.image, edit.block);
                require(content.available && content.money == value, "the money does not read back");
            }
        }},
        {"an amount above the maximum the game accepts is refused", [] {
            const auto image = make_image(17, 16);
            const auto edit = set_frlg_money(image, money_maximum + 1);
            require(!edit.ok, "the edit was accepted");
            require(!edit.reason.empty(), "the refusal has no reason");
            require(edit.image.empty(), "a copy was handed over anyway");
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
