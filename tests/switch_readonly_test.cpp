#include "pksm/switch_readonly.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace pksm;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void put16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1) = static_cast<std::uint8_t>(value >> 8);
}

void put32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    put16(bytes, offset, static_cast<std::uint16_t>(value));
    put16(bytes, offset + 2, static_cast<std::uint16_t>(value >> 16));
}

// All-zero checksum payloads with independent signatures, section IDs and indexes.
std::vector<std::uint8_t> synthetic_image() {
    std::vector<std::uint8_t> image(frlg_image_size, 0);
    for (std::size_t slot = 0; slot < 2; ++slot) {
        for (std::size_t id = 0; id < 14; ++id) {
            const std::size_t base = (slot * 14 + id) * 0x1000;
            put16(image, base + 0xFF4, static_cast<std::uint16_t>(id));
            put32(image, base + 0xFF8, 0x08012025);
            put32(image, base + 0xFFC, static_cast<std::uint32_t>(17 + slot));
        }
    }
    return image;
}

SwitchProfileId selected_profile() {
    SwitchProfileId profile;
    profile.bytes.front() = 0x12;
    profile.bytes.back() = 0x34;
    return profile;
}

struct FakeOperations final : SwitchReadOnlyOperations {
    std::vector<std::uint8_t> source = synthetic_image();
    std::vector<std::string> events;
    std::uint64_t mounted_id = 0;
    SwitchProfileId mounted_profile{};
    std::string opened_filename;
    std::size_t requested_capacity = 0;
    int write_calls = 0;
    int commit_calls = 0;
    bool fail_mount = false;
    bool fail_open = false;
    bool fail_read = false;

    bool mount_read_only(std::uint64_t id, const SwitchProfileId& profile) noexcept override {
        events.emplace_back("mount-read-only");
        mounted_id = id;
        mounted_profile = profile;
        return !fail_mount;
    }
    bool open_read_only(const char* filename) noexcept override {
        events.emplace_back("open-read-only");
        opened_filename = filename;
        return !fail_open;
    }
    bool read_to_end(std::uint8_t* output, std::size_t capacity,
                     std::size_t& bytes_read) noexcept override {
        events.emplace_back("read");
        requested_capacity = capacity;
        bytes_read = std::min(capacity, source.size());
        std::copy_n(source.begin(), bytes_read, output);
        return !fail_read;
    }
    void close_file() noexcept override { events.emplace_back("close"); }
    void unmount() noexcept override { events.emplace_back("unmount"); }

    // Sentinels are deliberately not part of the coordinator's public interface.
    void write() { ++write_calls; }
    void commit() { ++commit_calls; }
};

SwitchInspectionResult exercise(FakeOperations& fake,
                                const std::optional<SwitchProfileId>& profile,
                                const std::optional<SwitchTitle>& title,
                                SwitchInspectionStatus expected,
                                const std::vector<std::string>& expected_events) {
    const auto original = fake.source;
    std::ostringstream captured_out;
    std::ostringstream captured_err;
    auto* old_out = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_err = std::cerr.rdbuf(captured_err.rdbuf());
    const auto result = inspect_selected_switch_save(fake, profile, title);
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);

    require(result.status == expected, "wrong status");
    require(fake.events == expected_events, "wrong operation or cleanup order");
    require(fake.source == original, "synthetic source changed");
    require(fake.write_calls == 0 && fake.commit_calls == 0, "write or commit called");
    require(captured_out.str().empty() && captured_err.str().empty(), "unexpected output");
    if (expected != SwitchInspectionStatus::inspected)
        require(!result.inspection.supported_size, "failure returned inspection data");
    return result;
}

const std::vector<std::string> read_events = {
    "mount-read-only", "open-read-only", "read", "close", "unmount"
};

void check_target(const FakeOperations& fake, std::uint64_t id, const char* filename) {
    require(fake.mounted_id == id, "wrong mounted title ID");
    require(fake.mounted_profile.bytes == selected_profile().bytes, "wrong profile");
    require(fake.opened_filename == filename, "wrong save filename");
    require(fake.requested_capacity == frlg_image_size + 1, "read is not bounded to size plus one");
}

void check_healthy(const SaveInspection& inspection) {
    require(inspection.supported_size, "expected supported image size");
    for (std::size_t slot = 0; slot < 2; ++slot) {
        const auto& entry = inspection.slots[slot];
        require(entry.international_sections == 14 && entry.japanese_sections == 14,
                "expected fourteen matching checksums");
        require(entry.has_save_index && entry.save_index == 17 + slot && !entry.mixed_save_indices,
                "unexpected synthetic save index");
        require(!entry.invalid_signature && !entry.duplicate_section_id &&
                !entry.section_id_out_of_range, "unexpected structural warning");
    }
}
} // namespace

int main() {
    using Test = std::pair<const char*, std::function<void()>>;
    const std::vector<Test> tests = {
        {"source-documented mappings", [] {
            const auto* fire = switch_save_target(SwitchTitle::fire_red_spanish);
            const auto* leaf = switch_save_target(SwitchTitle::leaf_green_spanish);
            require(fire && fire->application_id == 0x0100EB702342C000ULL &&
                    std::string(fire->filename) == "FireRed_s.sav", "wrong FireRed mapping");
            require(leaf && leaf->application_id == 0x01002B5023434000ULL &&
                    std::string(leaf->filename) == "LeafGreen_s.sav", "wrong LeafGreen mapping");
            require(!switch_save_target(static_cast<SwitchTitle>(99)), "unknown title accepted");
        }},
        {"FireRed explicit selection and inspection", [] {
            FakeOperations fake;
            const auto result = exercise(fake, selected_profile(), SwitchTitle::fire_red_spanish,
                                         SwitchInspectionStatus::inspected, read_events);
            check_target(fake, 0x0100EB702342C000ULL, "FireRed_s.sav");
            check_healthy(result.inspection);
        }},
        {"LeafGreen explicit selection and inspection", [] {
            FakeOperations fake;
            const auto result = exercise(fake, selected_profile(), SwitchTitle::leaf_green_spanish,
                                         SwitchInspectionStatus::inspected, read_events);
            check_target(fake, 0x01002B5023434000ULL, "LeafGreen_s.sav");
            check_healthy(result.inspection);
        }},
        {"actual inspector observes changed synthetic checksum", [] {
            FakeOperations fake;
            fake.source[0] = 1;
            const auto result = exercise(fake, selected_profile(), SwitchTitle::fire_red_spanish,
                                         SwitchInspectionStatus::inspected, read_events);
            require(result.inspection.slots[0].international_sections == 13 &&
                    result.inspection.slots[0].japanese_sections == 13 &&
                    result.inspection.slots[1].international_sections == 14 &&
                    result.inspection.slots[1].japanese_sections == 14,
                    "inspection did not reflect checksum mismatch");
        }},
        {"mount failure is not title absence", [] {
            FakeOperations fake;
            fake.fail_mount = true;
            exercise(fake, selected_profile(), SwitchTitle::fire_red_spanish,
                     SwitchInspectionStatus::mount_failed, {"mount-read-only"});
        }},
        {"open failure unmounts", [] {
            FakeOperations fake;
            fake.fail_open = true;
            exercise(fake, selected_profile(), SwitchTitle::fire_red_spanish,
                     SwitchInspectionStatus::open_failed,
                     {"mount-read-only", "open-read-only", "unmount"});
        }},
        {"read failure closes before unmount", [] {
            FakeOperations fake;
            fake.fail_read = true;
            exercise(fake, selected_profile(), SwitchTitle::fire_red_spanish,
                     SwitchInspectionStatus::read_failed, read_events);
        }},
        {"short image rejected and closed", [] {
            FakeOperations fake;
            fake.source.pop_back();
            exercise(fake, selected_profile(), SwitchTitle::fire_red_spanish,
                     SwitchInspectionStatus::invalid_size, read_events);
            require(fake.requested_capacity == frlg_image_size + 1, "incorrect read bound");
        }},
        {"oversized image rejected and closed", [] {
            FakeOperations fake;
            fake.source.push_back(0);
            exercise(fake, selected_profile(), SwitchTitle::fire_red_spanish,
                     SwitchInspectionStatus::invalid_size, read_events);
            require(fake.requested_capacity == frlg_image_size + 1, "incorrect read bound");
        }},
        {"cancel without profile", [] {
            FakeOperations fake;
            exercise(fake, std::nullopt, SwitchTitle::fire_red_spanish,
                     SwitchInspectionStatus::cancelled, {});
        }},
        {"cancel without title", [] {
            FakeOperations fake;
            exercise(fake, selected_profile(), std::nullopt, SwitchInspectionStatus::cancelled, {});
        }},
        {"cancel without either selection", [] {
            FakeOperations fake;
            exercise(fake, std::nullopt, std::nullopt, SwitchInspectionStatus::cancelled, {});
        }},
        {"invalid empty profile rejected", [] {
            FakeOperations fake;
            exercise(fake, SwitchProfileId{}, SwitchTitle::fire_red_spanish,
                     SwitchInspectionStatus::invalid_selection, {});
        }},
        {"unsupported title rejected", [] {
            FakeOperations fake;
            exercise(fake, selected_profile(), static_cast<SwitchTitle>(99),
                     SwitchInspectionStatus::invalid_selection, {});
        }},
        {"distinct profiles and titles remain paired", [] {
            auto one = selected_profile();
            auto two = one;
            one.bytes[7] = 0xA1;
            two.bytes[7] = 0xB2;
            FakeOperations fire, leaf;
            const auto a = exercise(fire, one, SwitchTitle::fire_red_spanish,
                                    SwitchInspectionStatus::inspected, read_events);
            const auto b = exercise(leaf, two, SwitchTitle::leaf_green_spanish,
                                    SwitchInspectionStatus::inspected, read_events);
            require(fire.mounted_profile.bytes == one.bytes && leaf.mounted_profile.bytes == two.bytes,
                    "profile bytes changed or crossed");
            require(fire.mounted_id == 0x0100EB702342C000ULL &&
                    fire.opened_filename == "FireRed_s.sav" &&
                    leaf.mounted_id == 0x01002B5023434000ULL &&
                    leaf.opened_filename == "LeafGreen_s.sav", "title pairing changed");
            check_healthy(a.inspection);
            check_healthy(b.inspection);
        }},
        {"empty and substantially oversized images rejected", [] {
            for (const auto n : {std::size_t{0}, frlg_image_size + 17}) {
                FakeOperations fake;
                fake.source.resize(n);
                exercise(fake, selected_profile(), SwitchTitle::leaf_green_spanish,
                         SwitchInspectionStatus::invalid_size, read_events);
                require(fake.requested_capacity == frlg_image_size + 1, "wrong read bound");
            }
        }},
        {"read error precedes size rejection and cleans up", [] {
            FakeOperations fake;
            fake.source.resize(frlg_image_size + 17);
            fake.fail_read = true;
            exercise(fake, selected_profile(), SwitchTitle::leaf_green_spanish,
                     SwitchInspectionStatus::read_failed, read_events);
            require(fake.requested_capacity == frlg_image_size + 1, "wrong read bound");
        }},
        {"malformed image warnings reach selected-title result", [] {
            FakeOperations fake;
            put16(fake.source, 0xFF4, 1);
            put16(fake.source, 2 * 0x1000 + 0xFF4, 99);
            put32(fake.source, 3 * 0x1000 + 0xFF8, 0);
            const auto result = exercise(fake, selected_profile(), SwitchTitle::leaf_green_spanish,
                                         SwitchInspectionStatus::inspected, read_events);
            const auto& bad = result.inspection.slots[0];
            const auto& good = result.inspection.slots[1];
            require(bad.invalid_signature && bad.section_id_out_of_range && bad.duplicate_section_id,
                    "malformed section warnings missing");
            require(bad.international_sections == 11 && bad.japanese_sections == 11 &&
                    good.international_sections == 14 && good.japanese_sections == 14,
                    "malformed section counts wrong");
        }},
        {"negative unknown title never mounts", [] {
            FakeOperations fake;
            exercise(fake, selected_profile(), static_cast<SwitchTitle>(-1),
                     SwitchInspectionStatus::invalid_selection, {});
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
