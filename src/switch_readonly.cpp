#include "pksm/switch_readonly.hpp"

#include <algorithm>
#include <vector>

namespace pksm {
namespace {
struct MountGuard {
    SwitchReadOnlyOperations& operations;
    ~MountGuard() { operations.unmount(); }
};

struct FileGuard {
    SwitchReadOnlyOperations& operations;
    ~FileGuard() { operations.close_file(); }
};
} // namespace

const SwitchSaveTarget* switch_save_target(SwitchTitle title) noexcept {
    static constexpr SwitchSaveTarget fire_red = {0x0100EB702342C000ULL, "FireRed_s.sav"};
    static constexpr SwitchSaveTarget leaf_green = {0x01002B5023434000ULL, "LeafGreen_s.sav"};
    switch (title) {
        case SwitchTitle::fire_red_spanish: return &fire_red;
        case SwitchTitle::leaf_green_spanish: return &leaf_green;
    }
    return nullptr;
}

SwitchInspectionResult inspect_selected_switch_save(
    SwitchReadOnlyOperations& operations, const std::optional<SwitchProfileId>& profile,
    const std::optional<SwitchTitle>& title) {
    if (!profile || !title) return {SwitchInspectionStatus::cancelled, {}, {}};
    const auto* target = switch_save_target(*title);
    const bool valid_profile = std::any_of(profile->bytes.begin(), profile->bytes.end(),
                                           [](std::uint8_t byte) { return byte != 0; });
    if (!target || !valid_profile) return {SwitchInspectionStatus::invalid_selection, {}, {}};

    // Allocate before mounting. One extra byte rejects oversized files without loading them.
    std::vector<std::uint8_t> image(frlg_image_size + 1);
    std::size_t bytes_read = 0;
    {
        if (!operations.mount_read_only(target->application_id, *profile))
            return {SwitchInspectionStatus::mount_failed, {}, {}};
        MountGuard mounted{operations};
        if (!operations.open_read_only(target->filename))
            return {SwitchInspectionStatus::open_failed, {}, {}};
        FileGuard opened{operations};
        if (!operations.read_to_end(image.data(), image.size(), bytes_read))
            return {SwitchInspectionStatus::read_failed, {}, {}};
    } // Close before unmount, including every early-return path.

    if (bytes_read != frlg_image_size) return {SwitchInspectionStatus::invalid_size, {}, {}};
    image.resize(frlg_image_size);
    SwitchInspectionResult result;
    result.status = SwitchInspectionStatus::inspected;
    result.inspection = inspect_frlg_save(image);
    // The bytes are kept so a caller can decode the content. They are a
    // read-only copy and are never written back.
    result.image = std::move(image);
    return result;
}

} // namespace pksm
