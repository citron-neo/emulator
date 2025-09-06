// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.model

import android.content.Intent
import android.net.Uri
import android.os.Parcelable
import java.util.HashSet
import kotlinx.parcelize.Parcelize
import kotlinx.serialization.Serializable
import org.citron.citron_emu.NativeLibrary
import org.citron.citron_emu.R
import org.citron.citron_emu.CitronApplication
import org.citron.citron_emu.activities.EmulationActivity
import org.citron.citron_emu.utils.DirectoryInitialization
import org.citron.citron_emu.utils.FileUtil
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

@Parcelize
@Serializable
class Game(
    val title: String = "",
    val path: String,
    val programId: String = "",
    val developer: String = "",
    var version: String = "",
    val isHomebrew: Boolean = false
) : Parcelable {
    val keyAddedToLibraryTime get() = "${path}_AddedToLibraryTime"
    val keyLastPlayedTime get() = "${path}_LastPlayed"

    val settingsName: String
        get() {
            val programIdLong = programId.toLong()
            return if (programIdLong == 0L) {
                FileUtil.getFilename(Uri.parse(path))
            } else {
                "0" + programIdLong.toString(16).uppercase()
            }
        }

    val programIdHex: String
        get() {
            val programIdLong = programId.toLong()
            return if (programIdLong == 0L) {
                "0"
            } else {
                "0" + programIdLong.toString(16).uppercase()
            }
        }

    val saveZipName: String
        get() = "$title ${CitronApplication.appContext.getString(R.string.save_data).lowercase()} - ${
        LocalDateTime.now().format(DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm"))
        }.zip"

    val saveDir: String
        get() = DirectoryInitialization.userDirectory + "/nand" +
            NativeLibrary.getSavePath(programId)

    val addonDir: String
        get() = DirectoryInitialization.userDirectory + "/load/" + programIdHex + "/"

    val launchIntent: Intent
        get() = Intent(CitronApplication.appContext, EmulationActivity::class.java).apply {
            action = Intent.ACTION_VIEW
            data = Uri.parse(path)
        }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false

        other as Game

        if (title != other.title) return false
        if (path != other.path) return false
        if (programId != other.programId) return false
        if (developer != other.developer) return false
        if (version != other.version) return false
        if (isHomebrew != other.isHomebrew) return false

        return true
    }

    override fun hashCode(): Int {
        var result = title.hashCode()
        result = 31 * result + path.hashCode()
        result = 31 * result + programId.hashCode()
        result = 31 * result + developer.hashCode()
        result = 31 * result + version.hashCode()
        result = 31 * result + isHomebrew.hashCode()
        return result
    }

    companion object {
        val extensions: Set<String> = HashSet(
            listOf("xci", "nsp", "nca", "nro")
        )
    }
}
