// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

import android.content.Context
import android.net.Uri
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.citron.citron_emu.features.input.NativeInput

object AmiiboKeyManager {
    enum class Result {
        Success,
        InvalidExtension,
        InvalidKey,
        UnableToRead,
        UnableToWrite,
    }

    private const val KEY_FILE_SIZE = 0xA0

    suspend fun install(context: Context, source: Uri): Result =
        withContext(Dispatchers.IO) {
            if (FileUtil.getExtension(source) != "bin") {
                return@withContext Result.InvalidExtension
            }

            val keyData = try {
                context.contentResolver.openInputStream(source)?.use { input ->
                    val data = readAtMost(input, KEY_FILE_SIZE + 1)
                    if (data.size != KEY_FILE_SIZE) {
                        return@withContext Result.InvalidKey
                    }
                    data
                } ?: return@withContext Result.UnableToRead
            } catch (e: IOException) {
                Log.error("[AmiiboKeyManager] Failed to read $source: ${e.message}")
                return@withContext Result.UnableToRead
            } catch (e: SecurityException) {
                Log.error(
                    "[AmiiboKeyManager] Permission denied while reading $source: ${e.message}"
                )
                return@withContext Result.UnableToRead
            }

            if (!NativeInput.validateAmiiboKey(keyData)) {
                return@withContext Result.InvalidKey
            }

            val keysDirectory = File(DirectoryInitialization.userDirectory, "keys")
            if (!keysDirectory.exists() && !keysDirectory.mkdirs()) {
                return@withContext Result.UnableToWrite
            }

            var temporary: File? = null
            try {
                val created = File.createTempFile("key_retail_", ".tmp", keysDirectory)
                temporary = created
                created.writeBytes(keyData)
            } catch (e: IOException) {
                Log.error("[AmiiboKeyManager] Failed to stage key file: ${e.message}")
                temporary?.delete()
                return@withContext Result.UnableToWrite
            } catch (e: SecurityException) {
                Log.error(
                    "[AmiiboKeyManager] Permission denied while staging key file: ${e.message}"
                )
                temporary?.delete()
                return@withContext Result.UnableToWrite
            }
            val stagedFile = temporary ?: return@withContext Result.UnableToWrite

            try {
                Files.move(
                    stagedFile.toPath(),
                    File(keysDirectory, "key_retail.bin").toPath(),
                    StandardCopyOption.REPLACE_EXISTING
                )
                Result.Success
            } catch (e: IOException) {
                Log.error("[AmiiboKeyManager] Failed to install key file: ${e.message}")
                stagedFile.delete()
                Result.UnableToWrite
            } catch (e: SecurityException) {
                Log.error(
                    "[AmiiboKeyManager] Permission denied while installing key file: ${e.message}"
                )
                stagedFile.delete()
                Result.UnableToWrite
            }
        }

    private fun readAtMost(input: InputStream, maximumBytes: Int): ByteArray {
        val output = ByteArrayOutputStream(maximumBytes)
        val buffer = ByteArray(maximumBytes)
        var remaining = maximumBytes
        while (remaining > 0) {
            val count = input.read(buffer, 0, minOf(buffer.size, remaining))
            if (count <= 0) {
                break
            }
            output.write(buffer, 0, count)
            remaining -= count
        }
        return output.toByteArray()
    }
}
