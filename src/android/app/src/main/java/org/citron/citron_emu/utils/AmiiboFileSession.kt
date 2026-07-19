// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

import android.content.Context
import android.content.Intent
import android.net.Uri
import java.io.File
import java.io.IOException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.citron.citron_emu.features.input.NativeInput

/**
 * Owns the writable copy of an Amiibo selected through Android's document picker.
 *
 * The native virtual Amiibo implementation requires a regular filesystem path so game writes can
 * be persisted. Documents exposed through the Storage Access Framework do not necessarily have
 * one, so the selected document is copied to internal storage while it is mounted and copied back
 * before it is removed or replaced.
 */
object AmiiboFileSession {
    enum class Result {
        Success,
        UnableToRead,
        UnableToWrite,
        UnableToLoad,
        NotAnAmiibo,
        WrongDeviceState,
        Unknown,
    }

    private data class ActiveFile(val source: Uri, val cache: File)

    private val validFileSizes = setOf(0x214L, 0x21CL, 0x23CL, 0x400L)
    private val mutex = Mutex()
    private var activeFile: ActiveFile? = null

    suspend fun load(context: Context, source: Uri): Result =
        withContext(Dispatchers.IO) {
            mutex.withLock {
                persistDocumentPermission(context, source)

                try {
                    val descriptor = context.contentResolver.openFileDescriptor(source, "rw")
                    if (descriptor == null) {
                        return@withLock Result.UnableToWrite
                    }
                    descriptor.close()
                } catch (e: IOException) {
                    Log.error("[AmiiboFileSession] Selected document is not writable: ${e.message}")
                    return@withLock Result.UnableToWrite
                } catch (e: SecurityException) {
                    Log.error("[AmiiboFileSession] No write permission for $source")
                    return@withLock Result.UnableToWrite
                }

                val cacheDirectory = File(context.filesDir, "amiibo_runtime")
                if (!cacheDirectory.exists() && !cacheDirectory.mkdirs()) {
                    return@withLock Result.UnableToRead
                }

                val cache = try {
                    File.createTempFile("amiibo_", ".bin", cacheDirectory)
                } catch (e: IOException) {
                    Log.error("[AmiiboFileSession] Failed to create writable copy: ${e.message}")
                    return@withLock Result.UnableToRead
                }
                try {
                    val inputStream = context.contentResolver.openInputStream(source)
                    if (inputStream == null) {
                        cache.delete()
                        return@withLock Result.UnableToRead
                    }
                    inputStream.use { input ->
                        cache.outputStream().use(input::copyTo)
                    }
                } catch (e: IOException) {
                    Log.error("[AmiiboFileSession] Failed to read $source: ${e.message}")
                    cache.delete()
                    return@withLock Result.UnableToRead
                } catch (e: SecurityException) {
                    Log.error("[AmiiboFileSession] Permission denied while reading $source")
                    cache.delete()
                    return@withLock Result.UnableToRead
                }

                if (cache.length() !in validFileSizes) {
                    cache.delete()
                    return@withLock Result.NotAnAmiibo
                }

                activeFile?.let { current ->
                    if (!copyBack(context, current)) {
                        cache.delete()
                        return@withLock Result.UnableToWrite
                    }
                    NativeInput.removeAmiiboFile()
                    current.cache.delete()
                    activeFile = null
                }

                val result = nativeResult(NativeInput.loadAmiiboFile(cache.absolutePath))
                if (result == Result.Success) {
                    activeFile = ActiveFile(source, cache)
                } else {
                    cache.delete()
                }
                result
            }
        }

    suspend fun remove(context: Context): Result =
        withContext(Dispatchers.IO) {
            mutex.withLock {
                val current = activeFile
                if (current != null && !copyBack(context, current)) {
                    return@withLock Result.UnableToWrite
                }

                NativeInput.removeAmiiboFile()
                current?.cache?.delete()
                activeFile = null
                Result.Success
            }
        }

    private fun copyBack(context: Context, file: ActiveFile): Boolean =
        try {
            val outputStream = context.contentResolver.openOutputStream(file.source, "rwt")
                ?: context.contentResolver.openOutputStream(file.source, "wt")
            outputStream?.use { output ->
                file.cache.inputStream().use { input -> input.copyTo(output) }
            } != null
        } catch (e: IOException) {
            Log.error("[AmiiboFileSession] Failed to write ${file.source}: ${e.message}")
            false
        } catch (e: SecurityException) {
            Log.error("[AmiiboFileSession] Permission denied while writing ${file.source}")
            false
        }

    private fun persistDocumentPermission(context: Context, source: Uri) {
        try {
            context.contentResolver.takePersistableUriPermission(
                source,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            )
        } catch (_: SecurityException) {
            // Some providers grant access for the current task but do not support persisted grants.
        }
    }

    private fun nativeResult(result: Int): Result =
        when (result) {
            NativeInput.AmiiboResult.SUCCESS -> Result.Success
            NativeInput.AmiiboResult.UNABLE_TO_LOAD -> Result.UnableToLoad
            NativeInput.AmiiboResult.NOT_AN_AMIIBO -> Result.NotAnAmiibo
            NativeInput.AmiiboResult.WRONG_DEVICE_STATE -> Result.WrongDeviceState
            else -> Result.Unknown
        }
}
