// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu

import android.content.DialogInterface
import android.net.Uri
import android.text.Html
import android.text.method.LinkMovementMethod
import android.view.Surface
import android.view.View
import android.widget.TextView
import androidx.annotation.Keep
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.lang.ref.WeakReference
import org.citron.citron_emu.activities.EmulationActivity
import org.citron.citron_emu.fragments.CoreErrorDialogFragment
import org.citron.citron_emu.utils.DocumentsTree
import org.citron.citron_emu.utils.FileUtil
import org.citron.citron_emu.utils.Log
import org.citron.citron_emu.model.InstallResult
import org.citron.citron_emu.model.Patch
import org.citron.citron_emu.model.GameVerificationResult

/**
 * Class which contains methods that interact
 * with the native side of the Citron code.
 */
object NativeLibrary {
    @JvmField
    var sEmulationActivity = WeakReference<EmulationActivity?>(null)

    init {
        try {
            System.loadLibrary("citron-android")
        } catch (ex: UnsatisfiedLinkError) {
            error("[NativeLibrary] $ex")
        }
    }

    @Keep
    @JvmStatic
    fun openContentUri(path: String?, openmode: String?): Int {
        return if (DocumentsTree.isNativePath(path!!)) {
            CitronApplication.documentsTree!!.openContentUri(path, openmode)
        } else {
            FileUtil.openContentUri(path, openmode)
        }
    }

    @Keep
    @JvmStatic
    fun getSize(path: String?): Long {
        return if (DocumentsTree.isNativePath(path!!)) {
            CitronApplication.documentsTree!!.getFileSize(path)
        } else {
            FileUtil.getFileSize(path)
        }
    }

    @Keep
    @JvmStatic
    fun exists(path: String?): Boolean {
        return if (DocumentsTree.isNativePath(path!!)) {
            CitronApplication.documentsTree!!.exists(path)
        } else {
            FileUtil.exists(path, suppressLog = true)
        }
    }

    @Keep
    @JvmStatic
    fun isDirectory(path: String?): Boolean {
        return if (DocumentsTree.isNativePath(path!!)) {
            CitronApplication.documentsTree!!.isDirectory(path)
        } else {
            FileUtil.isDirectory(path)
        }
    }

    @Keep
    @JvmStatic
    fun getParentDirectory(path: String): String =
        if (DocumentsTree.isNativePath(path)) {
            CitronApplication.documentsTree!!.getParentDirectory(path)
        } else {
            path
        }

    @Keep
    @JvmStatic
    fun getFilename(path: String): String =
        if (DocumentsTree.isNativePath(path)) {
            CitronApplication.documentsTree!!.getFilename(path)
        } else {
            FileUtil.getFilename(Uri.parse(path))
        }

    external fun setAppDirectory(directory: String)

    /**
     * Installs a nsp or xci file to nand
     * @param filename String representation of file uri
     * @return int representation of [InstallResult]
     */
    external fun installFileToNand(
        filename: String,
        callback: (max: Long, progress: Long) -> Boolean
    ): Int

    external fun doesUpdateMatchProgram(programId: String, updatePath: String): Boolean

    external fun initializeGpuDriver(
        hookLibDir: String?,
        customDriverDir: String?,
        customDriverName: String?,
        fileRedirectDir: String?
    )

    external fun reloadKeys(): Boolean

    external fun initializeSystem(reload: Boolean)

    /**
     * Begins emulation.
     */
    external fun run(path: String?, programIndex: Int, frontendInitiated: Boolean)

    // Surface Handling
    external fun surfaceChanged(surf: Surface?)

    external fun surfaceDestroyed()

    /**
     * Unpauses emulation from a paused state.
     */
    external fun unpauseEmulation()

    /**
     * Pauses emulation.
     */
    external fun pauseEmulation()

    /**
     * Stops emulation.
     */
    external fun stopEmulation()

    /**
     * Returns true if emulation is running (or is paused).
     */
    external fun isRunning(): Boolean

    /**
     * Returns true if emulation is paused.
     */
    external fun isPaused(): Boolean

    /**
     * Returns the performance stats for the current game
     */
    external fun getPerfStats(): DoubleArray

    /**
     * Returns the number of shaders currently being built
     */
    external fun getShadersBuilding(): Int

    /**
     * Returns the current CPU backend.
     */
    external fun getCpuBackend(): String

    /**
     * Returns the current GPU Driver.
     */
    external fun getGpuDriver(): String

    external fun applySettings()

    external fun logSettings()

    enum class CoreError {
        ErrorSystemFiles,
        ErrorSavestate,
        ErrorUnknown
    }

    var coreErrorAlertResult = false
    val coreErrorAlertLock = Object()

    private fun onCoreErrorImpl(title: String, message: String) {
        val emulationActivity = sEmulationActivity.get()
        if (emulationActivity == null) {
            Log.error("[NativeLibrary] EmulationActivity not present")
            return
        }

        val fragment = CoreErrorDialogFragment.newInstance(title, message)
        fragment.show(emulationActivity.supportFragmentManager, "coreError")
    }

    /**
     * Handles a core error.
     *
     * @return true: continue; false: abort
     */
    fun onCoreError(error: CoreError?, details: String): Boolean {
        val emulationActivity = sEmulationActivity.get()
        if (emulationActivity == null) {
            Log.error("[NativeLibrary] EmulationActivity not present")
            return false
        }

        val title: String
        val message: String
        when (error) {
            CoreError.ErrorSystemFiles -> {
                title = emulationActivity.getString(R.string.system_archive_not_found)
                message = emulationActivity.getString(
                    R.string.system_archive_not_found_message,
                    details.ifEmpty { emulationActivity.getString(R.string.system_archive_general) }
                )
            }

            CoreError.ErrorSavestate -> {
                title = emulationActivity.getString(R.string.save_load_error)
                message = details
            }

            CoreError.ErrorUnknown -> {
                title = emulationActivity.getString(R.string.fatal_error)
                message = emulationActivity.getString(R.string.fatal_error_message)
            }

            else -> {
                return true
            }
        }

        // Show the AlertDialog on the main thread.
        emulationActivity.runOnUiThread { onCoreErrorImpl(title, message) }

        // Wait for the lock to notify that it is complete.
        synchronized(coreErrorAlertLock) { coreErrorAlertLock.wait() }

        return coreErrorAlertResult
    }

    @Keep
    @JvmStatic
    fun exitEmulationActivity(resultCode: Int) {
        val Success = 0
        val ErrorNotInitialized = 1
        val ErrorGetLoader = 2
        val ErrorSystemFiles = 3
        val ErrorSharedFont = 4
        val ErrorVideoCore = 5
        val ErrorUnknown = 6
        val ErrorLoader = 7

        val captionId: Int
        var descriptionId: Int
        when (resultCode) {
            ErrorVideoCore -> {
                captionId = R.string.loader_error_video_core
                descriptionId = R.string.loader_error_video_core_description
            }

            else -> {
                captionId = R.string.loader_error_encrypted
                descriptionId = R.string.loader_error_encrypted_roms_description
                if (!reloadKeys()) {
                    descriptionId = R.string.loader_error_encrypted_keys_description
                }
            }
        }

        val emulationActivity = sEmulationActivity.get()
        if (emulationActivity == null) {
            Log.warning("[NativeLibrary] EmulationActivity is null, can't exit.")
            return
        }

        val builder = MaterialAlertDialogBuilder(emulationActivity)
            .setTitle(captionId)
            .setMessage(
                Html.fromHtml(
                    emulationActivity.getString(descriptionId),
                    Html.FROM_HTML_MODE_LEGACY
                )
            )
            .setPositiveButton(android.R.string.ok) { _: DialogInterface?, _: Int ->
                emulationActivity.finish()
            }
            .setOnDismissListener { emulationActivity.finish() }
        emulationActivity.runOnUiThread {
            val alert = builder.create()
            alert.show()
            (alert.findViewById<View>(android.R.id.message) as TextView).movementMethod =
                LinkMovementMethod.getInstance()
        }
    }

    fun setEmulationActivity(emulationActivity: EmulationActivity?) {
        Log.debug("[NativeLibrary] Registering EmulationActivity.")
        sEmulationActivity = WeakReference(emulationActivity)
    }

    fun clearEmulationActivity() {
        Log.debug("[NativeLibrary] Unregistering EmulationActivity.")
        sEmulationActivity.clear()
    }

    @Keep
    @JvmStatic
    fun onEmulationStarted() {
        sEmulationActivity.get()!!.onEmulationStarted()
    }

    @Keep
    @JvmStatic
    fun onEmulationStopped(status: Int) {
        sEmulationActivity.get()!!.onEmulationStopped(status)
    }

    @Keep
    @JvmStatic
    fun onProgramChanged(programIndex: Int) {
        sEmulationActivity.get()!!.onProgramChanged(programIndex)
    }

    /**
     * Logs the Citron version, Android version and, CPU.
     */
    external fun logDeviceInfo()

    /**
     * Submits inline keyboard text. Called on input for buttons that result text.
     * @param text Text to submit to the inline software keyboard implementation.
     */
    external fun submitInlineKeyboardText(text: String?)

    /**
     * Submits inline keyboard input. Used to indicate keys pressed that are not text.
     * @param key_code Android Key Code associated with the keyboard input.
     */
    external fun submitInlineKeyboardInput(key_code: Int)

    /**
     * Creates a generic user directory if it doesn't exist already
     */
    external fun initializeEmptyUserDirectory()

    /**
     * Gets the launch path for a given applet. It is the caller's responsibility to also
     * set the system's current applet ID before trying to launch the nca given by this function.
     *
     * @param id The applet entry ID
     * @return The applet's launch path
     */
    external fun getAppletLaunchPath(id: Long): String

    /**
     * Sets the system's current applet ID before launching.
     *
     * @param appletId One of the ids in the Service::AM::Applets::AppletId enum
     */
    external fun setCurrentAppletId(appletId: Int)

    /**
     * Sets the cabinet mode for launching the cabinet applet.
     *
     * @param cabinetMode One of the modes that corresponds to the enum in Service::NFP::CabinetMode
     */
    external fun setCabinetMode(cabinetMode: Int)

    /**
     * Checks whether NAND contents are available and valid.
     *
     * @return 'true' if firmware is available
     */
    external fun isFirmwareAvailable(): Boolean

    /**
     * Checks the PatchManager for any addons that are available
     *
     * @param path Path to game file. Can be a [Uri].
     * @param programId String representation of a game's program ID
     * @return Array of available patches
     */
    external fun getPatchesForFile(path: String, programId: String): Array<Patch>?

    /**
     * Removes an update for a given [programId]
     * @param programId String representation of a game's program ID
     */
    external fun removeUpdate(programId: String)

    /**
     * Removes all DLC for a  [programId]
     * @param programId String representation of a game's program ID
     */
    external fun removeDLC(programId: String)

    /**
     * Removes a mod installed for a given [programId]
     * @param programId String representation of a game's program ID
     * @param name The name of a mod as given by [getPatchesForFile]. This corresponds with the name
     * of the mod's directory in a game's load folder.
     */
    external fun removeMod(programId: String, name: String)

    /**
     * Verifies all installed content
     * @param callback UI callback for verification progress. Return true in the callback to cancel.
     * @return Array of content that failed verification. Successful if empty.
     */
    external fun verifyInstalledContents(
        callback: (max: Long, progress: Long) -> Boolean
    ): Array<String>

    /**
     * Verifies the contents of a game
     * @param path String path to a game
     * @param callback UI callback for verification progress. Return true in the callback to cancel.
     * @return Int that is meant to be converted to a [GameVerificationResult]
     */
    external fun verifyGameContents(
        path: String,
        callback: (max: Long, progress: Long) -> Boolean
    ): Int

    /**
     * Gets the save location for a specific game
     *
     * @param programId String representation of a game's program ID
     * @return Save data path that may not exist yet
     */
    external fun getSavePath(programId: String): String

    /**
     * Gets the root save directory for the default profile as either
     * /user/save/account/<user id raw string> or /user/save/000...000/<user id>
     *
     * @param future If true, returns the /user/save/account/... directory
     * @return Save data path that may not exist yet
     */
    external fun getDefaultProfileSaveDataRoot(future: Boolean): String

    /**
     * Adds a file to the manual filesystem provider in our EmulationSession instance
     * @param path Path to the file we're adding. Can be a string representation of a [Uri] or
     * a normal path
     */
    external fun addFileToFilesystemProvider(path: String)

    /**
     * Clears all files added to the manual filesystem provider in our EmulationSession instance
     */
    external fun clearFilesystemProvider()

    /**
     * Checks if all necessary keys are present for decryption
     */
    external fun areKeysPresent(): Boolean

    /**
     * Dumps the RomFS from a game to the dump directory
     * @param gamePath Path to the game file
     * @param programId String representation of the game's program ID
     * @param dumpPath Optional custom dump path. If null or empty, uses default dump directory
     * @param callback Progress callback. Return true to cancel. Parameters: (max: Long, progress: Long)
     * @return true if successful, false otherwise
     */
    external fun dumpRomFS(
        gamePath: String,
        programId: String,
        dumpPath: String?,
        callback: (max: Long, progress: Long) -> Boolean
    ): Boolean

    /**
     * Dumps the ExeFS from a game to the dump directory
     * @param gamePath Path to the game file
     * @param programId String representation of the game's program ID
     * @param dumpPath Optional custom dump path. If null or empty, uses default dump directory
     * @param callback Progress callback. Return true to cancel. Parameters: (max: Long, progress: Long)
     * @return true if successful, false otherwise
     */
    external fun dumpExeFS(
        gamePath: String,
        programId: String,
        dumpPath: String?,
        callback: (max: Long, progress: Long) -> Boolean
    ): Boolean
}
