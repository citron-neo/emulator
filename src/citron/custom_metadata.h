// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include "common/common_types.h"

namespace Citron {

struct CustomGameMetadata {
    std::string title;
    std::string icon_path;
};

class CustomMetadata {
public:
    static CustomMetadata& GetInstance() {
        static CustomMetadata instance;
        return instance;
    }

    ~CustomMetadata();

    [[nodiscard]] std::optional<std::string> GetCustomTitle(u64 program_id) const;
    [[nodiscard]] std::optional<std::string> GetCustomIconPath(u64 program_id) const;

    void SetCustomTitle(u64 program_id, const std::string& title);
    void SetCustomIcon(u64 program_id, const std::string& icon_path);
    void RemoveCustomMetadata(u64 program_id);

    void Save();
    void Load();

private:
    explicit CustomMetadata();
    std::unordered_map<u64, CustomGameMetadata> metadata;
};

} // namespace Citron
