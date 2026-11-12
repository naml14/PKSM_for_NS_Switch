#include "pksm/frlg_console_write.hpp"

#include "pksm/frlg_content.hpp"
#include "pksm/frlg_save.hpp"
#include "pksm/frlg_write.hpp"

#include <cstdio>

namespace pksm {
namespace {

constexpr std::size_t sector_size = 0x1000;
constexpr std::size_t money_offset_in_section = 0x290;  // inside SaveBlock1
constexpr std::size_t checksum_offset_in_sector = 0xFF6;

// The save is read and written with the C library, so that the console
// application does not have to carry the iostream machinery.
bool read_file(const std::string& path, std::vector<std::uint8_t>& out) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    out.clear();
    std::uint8_t buffer[4096];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        out.insert(out.end(), buffer, buffer + read);
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    return !failed;
}

}  // namespace

namespace {

// The file keeps its size: the whole content is written from the beginning.
bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::FILE* file = std::fopen(path.c_str(), "r+b");
    if (file == nullptr) return false;
    if (std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return false;
    }
    const std::size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    bool complete = written == bytes.size() && std::fflush(file) == 0;
    if (complete) {
        // The size on disk is confirmed before the file is closed.
        if (std::fseek(file, 0, SEEK_END) != 0) {
            complete = false;
        } else if (static_cast<std::size_t>(std::ftell(file)) != bytes.size()) {
            complete = false;
        }
    }
    std::fclose(file);
    return complete;
}

// Puts the original bytes back and reports whether the file holds them. A
// write that failed halfway leaves the file with mixed bytes, so the original
// has to be written again before the app stops.
bool restore_original(const std::string& path, const MoneyWritePlan& plan) {
    std::vector<std::uint8_t> current;
    if (read_file(path, current) && current == plan.original) return true;
    return restore_frlg_save_file(path, plan.original);
}

}  // namespace

bool read_frlg_save_file(const std::string& path, std::vector<std::uint8_t>& image) noexcept {
    std::vector<std::uint8_t> bytes;
    if (!read_file(path, bytes)) return false;
    if (bytes.size() != frlg_image_size) return false;
    image = std::move(bytes);
    return true;
}

bool restore_frlg_save_file(const std::string& path,
                             const std::vector<std::uint8_t>& bytes) noexcept {
    if (!write_file(path, bytes)) return false;
    std::vector<std::uint8_t> written;
    return read_file(path, written) && written == bytes;
}

ImageWriteResult commit_frlg_image(const std::string& path,
                                    const std::vector<std::uint8_t>& original,
                                    const std::vector<std::uint8_t>& image) noexcept {
    ImageWriteResult result;
    if (original.size() != frlg_image_size || image.size() != frlg_image_size) {
        result.reason = "Unsupported image size";
        return result;
    }
    // The file has to still hold the bytes the change was prepared from. If
    // the game saved while the user was choosing, writing now would throw
    // those changes away, so nothing is written at all.
    std::vector<std::uint8_t> current;
    if (!read_frlg_save_file(path, current) || current != original) {
        result.reason = "the save changed while the edit was being prepared";
        return result;
    }
    const bool wrote = write_file(path, image);
    std::vector<std::uint8_t> written;
    if (wrote && read_frlg_save_file(path, written) && written == image) {
        result.ok = true;
        return result;
    }
    result.rolled_back = restore_frlg_save_file(path, original);
    result.reason = result.rolled_back ? "the image could not be written and verified"
                                       : "the image could not be written and verified and the original bytes could not be restored";
    return result;
}

MoneyWritePlan plan_frlg_money_write(const std::vector<std::uint8_t>& original,
                                     std::uint32_t value) {
    MoneyWritePlan plan;
    const auto edit = set_frlg_money(original, value);
    if (!edit.ok) {
        plan.reason = edit.reason;
        return plan;
    }
    const auto inspection = inspect_frlg_save(original);
    const auto& slot = inspection.slots[edit.block];
    const auto position = slot.sections[1].sector_position;
    const auto block_base = edit.block * frlg_section_count * sector_size;
    const auto sector_base = block_base + position * sector_size;
    plan.ok = true;
    plan.original = original;
    plan.bytes = edit.image;
    plan.block = edit.block;
    plan.money_offset = sector_base + money_offset_in_section;
    plan.checksum_offset = sector_base + checksum_offset_in_sector;
    plan.old_money = edit.old_money;
    plan.new_money = edit.new_money;
    plan.changed_bytes = edit.changed_bytes;
    return plan;
}

ConsoleWriteResult apply_frlg_money_write(const std::string& path,
                                          const MoneyWritePlan& plan) {
    ConsoleWriteResult result;
    result.old_money = plan.old_money;
    result.new_money = plan.new_money;
    result.changed_bytes = plan.changed_bytes;
    if (!plan.ok) {
        result.reason = plan.reason.empty() ? "the write was not prepared" : plan.reason;
        return result;
    }
    if (plan.original.size() != frlg_image_size || plan.bytes.size() != frlg_image_size) {
        result.reason = "Unsupported image size";
        return result;
    }
    // The file has to still hold the bytes the plan was prepared from. If it
    // changed in the meantime, what the user confirmed is not what would be
    // written, so nothing is written at all.
    std::vector<std::uint8_t> current;
    if (!read_file(path, current) || current != plan.original) {
        result.reason = "the file changed since the write was prepared";
        return result;
    }
    // Every byte that differs has to be inside the money or its checksum. The
    // permitted places are recomputed here, so a plan that was altered after
    // it was prepared cannot smuggle a change past this check.
    for (std::size_t i = 0; i < plan.bytes.size(); ++i) {
        if (plan.bytes[i] == plan.original[i]) continue;
        const bool in_money =
            i >= plan.money_offset && i < plan.money_offset + 4;
        const bool in_checksum =
            i >= plan.checksum_offset && i < plan.checksum_offset + 2;
        if (!in_money && !in_checksum) {
            result.reason =
                "the prepared write changes a byte outside the money and its checksum";
            return result;
        }
    }
    if (!write_file(path, plan.bytes)) {
        // The write may have failed halfway, leaving the file with mixed
        // bytes, so the original is put back before the app stops.
        result.rolled_back = restore_original(path, plan);
        result.reason = result.rolled_back ? "the file could not be written completely"
                                           : "the file could not be written and the original bytes could not be restored";
        return result;
    }
    std::vector<std::uint8_t> written;
    if (!read_file(path, written) || written.size() != frlg_image_size) {
        result.rolled_back = restore_original(path, plan);
        result.reason = result.rolled_back ? "the file could not be read back"
                                           : "the file could not be read back and the original bytes could not be restored";
        return result;
    }
    if (written != plan.bytes) {
        result.rolled_back = restore_original(path, plan);
        result.reason = result.rolled_back ? "the file does not hold the prepared bytes"
                                           : "the original bytes could not be restored";
        return result;
    }
    const auto content = read_frlg_content(written, plan.block);
    if (!content.available || content.money != plan.new_money) {
        result.rolled_back = restore_original(path, plan);
        result.reason = result.rolled_back ? "the money does not read back"
                                           : "the original bytes could not be restored";
        return result;
    }
    const auto inspection = inspect_frlg_save(written);
    const auto& slot = inspection.slots[plan.block];
    // A block that matches the Japanese range is complete as well: the game
    // accepts either range, so the verification accepts either one.
    if (slot.international_sections != frlg_section_count &&
        slot.japanese_sections != frlg_section_count) {
        result.rolled_back = restore_original(path, plan);
        result.reason = result.rolled_back ? "the written block is not complete"
                                           : "the original bytes could not be restored";
        return result;
    }
    result.ok = true;
    return result;
}

ConsoleWriteResult commit_frlg_money(const std::string& path, std::uint32_t value) {
    ConsoleWriteResult result;
    std::vector<std::uint8_t> original;
    if (!read_file(path, original)) {
        result.reason = "the file could not be read";
        return result;
    }
    const auto plan = plan_frlg_money_write(original, value);
    return apply_frlg_money_write(path, plan);
}

}  // namespace pksm
