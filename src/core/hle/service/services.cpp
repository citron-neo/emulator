// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/services.h"

#include "core/hle/service/acc/acc.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/aoc/addon_content_manager.h"
#include "core/hle/service/apm/apm.h"
#include "core/hle/service/audio/audio.h"
#include "core/hle/service/bcat/bcat.h"
#include "core/hle/service/bpc/bpc.h"
#include "core/hle/service/btdrv/btdrv.h"
#include "core/hle/service/btm/btm.h"
#include "core/hle/service/caps/caps.h"
#include "core/hle/service/erpt/erpt.h"
#include "core/hle/service/es/es.h"
#include "core/hle/service/eupld/eupld.h"
#include "core/hle/service/fatal/fatal.h"
#include "core/hle/service/fgm/fgm.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/friend/friend.h"
#include "core/hle/service/glue/glue.h"
#include "core/hle/service/grc/grc.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/jit/jit.h"
#include "core/hle/service/lbl/lbl.h"
#include "core/hle/service/ldn/ldn.h"
#include "core/hle/service/ldr/ldr.h"
#include "core/hle/service/lm/lm.h"
#include "core/hle/service/mig/mig.h"
#include "core/hle/service/mii/mii.h"
#include "core/hle/service/mm/mm_u.h"
#include "core/hle/service/mnpp/mnpp_app.h"
#include "core/hle/service/ncm/ncm.h"
#include "core/hle/service/nfc/nfc.h"
#include "core/hle/service/nfp/nfp.h"
#include "core/hle/service/ngc/ngc.h"
#include "core/hle/service/nex/nex.h"
#include "core/hle/service/nifm/nifm.h"
#include "core/hle/service/nim/nim.h"
#include "core/hle/service/npns/npns.h"
#include "core/hle/service/ns/ns.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvnflinger/nvnflinger.h"
#include "core/hle/service/olsc/olsc.h"
#include "core/hle/service/omm/omm.h"
#include "core/hle/service/pcie/pcie.h"
#include "core/hle/service/pctl/pctl.h"
#include "core/hle/service/pcv/pcv.h"
#include "core/hle/service/pm/pm.h"
#include "core/hle/service/prepo/prepo.h"
#include "core/hle/service/psc/psc.h"
#include "core/hle/service/ptm/ptm.h"
#include "core/hle/service/ro/ro.h"
#include "core/hle/service/service.h"
#include "core/hle/service/set/settings.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/sockets/sockets.h"
#include "core/hle/service/spl/spl_module.h"
#include "core/hle/service/ssl/ssl.h"
#include "core/hle/service/tma/tma.h"
#include "core/hle/service/usb/usb.h"
#include "core/hle/service/vi/vi.h"

namespace Service {

Services::Services(std::shared_ptr<SM::ServiceManager>& sm, Core::System& system,
                   std::stop_token token) {
    auto& kernel = system.Kernel();

    system.GetFileSystemController().CreateFactories(*system.GetFilesystem(), false);

    // clang-format off
    kernel.RunOnHostCoreProcess("audio",      [&] { Audio::LoopProcess(system); }).detach();
    kernel.RunOnHostCoreProcess("FS",         [&] { FileSystem::LoopProcess(system); }).detach();
    kernel.RunOnHostCoreProcess("jit",        [&] { JIT::LoopProcess(system); }).detach();
    kernel.RunOnHostCoreProcess("ldn",        [&] { LDN::LoopProcess(system); }).detach();
    kernel.RunOnHostCoreProcess("Loader",     [&] { LDR::LoopProcess(system); }).detach();
    kernel.RunOnHostCoreProcess("nvservices", [&] { Nvidia::LoopProcess(system); }).detach();
    kernel.RunOnHostCoreProcess("bsdsocket",  [&] { Sockets::LoopProcess(system); }).detach();
    kernel.RunOnHostCoreProcess("vi",         [&, token] { VI::LoopProcess(system, token); }).detach();

    kernel.RunOnGuestCoreProcess("sm",         [&] { SM::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("account",    [&] { Account::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("am",         [&] { AM::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("aoc",        [&] { AOC::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("apm",        [&] { APM::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("bcat",       [&] { BCAT::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("bpc",        [&] { BPC::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("btdrv",      [&] { BtDrv::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("btm",        [&] { BTM::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("capsrv",     [&] { Capture::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("erpt",       [&] { ERPT::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("es",         [&] { ES::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("eupld",      [&] { EUPLD::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("fatal",      [&] { Fatal::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("fgm",        [&] { FGM::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("friends",    [&] { Friend::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("settings",   [&] { Set::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("psc",        [&] { PSC::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("glue",       [&] { Glue::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("grc",        [&] { GRC::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("hid",        [&] { HID::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("lbl",        [&] { LBL::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("LogManager.Prod", [&] { LM::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("mig",        [&] { Migration::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("mii",        [&] { Mii::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("mm",         [&] { MM::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("mnpp",       [&] { MNPP::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("nvnflinger", [&] { Nvnflinger::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("NCM",        [&] { NCM::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("nfc",        [&] { NFC::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("nfp",        [&] { NFP::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("ngc",        [&] { NGC::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("nex",        [&] { Nex::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("nifm",       [&] { NIFM::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("nim",        [&] { NIM::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("npns",       [&] { NPNS::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("ns",         [&] { NS::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("olsc",       [&] { OLSC::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("omm",        [&] { OMM::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("pcie",       [&] { PCIe::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("pctl",       [&] { PCTL::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("pcv",        [&] { PCV::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("prepo",      [&] { PlayReport::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("ProcessManager", [&] { PM::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("ptm",        [&] { PTM::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("ro",         [&] { RO::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("spl",        [&] { SPL::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("ssl",        [&] { SSL::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("tma",        [&] { TMA::LoopProcess(system); });
    kernel.RunOnGuestCoreProcess("usb",        [&] { USB::LoopProcess(system); });
    // clang-format on
}

Services::~Services() = default;

} // namespace Service
