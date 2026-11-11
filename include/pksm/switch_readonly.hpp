#pragma once

#include "pksm/frlg_save.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace pksm {

// Opaque account identifier; never display or persist it.
struct SwitchProfileId {
    std::array<std::uint8_t, 16> bytes{};
};

enum class SwitchTitle { fire_red_spanish, leaf_green_spanish };

struct SwitchSaveTarget {
    std::uint64_t application_id;
    const char* filename;
};

// Only the source-documented Spanish targets are accepted; unknown values return null.
const SwitchSaveTarget* switch_save_target(SwitchTitle title) noexcept;

enum class SwitchInspectionStatus {
    cancelled,
    invalid_selection,
    mount_failed,
    open_failed,
    read_failed,
    invalid_size,
    inspected
};

struct SwitchInspectionResult {
    SwitchInspectionStatus status = SwitchInspectionStatus::cancelled;
    SaveInspection inspection{};
    // The bytes that were read, kept so a caller can decode the content.
    // They are a read-only copy: nothing ever writes them back.
    std::vector<std::uint8_t> image;
};

// An implementation must clean up any partial acquisition when mount or open fails.
// read_to_end reads at most capacity bytes and reports failure separately from EOF.
class SwitchReadOnlyOperations {
public:
    virtual ~SwitchReadOnlyOperations() = default;
    virtual bool mount_read_only(std::uint64_t application_id,
                                 const SwitchProfileId& profile) noexcept = 0;
    virtual bool open_read_only(const char* filename) noexcept = 0;
    virtual bool read_to_end(std::uint8_t* destination, std::size_t capacity,
                             std::size_t& bytes_read) noexcept = 0;
    virtual void close_file() noexcept = 0;
    virtual void unmount() noexcept = 0;
};

// Missing selections are cancellation, not permission to choose a default.
SwitchInspectionResult inspect_selected_switch_save(
    SwitchReadOnlyOperations& operations,
    const std::optional<SwitchProfileId>& profile,
    const std::optional<SwitchTitle>& title);

} // namespace pksm
