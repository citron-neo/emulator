// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>

namespace VideoCore { class RendererBase; }
namespace Core::Frontend { class EmuWindow; }
namespace Core::Detail {
// Internal to the private cold owner bootstrap, never a normal renderer option.
std::unique_ptr<VideoCore::RendererBase> CreateRestrictedProcessRenderer(Frontend::EmuWindow& window);
}
