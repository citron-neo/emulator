// SPDX-FileCopyrightText: 2018 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>

#include <string>

#include <QEventLoop>

#include <QNetworkAccessManager>

#include <QNetworkReply>

#include <discord_rpc.h>

#include <fmt/format.h>

#include "common/common_types.h"

#include "common/string_util.h"

#include "core/core.h"

#include "core/loader/loader.h"

#include "citron/discord_impl.h"

#include "citron/uisettings.h"

namespace DiscordRPC {

DiscordImpl::DiscordImpl(Core::System & system_): system {
system_
} {
DiscordEventHandlers handlers {};
// The number is the client ID for citron, it's used for images and the
// application name
Discord_Initialize("1361252452329848892", & handlers, 1, nullptr);
}

DiscordImpl::~DiscordImpl() {
Discord_ClearPresence();
Discord_Shutdown();
}

void DiscordImpl::Pause() {
Discord_ClearPresence();
}

void DiscordImpl::UpdateGameStatus(bool use_default) {
const std::string default_text = "Citron Is A Homebrew Emulator For The Nintendo Switch";
const std::string default_image = "citron_logo";
const std::string tinfoil_base_url = "https://tinfoil.media/ti/";
s64 start_time = std::chrono::duration_cast < std::chrono::seconds > (
std::chrono::system_clock::now().time_since_epoch())
.count();
DiscordRichPresence presence {};

// Store the URL string to prevent it from being destroyed
if (!game_title_id.empty()) {
game_url = fmt::format("{}{}/256/256", tinfoil_base_url, game_title_id);
// Make sure the string stays alive for the duration of the presence
cached_url = game_url;
presence.largeImageKey = cached_url.c_str();
} else {
presence.largeImageKey = cached_url.c_str();
}

presence.largeImageText = game_title.c_str();
presence.smallImageKey = default_image.c_str();
presence.smallImageText = default_text.c_str();
// Remove title ID from display, only show game title
presence.state = game_title.c_str();
presence.details = "Currently in game";
presence.startTimestamp = start_time;
Discord_UpdatePresence( & presence);
}

void DiscordImpl::Update() {
const std::string default_text = "Citron Is A Homebrew Emulator For The Nintendo Switch";
const std::string default_image = "citron_logo";

if (system.IsPoweredOn()) {
system.GetAppLoader().ReadTitle(game_title);
system.GetAppLoader().ReadProgramId(program_id);
game_title_id = fmt::format("{:016X}", program_id);

fmt::print("Title ID: {}\n", game_title_id);

QNetworkAccessManager manager;
QNetworkRequest request;
request.setUrl(QUrl(QString::fromStdString(
fmt::format("https://tinfoil.media/ti/{}/256/256", game_title_id))));
request.setTransferTimeout(10000);
QNetworkReply * reply = manager.head(request);
QEventLoop request_event_loop;
QObject::connect(reply, & QNetworkReply::finished, & request_event_loop, & QEventLoop::quit);
request_event_loop.exec();

if (reply -> error()) {
fmt::print("Failed to fetch game image: {} ({})\n", reply -> errorString().toStdString(),
program_id);
}

UpdateGameStatus(reply -> error());
reply -> deleteLater();
return;
}

s64 start_time = std::chrono::duration_cast < std::chrono::seconds > (
std::chrono::system_clock::now().time_since_epoch())
.count();

DiscordRichPresence presence {};
presence.largeImageKey = default_image.c_str();
presence.largeImageText = default_text.c_str();
presence.details = "Currently not in game";
presence.startTimestamp = start_time;
Discord_UpdatePresence( & presence);
}
} // namespace DiscordRPC
