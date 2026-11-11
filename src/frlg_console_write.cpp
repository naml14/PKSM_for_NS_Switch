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

bool read_frlg_save_file(const std::string& path, std::vector<std::uint8_t>& image) noexcept {
    std::vector<std::uint8_t> bytes;
    if (!read_file(path, bytes)) return false;
    if (bytes.size() != frlg_image_size) return false;
    image = std::move(bytes);
    return true;
}

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
    const bool complete = written == bytes.size() && std::fflush(file) == 0;
    std::fclose(file);
    return complete;
}

// Puts the original bytes back and says whether that worked.
void roll_back(const std::string& path, const MoneyWritePlan& plan,
               ConsoleWriteResult& result) {
    if (write_file(path, plan.original)) {
        std::vector<std::uint8_t> written;
        if (read_file(path, written) && written == plan.original) {
            result.rolled_back = true;
            return;
        }
    }
    result.reason = "the write failed and the original bytes could not be restored";
}

}  // namespace

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
        result.reason = "the file could not be written";
        return result;
    }
    std::vector<std::uint8_t> written;
    if (!read_file(path, written) || written.size() != frlg_image_size) {
        roll_back(path, plan, result);
        if (result.rolled_back) result.reason = "the file could not be read back";
        return result;
    }
    if (written != plan.bytes) {
        roll_back(path, plan, result);
        if (result.rolled_back) result.reason = "the file does not hold the prepared bytes";
        return result;
    }
    const auto content = read_frlg_content(written, plan.block);
    if (!content.available || content.money != plan.new_money) {
        roll_back(path, plan, result);
        if (result.rolled_back) result.reason = "the money does not read back";
        return result;
    }
    const auto inspection = inspect_frlg_save(written);
    if (inspection.slots[plan.block].international_sections != frlg_section_count) {
        roll_back(path, plan, result);
        if (result.rolled_back) result.reason = "the written block is not complete";
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
