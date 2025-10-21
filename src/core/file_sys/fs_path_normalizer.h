// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>
#include "core/hle/result.h"

namespace FileSys {

/// Path normalization and validation utilities
/// Based on LibHac's PathNormalizer and PathUtility
class PathNormalizer {
public:
    /// Maximum path length for Nintendo Switch filesystem
    static constexpr size_t MaxPathLength = 0x300; // 768 bytes

    /// Normalize a path by resolving ".", "..", removing redundant slashes, etc.
    /// Returns the normalized path and Result
    static Result Normalize(std::string* out_path, std::string_view path);

    /// Validate that a path contains only valid characters
    static Result ValidateCharacters(std::string_view path);

    /// Check if a path is normalized
    static bool IsNormalized(std::string_view path);

    /// Check if path is valid for Nintendo Switch filesystem
    static Result ValidatePath(std::string_view path);

private:
    /// Check if a character is valid in a path
    static bool IsValidCharacter(char c);

    /// Remove redundant slashes and resolve "." and ".."
    static Result NormalizeImpl(std::string* out_path, std::string_view path);
};

/// Helper functions for path manipulation
namespace PathUtility {

/// Check if path represents root directory
bool IsRootPath(std::string_view path);

/// Check if path starts with "/"
bool IsAbsolutePath(std::string_view path);

/// Remove trailing slashes
std::string RemoveTrailingSlashes(std::string_view path);

/// Combine two paths
std::string CombinePaths(std::string_view base, std::string_view relative);

} // namespace PathUtility

} // namespace FileSys
