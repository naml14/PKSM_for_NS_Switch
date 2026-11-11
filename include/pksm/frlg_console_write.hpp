#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pksm {

// The bytes that are going to be written, prepared and checked before
// anything touches the save file. The original bytes travel with the plan so
// that a failed write can be undone.
struct MoneyWritePlan {
    bool ok = false;
    std::string reason;                  // why it failed, empty when it worked
    std::vector<std::uint8_t> original;  // the bytes as they were read
    std::vector<std::uint8_t> bytes;     // the bytes to write
    std::size_t block = 0;               // the block that is edited
    std::size_t money_offset = 0;        // where the money is in the image
    std::size_t checksum_offset = 0;     // where the checksum of its section is
    std::uint32_t old_money = 0;
    std::uint32_t new_money = 0;
    std::size_t changed_bytes = 0;       // how many bytes differ from the original
};

// Prepares the write for an image that is already in memory. Nothing is
// written and the original is never modified.
MoneyWritePlan plan_frlg_money_write(const std::vector<std::uint8_t>& original,
                                     std::uint32_t value);

// The result of a write to a save file.
struct ConsoleWriteResult {
    bool ok = false;
    std::string reason;              // why it failed, empty when it worked
    bool rolled_back = false;        // the file holds the original bytes again
    std::size_t changed_bytes = 0;   // how many bytes differ from the original
    std::uint32_t old_money = 0;
    std::uint32_t new_money = 0;
};

// Writes the plan to the file, reads it back and verifies it: the bytes have
// to be the planned ones, the money has to read back, every section checksum
// has to match and no byte outside the money and its checksum may differ from
// the original. When any of that fails, the original bytes are written back
// before the function returns.
ConsoleWriteResult apply_frlg_money_write(const std::string& path,
                                          const MoneyWritePlan& plan);

// Reads a save file into memory. It fails when the file cannot be read or when
// it is not exactly 131072 bytes long.
bool read_frlg_save_file(const std::string& path, std::vector<std::uint8_t>& image) noexcept;

// Writes the given bytes to the file and reads them back to confirm that the
// file holds them. It is the way to undo a write that failed halfway.
bool restore_frlg_save_file(const std::string& path,
                             const std::vector<std::uint8_t>& bytes) noexcept;

// Reads the file, plans the change and writes it.
ConsoleWriteResult commit_frlg_money(const std::string& path, std::uint32_t value);

}  // namespace pksm
