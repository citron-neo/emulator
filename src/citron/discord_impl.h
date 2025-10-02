// SPDX-FileCopyrightText: 2018 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include "citron/discord.h"

namespace Core {
	class System;
}

namespace DiscordRPC {

	class DiscordImpl : public DiscordInterface {
	public:
		DiscordImpl(Core::System& system_);
		~DiscordImpl() override;

		void Pause() override;
		void Update() override;

		void ThreadRun();
		std::thread discord_thread;
		std::atomic<bool> discord_thread_running;

	private:
		void UpdateGameStatus(bool use_default);

		std::string game_url{};
		std::string game_title{};
		std::string game_title_id{};
		std::string cached_url;

		Core::System& system;
		u64 program_id = 0;

		s64 current_state_start_time = 0;

		bool was_powered_on = false;
	};

} // namespace DiscordRPC
