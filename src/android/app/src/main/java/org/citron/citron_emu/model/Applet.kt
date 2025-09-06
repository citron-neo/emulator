// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.model

import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import org.citron.citron_emu.R

data class Applet(
    @StringRes val titleId: Int,
    @StringRes val descriptionId: Int,
    @DrawableRes val iconId: Int,
    val appletInfo: AppletInfo,
    val cabinetMode: CabinetMode = CabinetMode.None
)

// Combination of Common::AM::Applets::AppletId enum and the entry id
enum class AppletInfo(val appletId: Int, val entryId: Long = 0) {
    None(0x00),
    Application(0x01),
    OverlayDisplay(0x02),
    QLaunch(0x03),
    Starter(0x04),
    Auth(0x0A),
    Cabinet(0x0B, 0x0100000000001002),
    Controller(0x0C),
    DataErase(0x0D),
    Error(0x0E),
    NetConnect(0x0F),
    ProfileSelect(0x10),
    SoftwareKeyboard(0x11),
    MiiEdit(0x12, 0x0100000000001009),
    Web(0x13),
    Shop(0x14),
    PhotoViewer(0x015, 0x010000000000100D),
    Settings(0x16),
    OfflineWeb(0x17),
    LoginShare(0x18),
    WebAuth(0x19),
    MyPage(0x1A)
}

// Matches enum in Service::NFP::CabinetMode with extra metadata
enum class CabinetMode(
    val id: Int,
    @StringRes val titleId: Int = 0,
    @DrawableRes val iconId: Int = 0
) {
    None(-1),
    StartNicknameAndOwnerSettings(0, R.string.cabinet_nickname_and_owner, R.drawable.ic_edit),
    StartGameDataEraser(1, R.string.cabinet_game_data_eraser, R.drawable.ic_refresh),
    StartRestorer(2, R.string.cabinet_restorer, R.drawable.ic_restore),
    StartFormatter(3, R.string.cabinet_formatter, R.drawable.ic_clear)
}
