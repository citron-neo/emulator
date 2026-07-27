// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

import android.content.Context
import android.content.Intent
import android.net.Uri
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.InputStream
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
        EncryptedKeysRequired,
        InvalidAmiiboKeys,
        Unknown,
    }

    private sealed interface SourceFormat {
        data object Binary : SourceFormat
        data class Flipper(val template: String) : SourceFormat
    }

    private data class ActiveFile(
        val source: Uri,
        val cache: File,
        val format: SourceFormat,
    )

    private val validFileSizes = setOf(0x214L, 0x21CL, 0x23CL, 0x400L)
    private const val MAX_FLIPPER_FILE_SIZE = 1024 * 1024
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

                // Re-reading the same URI before syncing would mount a stale snapshot and later
                // overwrite changes made by the game.
                activeFile?.takeIf { it.source == source }?.let { current ->
                    if (!closeAndSync(context, current)) {
                        return@withLock Result.UnableToWrite
                    }
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
                val format: SourceFormat
                try {
                    val inputStream = context.contentResolver.openInputStream(source)
                        ?: run {
                            cache.delete()
                            return@withLock Result.UnableToRead
                        }
                    if (FileUtil.getExtension(source) == "nfc") {
                        val sourceBytes = inputStream.use {
                            readAtMost(it, MAX_FLIPPER_FILE_SIZE + 1)
                        }
                        if (sourceBytes.size > MAX_FLIPPER_FILE_SIZE) {
                            cache.delete()
                            return@withLock Result.NotAnAmiibo
                        }
                        val text = sourceBytes.toString(Charsets.UTF_8)
                        val parsed = FlipperAmiiboFile.parse(text)
                            ?: run {
                                cache.delete()
                                return@withLock Result.NotAnAmiibo
                            }
                        cache.writeBytes(parsed.data)
                        format = SourceFormat.Flipper(parsed.originalText)
                    } else {
                        inputStream.use { input ->
                            cache.outputStream().use(input::copyTo)
                        }
                        format = SourceFormat.Binary
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
                    if (!closeAndSync(context, current)) {
                        cache.delete()
                        return@withLock Result.UnableToWrite
                    }
                }

                val nativeLoadResult = when (format) {
                    SourceFormat.Binary -> NativeInput.loadAmiiboBinFile(cache.absolutePath)
                    is SourceFormat.Flipper -> NativeInput.loadAmiiboFile(cache.absolutePath)
                }
                val result = nativeResult(nativeLoadResult)
                if (result == Result.Success) {
                    activeFile = ActiveFile(source, cache, format)
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
                if (current != null) {
                    if (!closeAndSync(context, current)) {
                        return@withLock Result.UnableToWrite
                    }
                } else {
                    NativeInput.removeAmiiboFile()
                }

                activeFile = null
                Result.Success
            }
        }

    private fun closeAndSync(context: Context, file: ActiveFile): Boolean {
        NativeInput.removeAmiiboFile()
        if (!copyBack(context, file)) {
            restoreAfterFailedWrite(file)
            return false
        }
        file.cache.delete()
        activeFile = null
        return true
    }

    private fun copyBack(context: Context, file: ActiveFile): Boolean =
        try {
            // Build the complete output before opening the source with a truncating mode.
            val contents = when (val format = file.format) {
                SourceFormat.Binary -> file.cache.readBytes()
                is SourceFormat.Flipper -> {
                    val rendered = FlipperAmiiboFile.render(
                        format.template,
                        file.cache.readBytes()
                    ) ?: return false
                    rendered.toByteArray(Charsets.UTF_8)
                }
            }
            val outputStream = openOutputStream(context, file.source)
            outputStream?.use { output ->
                output.write(contents)
            } != null
        } catch (e: IOException) {
            Log.error("[AmiiboFileSession] Failed to write ${file.source}: ${e.message}")
            false
        } catch (e: SecurityException) {
            Log.error("[AmiiboFileSession] Permission denied while writing ${file.source}")
            false
        } catch (e: IllegalArgumentException) {
            Log.error("[AmiiboFileSession] Provider rejected write mode for ${file.source}")
            false
        }

    private fun openOutputStream(context: Context, source: Uri) = (
        try {
            context.contentResolver.openOutputStream(source, "rwt")
        } catch (_: IOException) {
            null
        } catch (_: IllegalArgumentException) {
            null
        }
    ) ?: context.contentResolver.openOutputStream(source, "wt")

    private fun restoreAfterFailedWrite(file: ActiveFile) {
        val result = nativeResult(NativeInput.loadAmiiboFile(file.cache.absolutePath))
        if (result != Result.Success) {
            Log.warning(
                "[AmiiboFileSession] Amiibo cache was retained but could not be remounted: $result"
            )
        }
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

    private fun readAtMost(input: InputStream, maximumBytes: Int): ByteArray {
        val output = ByteArrayOutputStream(minOf(maximumBytes, 8192))
        val buffer = ByteArray(8192)
        var remaining = maximumBytes
        while (remaining > 0) {
            val count = input.read(buffer, 0, minOf(buffer.size, remaining))
            if (count < 0) {
                break
            }
            output.write(buffer, 0, count)
            remaining -= count
        }
        return output.toByteArray()
    }

    private fun nativeResult(result: Int): Result =
        when (result) {
            NativeInput.AmiiboResult.SUCCESS -> Result.Success
            NativeInput.AmiiboResult.UNABLE_TO_LOAD -> Result.UnableToLoad
            NativeInput.AmiiboResult.NOT_AN_AMIIBO -> Result.NotAnAmiibo
            NativeInput.AmiiboResult.WRONG_DEVICE_STATE -> Result.WrongDeviceState
            NativeInput.AmiiboResult.UNKNOWN -> Result.Unknown
            NativeInput.AmiiboResult.ENCRYPTED_KEYS_REQUIRED -> Result.EncryptedKeysRequired
            NativeInput.AmiiboResult.INVALID_AMIIBO_KEYS -> Result.InvalidAmiiboKeys
            else -> Result.Unknown
        }
}
