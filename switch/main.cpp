#include "pksm/frlg_content.hpp"
#include "pksm/frlg_pokemon.hpp"
#include "pksm/frlg_text.hpp"
#include "pksm/switch_readonly.hpp"

#include <switch.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

// Names are used only for the on-screen choice; never print IDs or save contents.
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

// Shows decoded content without modifying the save.
void show_content(const std::vector<std::uint8_t>& image, std::size_t block) {
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

    enum class Screen { profile, title, result };
    Screen screen = Screen::profile;
    std::size_t profile_index = 0;
    std::size_t title_index = 0;
    const std::array<pksm::SwitchTitle, 2> titles = {
        pksm::SwitchTitle::fire_red_spanish, pksm::SwitchTitle::leaf_green_spanish
    };
    const std::array<const char*, 2> title_labels = {"FireRed (Spanish)", "LeafGreen (Spanish)"};
    pksm::SwitchInspectionResult result{};
    bool cleanup_failed = false;

    while (appletMainLoop()) {
        consoleClear();
        std::printf("FRLG read-only inspection (experimental)\n\n");
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
        } else {
            show_result(result);
            if (cleanup_failed) std::printf("Cleanup reported an error; exit this application.\n");
            else std::printf("B: choose another profile\n");
        }
        std::printf("\nPlus: exit\n");
        consoleUpdate(nullptr);

        padUpdate(&pad);
        const u64 pressed = padGetButtonsDown(&pad);
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
        } else if (pressed & HidNpadButton_B) {
            screen = Screen::profile;
        }
    }
    if (account_ready) accountExit();
    consoleExit(nullptr);
    return 0;
}
