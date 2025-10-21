// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <vector>
#include "common/string_util.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/fs_path_normalizer.h"

namespace FileSys {

namespace {

constexpr char DirectorySeparator = '/';
constexpr std::string_view CurrentDirectory = ".";
constexpr std::string_view ParentDirectory = "..";

// Invalid characters for Nintendo Switch paths
bool IsInvalidCharacter(char c) {
    // Control characters
    if (c < 0x20) {
        return true;
    }
    // Reserved characters
    switch (c) {
    case '<':
    case '>':
    case '"':
    case '\\':
    case '|':
    case '?':
    case '*':
    case ':': // Colon is invalid except for drive letters (which we don't use)
        return true;
    default:
        return false;
    }
}

} // Anonymous namespace

bool PathNormalizer::IsValidCharacter(char c) {
    return !IsInvalidCharacter(c);
}

Result PathNormalizer::ValidateCharacters(std::string_view path) {
    for (char c : path) {
        if (IsInvalidCharacter(c)) {
            return ResultInvalidCharacter;
        }
    }
    return ResultSuccess;
}

Result PathNormalizer::ValidatePath(std::string_view path) {
    // Check path length
    if (path.length() >= MaxPathLength) {
        return ResultTooLongPath;
    }

    // Check for invalid characters
    R_TRY(ValidateCharacters(path));

    // Empty path is valid (represents current directory)
    if (path.empty()) {
        return ResultSuccess;
    }

    return ResultSuccess;
}

Result PathNormalizer::Normalize(std::string* out_path, std::string_view path) {
    // Validate input
    R_TRY(ValidatePath(path));

    // Empty or root path
    if (path.empty() || path == "/") {
        *out_path = "/";
        return ResultSuccess;
    }

    return NormalizeImpl(out_path, path);
}

Result PathNormalizer::NormalizeImpl(std::string* out_path, std::string_view path) {
    std::vector<std::string> components;
    std::string current_component;

    // Split path into components and resolve "." and ".."
    for (size_t i = 0; i < path.length(); ++i) {
        char c = path[i];

        if (c == DirectorySeparator) {
            if (!current_component.empty()) {
                if (current_component == CurrentDirectory) {
                    // Skip "." components
                } else if (current_component == ParentDirectory) {
                    // Go up one directory
                    if (components.empty()) {
                        // Can't go above root
                        return ResultInvalidPath;
                    }
                    components.pop_back();
                } else {
                    components.push_back(current_component);
                }
                current_component.clear();
            }
            // Skip redundant slashes
        } else {
            current_component += c;
        }
    }

    // Handle last component
    if (!current_component.empty()) {
        if (current_component == CurrentDirectory) {
            // Skip
        } else if (current_component == ParentDirectory) {
            if (components.empty()) {
                return ResultInvalidPath;
            }
            components.pop_back();
        } else {
            components.push_back(current_component);
        }
    }

    // Build normalized path
    if (components.empty()) {
        *out_path = "/";
        return ResultSuccess;
    }

    std::string normalized = "";
    for (const auto& component : components) {
        normalized += DirectorySeparator;
        normalized += component;
    }

    // Check normalized path length
    if (normalized.length() >= MaxPathLength) {
        return ResultTooLongPath;
    }

    *out_path = std::move(normalized);
    return ResultSuccess;
}

bool PathNormalizer::IsNormalized(std::string_view path) {
    // Empty path is normalized
    if (path.empty()) {
        return true;
    }

    // Check for invalid characters
    if (ValidateCharacters(path) != ResultSuccess) {
        return false;
    }

    // Check for "." or ".." components
    if (path.find("/.") != std::string_view::npos ||
        path.find("./") != std::string_view::npos ||
        path == "." || path == "..") {
        return false;
    }

    // Check for redundant slashes
    if (path.find("//") != std::string_view::npos) {
        return false;
    }

    // Check for trailing slashes (except for root)
    if (path.length() > 1 && path.back() == DirectorySeparator) {
        return false;
    }

    return true;
}

namespace PathUtility {

bool IsRootPath(std::string_view path) {
    return path == "/" || path.empty();
}

bool IsAbsolutePath(std::string_view path) {
    return !path.empty() && path[0] == DirectorySeparator;
}

std::string RemoveTrailingSlashes(std::string_view path) {
    if (path.empty() || path == "/") {
        return std::string(path);
    }

    size_t end = path.length();
    while (end > 1 && path[end - 1] == DirectorySeparator) {
        --end;
    }

    return std::string(path.substr(0, end));
}

std::string CombinePaths(std::string_view base, std::string_view relative) {
    if (relative.empty()) {
        return std::string(base);
    }

    if (IsAbsolutePath(relative)) {
        return std::string(relative);
    }

    std::string result(base);
    if (!result.empty() && result.back() != DirectorySeparator) {
        result += DirectorySeparator;
    }
    result += relative;

    return result;
}

} // namespace PathUtility

} // namespace FileSys
