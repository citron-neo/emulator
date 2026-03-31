// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once



namespace Tools {

class RenderdocAPI {
public:
    explicit RenderdocAPI();
    ~RenderdocAPI();

    void ToggleCapture();

private:
    void* rdoc_api{};
    bool is_capturing{false};
};

} // namespace Tools
