// SPDX-FileCopyrightText: 2018 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <discord_rpc.h>

#include <chrono>
#include <string>
#include <thread>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <fmt/format.h>

#include "common/common_types.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/loader/loader.h"
#include "citron/discord_impl.h"
#include "citron/uisettings.h"

static void OnDiscordReady(const DiscordUser* request) {
    fmt::print("\n[DISCORD CALLBACK] SUCCESS: Connected to Discord as {}#{}\n\n", request->username, request->discriminator);
}
static void OnDiscordDisconnected(int errcode, const char* message) {
    fmt::print("\n[DISCORD CALLBACK] ERROR: Disconnected from Discord. Code: {}, Message: {}\n\n", errcode, message);
}
static void OnDiscordError(int errcode, const char* message) {
    fmt::print("\n[DISCORD CALLBACK] ERROR: An error occurred. Code: {}, Message: {}\n\n", errcode, message);
}

namespace DiscordRPC {

    DiscordImpl::DiscordImpl(Core::System& system_) : system{system_} {
        DiscordEventHandlers handlers{};
        handlers.ready = OnDiscordReady;
        handlers.disconnected = OnDiscordDisconnected;
        handlers.errored = OnDiscordError;

        Discord_Initialize("1361252452329848892", &handlers, 1, nullptr);

        // Initialize the timer for the first state (being in the menu).
        current_state_start_time = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
            was_powered_on = false; // Start in the "off" state.

            discord_thread_running = true;
            discord_thread = std::thread(&DiscordImpl::ThreadRun, this);
    }

    DiscordImpl::~DiscordImpl() {
        if (discord_thread_running) {
            discord_thread_running = false;
            if (discord_thread.joinable()) {
                discord_thread.join();
            }
        }
        Discord_ClearPresence();
        Discord_Shutdown();
    }

    void DiscordImpl::Pause() {
        Discord_ClearPresence();
    }

    void DiscordImpl::UpdateGameStatus(bool use_default) {
        const std::string default_text = "Citron Is A Homebrew Emulator For The Nintendo Switch";
        const std::string default_image = "citron_logo";
        DiscordRichPresence presence{};

        if (!use_default && !game_title_id.empty()) {
            // Discord external image format
            // Note: Discord may require app permissions for external URLs
            game_url = fmt::format("https://tinfoil.media/ti/{}/256/256", game_title_id);
            cached_url = game_url;
            presence.largeImageKey = cached_url.c_str();
        } else {
            presence.largeImageKey = default_image.c_str();
        }

        presence.largeImageText = game_title.c_str();
        presence.smallImageKey = default_image.c_str();
        presence.smallImageText = default_text.c_str();
        presence.details = game_title.c_str();
        presence.state = "Currently in game";
        presence.startTimestamp = current_state_start_time;
        Discord_UpdatePresence(&presence);
    }

    void DiscordImpl::Update() {
        const bool is_powered_on = system.IsPoweredOn();

        if (is_powered_on != was_powered_on) {
            // State changed! Reset the timer
            current_state_start_time = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
                // Update the state tracker.
                was_powered_on = is_powered_on;
        }

        if (is_powered_on) {
            // Game is running.
            system.GetAppLoader().ReadTitle(game_title);
            system.GetAppLoader().ReadProgramId(program_id);
            game_title_id = fmt::format("{:016X}", program_id);

            // Always try to use the Tinfoil image - Discord will handle fallback
            // Network check removed as it was causing unnecessary delays and false negatives
            UpdateGameStatus(false);
        } else {
            // Game is NOT running (in menus).
            const std::string default_text = "Citron Is A Homebrew Emulator For The Nintendo Switch";
            const std::string default_image = "citron_logo";
            DiscordRichPresence presence{};
            presence.largeImageKey = default_image.c_str();
            presence.largeImageText = default_text.c_str();
            presence.details = "In the Menus";
            presence.startTimestamp = current_state_start_time; // Use the state-based timer
            Discord_UpdatePresence(&presence);
        }
    }

    void DiscordImpl::ThreadRun() {
        while (discord_thread_running) {
            Update();
            Discord_RunCallbacks();
            std::this_thread::sleep_for(std::chrono::seconds(15));
        }
    }

} // namespace DiscordRPC
