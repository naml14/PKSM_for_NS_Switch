#include "pksm/frlg_console_write.hpp"
#include "pksm/frlg_content.hpp"
#include "pksm/frlg_money_editor.hpp"
#include "pksm/frlg_pokemon.hpp"
#include "pksm/frlg_text.hpp"
#include "pksm/frlg_write.hpp"
#include "pksm/switch_readonly.hpp"

#include <switch.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr std::size_t max_profiles = 8;
constexpr const char* mount_name = "frlg";

struct Profile {
    pksm::SwitchProfileId id;
    std::array<char, 33> label{};
};

// What the profile listing reported, so a failure can be explained on screen
// instead of guessed at.
struct ProfileListing {
    bool ok = false;
    bool fallback = false;  // used the profile the console opened last
    Result init_result = 0;
    Result list_result = 0;
    s32 total = 0;
};

// The profile nickname is used only for the on-screen choice. Account
// identifiers are never printed. The save content is shown on the result
// screen, which only ever reads it.
bool list_profiles(std::array<Profile, max_profiles>& profiles, std::size_t& count,
                   bool& skipped, ProfileListing& listing) {
    std::array<AccountUid, max_profiles> ids{};
    s32 total = 0;
    listing.list_result = accountListAllUsers(ids.data(), static_cast<s32>(ids.size()), &total);
    listing.total = total;
    if (R_FAILED(listing.list_result) || total < 0 || total > static_cast<s32>(ids.size())) {
        // Listing the profiles can fail on its own. The profile the console
        // opened last is still a real profile, and using it stays read-only.
        AccountUid last{};
        if (R_SUCCEEDED(accountGetLastOpenedUser(&last))) {
            AccountProfile account{};
            if (R_SUCCEEDED(accountGetProfile(&account, last))) {
                AccountProfileBase base{};
                const Result result = accountProfileGet(&account, nullptr, &base);
                accountProfileClose(&account);
                if (R_SUCCEEDED(result)) {
                    Profile entry{};
                    static_assert(sizeof(AccountUid) == sizeof(entry.id.bytes), "Unexpected account ID size");
                    std::memcpy(entry.id.bytes.data(), &last, sizeof(AccountUid));
                    std::size_t length = 0;
                    bool usable = true;
                    while (length < sizeof(base.nickname) && length + 1 < entry.label.size() &&
                           base.nickname[length] != '\0') {
                        const auto ch = static_cast<unsigned char>(base.nickname[length]);
                        if (ch < 0x20 || ch == 0x7f) {
                            usable = false;
                            break;
                        }
                        entry.label[length] = static_cast<char>(ch);
                        ++length;
                    }
                    if (!usable || length == 0)
                        std::snprintf(entry.label.data(), entry.label.size(), "last opened profile");
                    profiles[0] = entry;
                    count = 1;
                    skipped = false;
                    listing.ok = true;
                    listing.fallback = true;
                    return true;
                }
            }
        }
        return false;
    }

    count = 0;
    skipped = false;
    for (s32 i = 0; i < total; ++i) {
        AccountProfile account{};
        if (R_FAILED(accountGetProfile(&account, ids[static_cast<std::size_t>(i)]))) {
            skipped = true;
            continue;
        }
        AccountProfileBase base{};
        const Result result = accountProfileGet(&account, nullptr, &base);
        accountProfileClose(&account);
        if (R_FAILED(result)) {
            skipped = true;
            continue;
        }
        Profile entry{};
        static_assert(sizeof(AccountUid) == sizeof(entry.id.bytes), "Unexpected account ID size");
        std::memcpy(entry.id.bytes.data(), &ids[static_cast<std::size_t>(i)], sizeof(AccountUid));
        std::size_t length = 0;
        bool usable = true;
        bool non_space = false;
        while (length < sizeof(base.nickname) && length + 1 < entry.label.size() &&
               base.nickname[length] != '\0') {
            const auto ch = static_cast<unsigned char>(base.nickname[length]);
            // The nickname is UTF-8 text, so accented letters and emoji are
            // part of it and must not make the profile unusable.
            if (ch < 0x20 || ch == 0x7f) {
                usable = false;
                break;
            }
            entry.label[length] = static_cast<char>(ch);
            non_space = non_space || ch != ' ';
            ++length;
        }
        if (!usable || !non_space || entry.label[0] == ' ' ||
            entry.label[length - 1] == ' ') {
            skipped = true;
            continue;
        }
        profiles[count++] = entry;
    }
    // Two profiles may show the same name. Keep both and number them, so the
    // user can still choose one instead of losing both.
    std::array<bool, max_profiles> ambiguous{};
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1; j < count; ++j) {
            if (profiles[i].label == profiles[j].label) {
                ambiguous[i] = ambiguous[j] = true;
                skipped = true;
            }
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!ambiguous[i]) continue;
        char suffix[8];
        std::snprintf(suffix, sizeof(suffix), " (%u)", static_cast<unsigned>(i + 1));
        const std::size_t used = std::strlen(profiles[i].label.data());
        std::snprintf(profiles[i].label.data() + used, profiles[i].label.size() - used, "%s", suffix);
    }
    listing.ok = true;
    return true;
}

class NativeReadOnlyOperations final : public pksm::SwitchReadOnlyOperations {
public:
    bool mount_read_only(std::uint64_t application_id,
                         const pksm::SwitchProfileId& profile) noexcept override {
        AccountUid uid{};
        static_assert(sizeof(uid) == sizeof(profile.bytes), "Unexpected account ID size");
        std::memcpy(&uid, profile.bytes.data(), sizeof(uid));
        return R_SUCCEEDED(fsdevMountSaveDataReadOnly(mount_name, application_id, uid));
    }

    bool open_read_only(const char* filename) noexcept override {
        char path[64]{};
        const int length = std::snprintf(path, sizeof(path), "%s:/%s", mount_name, filename);
        if (length < 0 || static_cast<std::size_t>(length) >= sizeof(path)) return false;
        file_ = std::fopen(path, "rb");
        return file_ != nullptr;
    }

    bool read_to_end(std::uint8_t* output, std::size_t capacity,
                     std::size_t& bytes_read) noexcept override {
        if (file_ == nullptr) {
            bytes_read = 0;
            return false;
        }
        bytes_read = std::fread(output, 1, capacity, file_);
        return std::ferror(file_) == 0;
    }

    void close_file() noexcept override {
        if (file_) {
            if (std::fclose(file_) != 0) cleanup_failed_ = true;
            file_ = nullptr;
        }
    }

    void unmount() noexcept override {
        if (fsdevUnmountDevice(mount_name) != 0) cleanup_failed_ = true;
    }

    bool cleanup_failed() const noexcept { return cleanup_failed_; }

private:
    std::FILE* file_ = nullptr;
    bool cleanup_failed_ = false;
};

// Mounts the save data so that it can be written, which is only requested once
// the user asked to edit. It never asks for write access while it only reads.
bool mount_for_writing(std::uint64_t application_id,
                       const pksm::SwitchProfileId& profile) noexcept {
    AccountUid uid{};
    static_assert(sizeof(uid) == sizeof(profile.bytes), "Unexpected account ID size");
    std::memcpy(&uid, profile.bytes.data(), sizeof(uid));
    return R_SUCCEEDED(fsdevMountSaveData(mount_name, application_id, uid));
}

// The result of releasing the save device.
struct ReleaseResult {
    bool committed = false;
    bool released = false;
};

// Commits the changes and then releases the device, so that the file on disk
// holds what was written. Both steps report whether they worked.
ReleaseResult release_save() noexcept {
    ReleaseResult result;
    result.committed = R_SUCCEEDED(fsdevCommitDevice(mount_name));
    result.released = fsdevUnmountDevice(mount_name) == 0;
    return result;
}

// Shows decoded content without modifying the save.
void show_content(const std::vector<std::uint8_t>& image, std::size_t block) {
    if (image.size() != pksm::frlg_image_size) {
        std::printf("  Content: unavailable (the save image has an unexpected size)\n");
        return;
    }
    const auto content = pksm::read_frlg_content(image, block);
    if (!content.available) {
        std::printf("  Content: unavailable (%s)\n", content.unavailable_reason.c_str());
        return;
    }
    std::printf("  Content: available\n");
    const auto trainer = pksm::decode_frlg_text_terminated(content.player_name.data(), content.player_name.size(), pksm::FrlgTextLanguage::international);
    if (!trainer.empty()) std::printf("    Trainer: %s\n", trainer.c_str());
    std::printf("    Play time: %uh %um %us\n", static_cast<unsigned>(content.play_hours),
                static_cast<unsigned>(content.play_minutes),
                static_cast<unsigned>(content.play_seconds));
    std::printf("    Money: %u\n", static_cast<unsigned>(content.money));
    std::printf("    Party: %u\n", static_cast<unsigned>(content.party_count));
    std::printf("    Pokedex: %u owned, %u seen\n", static_cast<unsigned>(content.dex_owned),
                static_cast<unsigned>(content.dex_seen));
    std::printf("    Boxes occupied:");
    for (const auto occupied : content.box_occupied)
        std::printf(" %u", static_cast<unsigned>(occupied));
    std::printf("\n");
    std::size_t occupied = 0;
    for (std::size_t box = 0; box < 14; ++box) for (std::size_t slot = 0; slot < 30; ++slot) {
        const auto mon = pksm::read_frlg_box_mon(content, box, slot);
        if (mon.status != pksm::PokemonReadStatus::ok) continue;
        if (occupied++ >= 24) continue;
        const auto name = pksm::decode_frlg_name(mon.nickname.data(), mon.nickname.size(), mon.language);
        std::printf("    Box %u slot %u: species %u, nickname %s%s\n",
                    static_cast<unsigned>(box + 1), static_cast<unsigned>(slot + 1),
                    static_cast<unsigned>(mon.species), name.text.c_str(), name.fallback ? " [jp]" : "");
    }
    if (occupied > 24) std::printf("    %u occupied slots left out\n", static_cast<unsigned>(occupied - 24));
    const auto party = pksm::read_frlg_party(content);
    for (std::size_t i = 0; i < party.size(); ++i) {
        if (party[i].status != pksm::PokemonReadStatus::ok) continue;
        std::printf("    Party %u: species %u, level %u, hp %u/%u\n",
                    static_cast<unsigned>(i + 1), static_cast<unsigned>(party[i].species),
                    static_cast<unsigned>(party[i].level),
                    static_cast<unsigned>(party[i].current_hp),
                    static_cast<unsigned>(party[i].max_hp));
    }
}

void show_result(const pksm::SwitchInspectionResult& result) {
    using pksm::SwitchInspectionStatus;
    switch (result.status) {
        case SwitchInspectionStatus::cancelled:
            std::printf("Selection cancelled; no save was accessed.\n");
            return;
        case SwitchInspectionStatus::invalid_selection:
            std::printf("The profile or title selection is invalid.\n");
            return;
        case SwitchInspectionStatus::mount_failed:
            std::printf("Read-only mount failed. Permissions or another error may be responsible.\n"
                        "This does not establish whether the title or save exists.\n");
            return;
        case SwitchInspectionStatus::open_failed:
            std::printf("The mapped save file could not be opened for reading.\n");
            return;
        case SwitchInspectionStatus::read_failed:
            std::printf("The save file could not be read.\n");
            return;
        case SwitchInspectionStatus::invalid_size:
            std::printf("The file is not exactly 131072 bytes.\n");
            return;
        case SwitchInspectionStatus::inspected:
            break;
    }
    for (std::size_t i = 0; i < result.inspection.slots.size(); ++i) {
        const auto& slot = result.inspection.slots[i];
        std::printf("Slot %u: international %u/14; Japanese %u/14\n",
                    static_cast<unsigned>(i), static_cast<unsigned>(slot.international_sections),
                    static_cast<unsigned>(slot.japanese_sections));
        std::printf("  Signature, ID or duplicate warning: %s\n",
                    slot.invalid_signature || slot.section_id_out_of_range ||
                    slot.duplicate_section_id ? "yes" : "no");
        show_content(result.image, i);
    }
    std::printf("Content is read-only; game identity, language and revision are not verified.\n");
}
} // namespace

int main() {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    ProfileListing listing{};
    Result init_result = accountInitialize(AccountServiceType_Application);
    bool account_ready = R_SUCCEEDED(init_result);
    if (!account_ready) {
        // Some environments only expose the system account service.
        const Result system_result = accountInitialize(AccountServiceType_System);
        if (R_SUCCEEDED(system_result)) {
            init_result = system_result;
            account_ready = true;
        }
    }
    listing.init_result = init_result;
    std::array<Profile, max_profiles> profiles{};
    std::size_t profile_count = 0;
    bool skipped = false;
    const bool listed = R_SUCCEEDED(init_result) &&
                         list_profiles(profiles, profile_count, skipped, listing);

    enum class Screen { profile, title, result, edit, confirm, write_result };
    Screen screen = Screen::profile;
    std::size_t profile_index = 0;
    std::size_t title_index = 0;
    const std::array<pksm::SwitchTitle, 2> titles = {
        pksm::SwitchTitle::fire_red_spanish, pksm::SwitchTitle::leaf_green_spanish
    };
    const std::array<const char*, 2> title_labels = {"FireRed (Spanish)", "LeafGreen (Spanish)"};
    pksm::SwitchInspectionResult result{};
    bool cleanup_failed = false;
    bool write_mounted = false;
    ReleaseResult release;
    std::string save_path;
    std::vector<std::uint8_t> save_image;
    pksm::MoneyEditor editor{};
    pksm::MoneyWritePlan plan;
    pksm::ConsoleWriteResult write_result;
    std::string edit_message;

    while (appletMainLoop()) {
        consoleClear();
        std::printf("FRLG save tool (experimental)\n\n");
        if (!edit_message.empty()) std::printf("%s\n\n", edit_message.c_str());
        if (!listed) {
            std::printf("Could not list existing profiles. No save was accessed.\n");
            std::printf("  account service result: 0x%08x\n",
                        static_cast<unsigned>(listing.init_result));
            std::printf("  listing result: 0x%08x, profiles reported: %d\n",
                        static_cast<unsigned>(listing.list_result), static_cast<int>(listing.total));
        } else if (profile_count == 0) {
            std::printf("No selectable existing profile was available. No save was accessed.\n");
            std::printf("  profiles reported by the console: %d\n",
                        static_cast<int>(listing.total));
            if (skipped) std::printf("Unreadable names were omitted and repeated names were numbered.\n");
        } else if (screen == Screen::profile) {
            if (listing.fallback)
                std::printf("Listing the profiles failed; using the last opened profile.\n");
            std::printf("Choose an existing profile: Up/Down, A select, B cancel\n");
            for (std::size_t i = 0; i < profile_count; ++i)
                std::printf("%s %u: %s\n", i == profile_index ? ">" : " ",
                            static_cast<unsigned>(i + 1), profiles[i].label.data());
            if (skipped) std::printf("Unreadable names were omitted and repeated names were numbered.\n");
        } else if (screen == Screen::title) {
            std::printf("Choose a supported title: Up/Down, A inspect, B back\n");
            for (std::size_t i = 0; i < titles.size(); ++i)
                std::printf("%s %s\n", i == title_index ? ">" : " ", title_labels[i]);
            std::printf("No save is accessed until you select a title.\n");
        } else if (screen == Screen::result) {
            show_result(result);
            if (cleanup_failed) std::printf("Cleanup reported an error; exit this application.\n");
            else std::printf("A: edit the money on the console, B: choose another profile\n");
        } else if (screen == Screen::edit) {
            const auto digits = pksm::format_money(editor.value);
            std::printf("Money: ");
            for (std::size_t i = 0; i < digits.size(); ++i) {
                if (i == editor.digit) std::printf("[%c]", digits[i]);
                else std::printf("%c", digits[i]);
            }
            std::printf("\n");
            std::printf("Left/Right: choose the digit, Up/Down: change it\n");
            std::printf("A: continue, B: cancel\n");
        } else if (screen == Screen::confirm) {
            std::printf("The money will go from %s to %s.\n",
                        pksm::format_money(plan.old_money).c_str(),
                        pksm::format_money(plan.new_money).c_str());
            std::printf("%u bytes will change on the console save.\n",
                        static_cast<unsigned>(plan.changed_bytes));
            std::printf("Close the game before you use the save: the game keeps its own copy\n");
            std::printf("of the values in memory and writes them back when it saves.\n");
            std::printf("A: write to the console, B: cancel\n");
        } else {
            if (write_result.ok) {
                std::printf("Written and verified: %u -> %u, %u bytes changed.\n",
                            static_cast<unsigned>(write_result.old_money),
                            static_cast<unsigned>(write_result.new_money),
                            static_cast<unsigned>(write_result.changed_bytes));
            } else {
                std::printf("The write failed: %s\n", write_result.reason.c_str());
                if (write_result.rolled_back)
                    std::printf("The original bytes were restored.\n");
            }
            if (!release.released)
                std::printf("The save device could not be released cleanly. Close the game and\n");
            else if (!release.committed)
                std::printf("The device did not confirm the change. Close the game and check it.\n");
            std::printf("Close the game before you use the save: the game keeps its own copy\n");
            std::printf("of the values in memory and writes them back when it saves.\n");
            std::printf("B: back\n");
        }
        std::printf("\nPlus: exit\n");
        consoleUpdate(nullptr);

        padUpdate(&pad);
        const u64 pressed = padGetButtonsDown(&pad);
        if (pressed) edit_message.clear();
        if (pressed & HidNpadButton_Plus) break;
        if (!listed || profile_count == 0 || cleanup_failed) continue;
        if (screen == Screen::profile) {
            if (pressed & HidNpadButton_B) break;
            if (pressed & HidNpadButton_Up) profile_index = (profile_index + profile_count - 1) % profile_count;
            if (pressed & HidNpadButton_Down) profile_index = (profile_index + 1) % profile_count;
            if (pressed & HidNpadButton_A) screen = Screen::title;
        } else if (screen == Screen::title) {
            if (pressed & HidNpadButton_B) {
                screen = Screen::profile;
            } else {
                if (pressed & HidNpadButton_Up) title_index = (title_index + titles.size() - 1) % titles.size();
                if (pressed & HidNpadButton_Down) title_index = (title_index + 1) % titles.size();
                if (pressed & HidNpadButton_A) {
                    NativeReadOnlyOperations operations;
                    result = pksm::inspect_selected_switch_save(operations, profiles[profile_index].id,
                                                                titles[title_index]);
                    cleanup_failed = operations.cleanup_failed();
                    screen = Screen::result;
                }
            }
        } else if (screen == Screen::result) {
            if (pressed & HidNpadButton_A) {
                const auto* target = pksm::switch_save_target(titles[title_index]);
                if (!mount_for_writing(target->application_id, profiles[profile_index].id)) {
                    // The platform refused the write mount. The app says so and
                    // stops; it never retries with different privileges.
                    edit_message = "The platform refused the write mount. No write was attempted.";
                    screen = Screen::profile;
                } else {
                    write_mounted = true;
                    save_path = std::string(mount_name) + ":/" + target->filename;
                    if (!pksm::read_frlg_save_file(save_path, save_image)) {
                        release = release_save();
                        write_mounted = false;
                        edit_message = "The save file could not be read for writing.";
                        screen = Screen::profile;
                    } else {
                        const auto chosen = pksm::select_frlg_block(save_image);
                        if (!chosen.ok) {
                            release = release_save();
                            write_mounted = false;
                            edit_message = "The save cannot be edited: " + chosen.reason;
                            screen = Screen::profile;
                        } else {
                            const auto content = pksm::read_frlg_content(save_image, chosen.block);
                            // The editor never carries an amount the game
                            // would not accept.
                            editor.value =
                                content.available && content.money <= pksm::money_maximum
                                    ? content.money
                                    : 0;
                            editor.digit = 0;
                            screen = Screen::edit;
                        }
                    }
                }
            } else if (pressed & HidNpadButton_B) {
                screen = Screen::profile;
            }
        } else if (screen == Screen::edit) {
            if (pressed & HidNpadButton_B) {
                release = release_save();
                write_mounted = false;
                screen = Screen::result;
            } else {
                if (pressed & HidNpadButton_Left)
                    pksm::apply_money_editor_action(editor, pksm::MoneyEditorAction::left);
                if (pressed & HidNpadButton_Right)
                    pksm::apply_money_editor_action(editor, pksm::MoneyEditorAction::right);
                if (pressed & HidNpadButton_Up)
                    pksm::apply_money_editor_action(editor, pksm::MoneyEditorAction::up);
                if (pressed & HidNpadButton_Down)
                    pksm::apply_money_editor_action(editor, pksm::MoneyEditorAction::down);
                if (pressed & HidNpadButton_A) {
                    plan = pksm::plan_frlg_money_write(save_image, editor.value);
                    if (plan.ok) screen = Screen::confirm;
                }
            }
        } else if (screen == Screen::confirm) {
            if (pressed & HidNpadButton_B) {
                screen = Screen::edit;
            } else if (pressed & HidNpadButton_A) {
                // The file is read again before the write, so that the bytes
                // that go to the console are exactly the ones that were
                // confirmed. If the save changed in the meantime, nothing is
                // written.
                std::vector<std::uint8_t> current;
                if (!pksm::read_frlg_save_file(save_path, current) || current != save_image) {
                    edit_message = "The save changed while you were editing. Nothing was written.";
                    screen = Screen::edit;
                } else {
                    write_result = pksm::apply_frlg_money_write(save_path, plan);
                    release = release_save();
                    write_mounted = false;
                    screen = Screen::write_result;
                }
            }
        } else if (pressed & HidNpadButton_B) {
            screen = Screen::result;
        }
    }
    if (write_mounted) release_save();
    if (account_ready) accountExit();
    consoleExit(nullptr);
    return 0;
}
