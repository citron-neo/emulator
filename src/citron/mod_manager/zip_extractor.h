// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>
#include <QStringList>

namespace ModManager {

enum class ModStructure {
    Unknown,
    RomFS,  // Contains romfs/ folder
    ExeFS,  // Contains exefs/ folder
    Mixed,  // Contains both romfs/ and exefs/
    Cheats, // Contains cheats/ folder
    Flat    // Loose files, no standard structure
};

class ZipExtractor {
public:
    // Extract a ZIP file to the destination path
    // Returns true on success, false on failure
    static bool ExtractToPath(const QString& zip_path, const QString& dest_path);

    // Analyze a ZIP file to determine its mod structure
    static ModStructure DetectModStructure(const QString& zip_path);

    // Get the list of files in a ZIP archive
    static QStringList ListContents(const QString& zip_path);

    // Extract and automatically organize based on detected structure
    // Places files in appropriate subdirectories (romfs/, exefs/, cheats/)
    static bool ExtractAndOrganize(const QString& zip_path, const QString& mod_folder);

    // Check if necessary extraction tools are available on the system
    static bool CanExtract();

private:
    // Helper to check if any entry starts with a given prefix
    static bool HasPrefix(const QStringList& entries, const QString& prefix);
};

} // namespace ModManager
