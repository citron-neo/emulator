// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <filesystem>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "citron/mod_manager/zip_extractor.h"
#include "common/fs/fs.h"
#include "common/logging.h"

namespace ModManager {

bool ZipExtractor::CanExtract() {
#ifdef _WIN32
    // Check for PowerShell (built-in) or 7z
    if (!QStandardPaths::findExecutable(QStringLiteral("powershell")).isEmpty()) {
        return true;
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty()) {
        return true;
    }
#else
    // Check for 7z, unzip, or unar
    if (!QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty()) {
        return true;
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("7za")).isEmpty()) {
        return true;
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty()) {
        return true;
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("unar")).isEmpty()) {
        return true;
    }
#endif
    return false;
}

bool ZipExtractor::ExtractToPath(const QString& zip_path, const QString& dest_path) {
    QDir().mkpath(dest_path);

#ifdef _WIN32
    // On Windows, use PowerShell's Expand-Archive for .zip, or 7z for others
    if (zip_path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        QString powershell = QStandardPaths::findExecutable(QStringLiteral("powershell"));
        if (!powershell.isEmpty()) {
            QProcess process;
            process.setProgram(powershell);
            process.setArguments(
                {QStringLiteral("-Command"),
                 QStringLiteral("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                     .arg(zip_path, dest_path)});
            process.start();
            process.waitForFinished(60000);
            if (process.exitCode() == 0)
                return true;
        }
    }

    // Fallback to 7z
    QString p7z = QStandardPaths::findExecutable(QStringLiteral("7z"));
    if (!p7z.isEmpty()) {
        QProcess process;
        process.setProgram(p7z);
        process.setArguments(
            {QStringLiteral("x"), zip_path, QStringLiteral("-o") + dest_path, QStringLiteral("-y")});
        process.start();
        process.waitForFinished(120000);
        return process.exitCode() == 0;
    }
#else
    // On Linux/macOS, try 7z first
    QString p7z = QStandardPaths::findExecutable(QStringLiteral("7z"));
    if (!p7z.isEmpty()) {
        QProcess process;
        process.setProgram(p7z);
        process.setArguments(
            {QStringLiteral("x"), zip_path, QStringLiteral("-o") + dest_path, QStringLiteral("-y")});
        process.start();
        if (process.waitForFinished(120000) && process.exitCode() == 0) {
            return true;
        }
    }

    // Try unzip for .zip files
    if (zip_path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        QString unzip = QStandardPaths::findExecutable(QStringLiteral("unzip"));
        if (!unzip.isEmpty()) {
            QProcess process;
            process.setProgram(unzip);
            process.setArguments({QStringLiteral("-o"), zip_path, QStringLiteral("-d"), dest_path});
            process.start();
            if (process.waitForFinished(60000) && process.exitCode() == 0)
                return true;
        }
    }

    // Try 7za
    QString p7za = QStandardPaths::findExecutable(QStringLiteral("7za"));
    if (!p7za.isEmpty()) {
        QProcess process;
        process.setProgram(p7za);
        process.setArguments(
            {QStringLiteral("x"), zip_path, QStringLiteral("-o") + dest_path, QStringLiteral("-y")});
        process.start();
        if (process.waitForFinished(120000) && process.exitCode() == 0)
            return true;
    }

    // Try unar
    QString unar = QStandardPaths::findExecutable(QStringLiteral("unar"));
    if (!unar.isEmpty()) {
        QProcess process;
        process.setProgram(unar);
        process.setArguments({QStringLiteral("-o"), dest_path, QStringLiteral("-f"), zip_path});
        process.start();
        if (process.waitForFinished(120000) && process.exitCode() == 0)
            return true;
    }
#endif

    return false;
}

QStringList ZipExtractor::ListContents(const QString& zip_path) {
    QStringList contents;

#ifdef _WIN32
    QString p7z = QStandardPaths::findExecutable(QStringLiteral("7z"));
    if (!p7z.isEmpty()) {
        QProcess process;
        process.setProgram(p7z);
        process.setArguments({QStringLiteral("l"), zip_path});
        process.start();
        process.waitForFinished(30000);
        QString output = QString::fromUtf8(process.readAllStandardOutput());
        QStringList lines = output.split(QStringLiteral("\n"));
        bool in_list = false;
        for (const QString& line : lines) {
            if (line.contains(QStringLiteral("-------------------"))) {
                in_list = !in_list;
                continue;
            }
            if (in_list && line.length() > 53) {
                contents.append(line.mid(53).trimmed());
            }
        }
        if (!contents.isEmpty())
            return contents;
    }

    QString powershell = QStandardPaths::findExecutable(QStringLiteral("powershell"));
    if (!powershell.isEmpty()) {
        QProcess process;
        process.setProgram(powershell);
        process.setArguments(
            {QStringLiteral("-Command"),
             QStringLiteral(
                 "(Get-ChildItem -Path (New-Object "
                 "System.IO.Compression.ZipFile)::OpenRead('%1').Entries.FullName) -join \"`n\"")
                 .arg(zip_path)});
        process.start();
        process.waitForFinished(30000);
        QString output = QString::fromUtf8(process.readAllStandardOutput());
        return output.split(QStringLiteral("\n"), Qt::SkipEmptyParts);
    }
#else
    QString p7z = QStandardPaths::findExecutable(QStringLiteral("7z"));
    if (!p7z.isEmpty()) {
        QProcess process;
        process.setProgram(p7z);
        process.setArguments({QStringLiteral("l"), zip_path});
        process.start();
        process.waitForFinished(30000);
        QString output = QString::fromUtf8(process.readAllStandardOutput());

        QStringList lines = output.split(QStringLiteral("\n"));
        bool in_list = false;
        for (const QString& line : lines) {
            if (line.contains(QStringLiteral("-------------------"))) {
                in_list = !in_list;
                continue;
            }
            if (in_list && line.length() > 53) {
                contents.append(line.mid(53).trimmed());
            }
        }
        if (!contents.isEmpty())
            return contents;
    }

    QString unzip = QStandardPaths::findExecutable(QStringLiteral("unzip"));
    if (!unzip.isEmpty()) {
        QProcess process;
        process.setProgram(unzip);
        process.setArguments({QStringLiteral("-l"), zip_path});
        process.start();
        process.waitForFinished(30000);
        QString output = QString::fromUtf8(process.readAllStandardOutput());

        QStringList lines = output.split(QStringLiteral("\n"));
        bool in_file_list = false;
        for (const QString& line : lines) {
            if (line.contains(QStringLiteral("----"))) {
                in_file_list = !in_file_list;
                continue;
            }
            if (in_file_list && !line.trimmed().isEmpty()) {
                QStringList parts =
                    line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                if (parts.size() >= 4) {
                    contents.append(parts.last());
                }
            }
        }
    }
#endif

    return contents;
}

bool ZipExtractor::HasPrefix(const QStringList& entries, const QString& prefix) {
    for (const QString& entry : entries) {
        if (entry.startsWith(prefix, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

ModStructure ZipExtractor::DetectModStructure(const QString& zip_path) {
    QStringList contents = ListContents(zip_path);

    if (contents.isEmpty()) {
        return ModStructure::Unknown;
    }

    bool has_romfs = HasPrefix(contents, QStringLiteral("romfs/")) ||
                     HasPrefix(contents, QStringLiteral("romfs\\"));
    bool has_exefs = HasPrefix(contents, QStringLiteral("exefs/")) ||
                     HasPrefix(contents, QStringLiteral("exefs\\"));
    bool has_cheats = HasPrefix(contents, QStringLiteral("cheats/")) ||
                      HasPrefix(contents, QStringLiteral("cheats\\"));

    if (!has_romfs && !has_exefs && !has_cheats) {
        for (const QString& entry : contents) {
            if (entry.contains(QStringLiteral("/romfs/"), Qt::CaseInsensitive) ||
                entry.contains(QStringLiteral("\\romfs\\"), Qt::CaseInsensitive)) {
                has_romfs = true;
            }
            if (entry.contains(QStringLiteral("/exefs/"), Qt::CaseInsensitive) ||
                entry.contains(QStringLiteral("\\exefs\\"), Qt::CaseInsensitive)) {
                has_exefs = true;
            }
            if (entry.contains(QStringLiteral("/cheats/"), Qt::CaseInsensitive) ||
                entry.contains(QStringLiteral("\\cheats\\"), Qt::CaseInsensitive)) {
                has_cheats = true;
            }
        }
    }

    if (has_cheats && !has_romfs && !has_exefs) {
        return ModStructure::Cheats;
    }
    if (has_romfs && has_exefs) {
        return ModStructure::Mixed;
    }
    if (has_romfs) {
        return ModStructure::RomFS;
    }
    if (has_exefs) {
        return ModStructure::ExeFS;
    }

    bool only_txt = true;
    for (const QString& entry : contents) {
        QFileInfo info(entry);
        if (info.isDir())
            continue;
        if (info.suffix().compare(QStringLiteral("txt"), Qt::CaseInsensitive) != 0) {
            only_txt = false;
            break;
        }
    }
    if (only_txt && !contents.isEmpty())
        return ModStructure::Cheats;

    return ModStructure::Flat;
}

static void RobustMove(const std::filesystem::path& src, const std::filesystem::path& dst) {
    std::error_code ec;
    if (std::filesystem::exists(dst, ec)) {
        std::filesystem::remove_all(dst, ec);
    }
    std::filesystem::create_directories(dst.parent_path(), ec);

    std::filesystem::rename(src, dst, ec);
    if (ec) {
        std::filesystem::copy(src, dst, std::filesystem::copy_options::recursive, ec);
        if (!ec) {
            std::filesystem::remove_all(src, ec);
        }
    }
}

bool ZipExtractor::ExtractAndOrganize(const QString& zip_path, const QString& mod_folder) {
    QTemporaryDir temp_dir;
    if (!temp_dir.isValid())
        return false;

    if (!ExtractToPath(zip_path, temp_dir.path()))
        return false;

    ModStructure structure = DetectModStructure(zip_path);
    std::filesystem::path src_root(temp_dir.path().toStdString());
    std::filesystem::path dst_root(mod_folder.toStdString());

    QDir temp(temp_dir.path());
    QStringList entries = temp.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    if (entries.size() == 1 && QFileInfo(temp.absoluteFilePath(entries[0])).isDir()) {
        src_root /= entries[0].toStdString();
    }

    if (structure == ModStructure::Flat) {
        std::filesystem::path romfs_dst = dst_root / "romfs";
        RobustMove(src_root, romfs_dst);
    } else if (structure == ModStructure::Cheats) {
        std::filesystem::path cheats_src = src_root / "cheats";
        if (!std::filesystem::exists(cheats_src)) {
            std::filesystem::path cheats_dst = dst_root / "cheats";
            RobustMove(src_root, cheats_dst);
        } else {
            RobustMove(src_root, dst_root);
        }
    } else {
        RobustMove(src_root, dst_root);
    }

    return true;
}

} // namespace ModManager
