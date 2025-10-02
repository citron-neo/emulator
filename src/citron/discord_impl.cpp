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
		s64 start_time = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch())
		.count();
		DiscordRichPresence presence{};

		if (!use_default && !game_title_id.empty()) {
			game_url = fmt::format("{}{}/256/256", "https://tinfoil.media/ti/", game_title_id);
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
		presence.startTimestamp = start_time;
		Discord_UpdatePresence(&presence);
	}

	void DiscordImpl::Update() {
		const std::string default_text = "Citron Is A Homebrew Emulator For The Nintendo Switch";
		const std::string default_image = "citron_logo";

		if (system.IsPoweredOn()) {
			system.GetAppLoader().ReadTitle(game_title);
			system.GetAppLoader().ReadProgramId(program_id);
			game_title_id = fmt::format("{:016X}", program_id);

			QNetworkAccessManager manager;
			QNetworkRequest request;
			request.setUrl(QUrl(QString::fromStdString(
				fmt::format("https://tinfoil.media/ti/{}/256/256", game_title_id))));
			request.setTransferTimeout(5000); // 5 second timeout
			QNetworkReply* reply = manager.head(request);
			QEventLoop request_event_loop;
			QObject::connect(reply, &QNetworkReply::finished, &request_event_loop, &QEventLoop::quit);
			request_event_loop.exec();

			UpdateGameStatus(reply->error() != QNetworkReply::NoError);
			reply->deleteLater();
		} else {
			s64 start_time = std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::system_clock::now().time_since_epoch())
			.count();
			DiscordRichPresence presence{};
			presence.largeImageKey = default_image.c_str();
			presence.largeImageText = default_text.c_str();
			presence.details = "Currently not in game";
			presence.startTimestamp = start_time;
			Discord_UpdatePresence(&presence);
		}
	}

	void DiscordImpl::ThreadRun() {
		while (discord_thread_running) {
			Update(); // This is the line that was likely missing.
			Discord_RunCallbacks();
			std::this_thread::sleep_for(std::chrono::seconds(15));
		}
	}

} // namespace DiscordRPC
