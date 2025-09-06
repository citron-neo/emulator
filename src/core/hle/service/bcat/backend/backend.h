// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <optional>
#include <string>

#include "common/common_types.h"
#include "core/file_sys/vfs/vfs_types.h"
#include "core/hle/result.h"
#include "core/hle/service/bcat/bcat_types.h"
#include "core/hle/service/kernel_helpers.h"

namespace Core {
class System;
}

namespace Kernel {
class KernelCore;
class KEvent;
class KReadableEvent;
} // namespace Kernel

namespace Service::BCAT {

// A class to manage the signalling to the game about BCAT download progress.
// Some of this class is implemented in module.cpp to avoid exposing the implementation structure.
class ProgressServiceBackend {
    friend class IBcatService;
    friend class IServiceCreator;

public:
    ~ProgressServiceBackend();

    // Sets the number of bytes total in the entire download.
    void SetTotalSize(u64 size);

    // Notifies the application that the backend has started connecting to the server.
    void StartConnecting();
    // Notifies the application that the backend has begun accumulating and processing metadata.
    void StartProcessingDataList();

    // Notifies the application that a file is starting to be downloaded.
    void StartDownloadingFile(std::string_view dir_name, std::string_view file_name, u64 file_size);
    // Updates the progress of the current file to the size passed.
    void UpdateFileProgress(u64 downloaded);
    // Notifies the application that the current file has completed download.
    void FinishDownloadingFile();

    // Notifies the application that all files in this directory have completed and are being
    // finalized.
    void CommitDirectory(std::string_view dir_name);

    // Notifies the application that the operation completed with result code result.
    void FinishDownload(Result result);

private:
    explicit ProgressServiceBackend(Core::System& system, std::string_view event_name);

    Kernel::KReadableEvent& GetEvent();
    DeliveryCacheProgressImpl& GetImpl();

    void SignalUpdate();

    KernelHelpers::ServiceContext service_context;

    DeliveryCacheProgressImpl impl{};
    Kernel::KEvent* update_event;
};

// A class representing an abstract backend for BCAT functionality.
class BcatBackend {
public:
    explicit BcatBackend(DirectoryGetter getter);
    virtual ~BcatBackend();

    // Called when the backend is needed to synchronize the data for the game with title ID and
    // version in title. A ProgressServiceBackend object is provided to alert the application of
    // status.
    virtual bool Synchronize(TitleIDVersion title, ProgressServiceBackend& progress) = 0;
    // Very similar to Synchronize, but only for the directory provided. Backends should not alter
    // the data for any other directories.
    virtual bool SynchronizeDirectory(TitleIDVersion title, std::string name,
                                      ProgressServiceBackend& progress) = 0;

    // Removes all cached data associated with title id provided.
    virtual bool Clear(u64 title_id) = 0;

    // Sets the BCAT Passphrase to be used with the associated title ID.
    virtual void SetPassphrase(u64 title_id, const Passphrase& passphrase) = 0;

    // Gets the launch parameter used by AM associated with the title ID and version provided.
    virtual std::optional<std::vector<u8>> GetLaunchParameter(TitleIDVersion title) = 0;

protected:
    DirectoryGetter dir_getter;
};

// A backend of BCAT that provides no operation.
class NullBcatBackend : public BcatBackend {
public:
    explicit NullBcatBackend(DirectoryGetter getter);
    ~NullBcatBackend() override;

    bool Synchronize(TitleIDVersion title, ProgressServiceBackend& progress) override;
    bool SynchronizeDirectory(TitleIDVersion title, std::string name,
                              ProgressServiceBackend& progress) override;

    bool Clear(u64 title_id) override;

    void SetPassphrase(u64 title_id, const Passphrase& passphrase) override;

    std::optional<std::vector<u8>> GetLaunchParameter(TitleIDVersion title) override;
};

std::unique_ptr<BcatBackend> CreateBackendFromSettings(Core::System& system,
                                                       DirectoryGetter getter);

} // namespace Service::BCAT
