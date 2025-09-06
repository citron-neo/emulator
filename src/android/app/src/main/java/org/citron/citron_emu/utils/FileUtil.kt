// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

import android.database.Cursor
import android.net.Uri
import android.provider.DocumentsContract
import androidx.documentfile.provider.DocumentFile
import java.io.BufferedInputStream
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.net.URLDecoder
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import org.citron.citron_emu.CitronApplication
import org.citron.citron_emu.model.MinimalDocumentFile
import org.citron.citron_emu.model.TaskState
import java.io.BufferedOutputStream
import java.io.OutputStream
import java.lang.NullPointerException
import java.nio.charset.StandardCharsets
import java.util.zip.Deflater
import java.util.zip.ZipOutputStream
import kotlin.IllegalStateException

object FileUtil {
    const val PATH_TREE = "tree"
    const val DECODE_METHOD = "UTF-8"
    const val APPLICATION_OCTET_STREAM = "application/octet-stream"
    const val TEXT_PLAIN = "text/plain"

    private val context get() = CitronApplication.appContext

    /**
     * Create a file from directory with filename.
     * @param context Application context
     * @param directory parent path for file.
     * @param filename file display name.
     * @return boolean
     */
    fun createFile(directory: String?, filename: String): DocumentFile? {
        var decodedFilename = filename
        try {
            val directoryUri = Uri.parse(directory)
            val parent = DocumentFile.fromTreeUri(context, directoryUri) ?: return null
            decodedFilename = URLDecoder.decode(decodedFilename, DECODE_METHOD)
            var mimeType = APPLICATION_OCTET_STREAM
            if (decodedFilename.endsWith(".txt")) {
                mimeType = TEXT_PLAIN
            }
            val exists = parent.findFile(decodedFilename)
            return exists ?: parent.createFile(mimeType, decodedFilename)
        } catch (e: Exception) {
            Log.error("[FileUtil]: Cannot create file, error: " + e.message)
        }
        return null
    }

    /**
     * Create a directory from directory with filename.
     * @param directory parent path for directory.
     * @param directoryName directory display name.
     * @return boolean
     */
    fun createDir(directory: String?, directoryName: String?): DocumentFile? {
        var decodedDirectoryName = directoryName
        try {
            val directoryUri = Uri.parse(directory)
            val parent = DocumentFile.fromTreeUri(context, directoryUri) ?: return null
            decodedDirectoryName = URLDecoder.decode(decodedDirectoryName, DECODE_METHOD)
            val isExist = parent.findFile(decodedDirectoryName)
            return isExist ?: parent.createDirectory(decodedDirectoryName)
        } catch (e: Exception) {
            Log.error("[FileUtil]: Cannot create file, error: " + e.message)
        }
        return null
    }

    /**
     * Open content uri and return file descriptor to JNI.
     * @param path Native content uri path
     * @param openMode will be one of "r", "r", "rw", "wa", "rwa"
     * @return file descriptor
     */
    @JvmStatic
    fun openContentUri(path: String, openMode: String?): Int {
        try {
            val uri = Uri.parse(path)
            val parcelFileDescriptor = context.contentResolver.openFileDescriptor(uri, openMode!!)
            if (parcelFileDescriptor == null) {
                Log.error("[FileUtil]: Cannot get the file descriptor from uri: $path")
                return -1
            }
            val fileDescriptor = parcelFileDescriptor.detachFd()
            parcelFileDescriptor.close()
            return fileDescriptor
        } catch (e: Exception) {
            Log.error("[FileUtil]: Cannot open content uri, error: " + e.message)
        }
        return -1
    }

    /**
     * Reference:  https://stackoverflow.com/questions/42186820/documentfile-is-very-slow
     * This function will be faster than DocumentFile.listFiles
     * @param uri Directory uri.
     * @return CheapDocument lists.
     */
    fun listFiles(uri: Uri): Array<MinimalDocumentFile> {
        val resolver = context.contentResolver
        val columns = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE
        )
        var c: Cursor? = null
        val results: MutableList<MinimalDocumentFile> = ArrayList()
        try {
            val docId: String = if (isRootTreeUri(uri)) {
                DocumentsContract.getTreeDocumentId(uri)
            } else {
                DocumentsContract.getDocumentId(uri)
            }
            val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(uri, docId)
            c = resolver.query(childrenUri, columns, null, null, null)
            while (c!!.moveToNext()) {
                val documentId = c.getString(0)
                val documentName = c.getString(1)
                val documentMimeType = c.getString(2)
                val documentUri = DocumentsContract.buildDocumentUriUsingTree(uri, documentId)
                val document = MinimalDocumentFile(documentName, documentMimeType, documentUri)
                results.add(document)
            }
        } catch (e: Exception) {
            Log.error("[FileUtil]: Cannot list file error: " + e.message)
        } finally {
            closeQuietly(c)
        }
        return results.toTypedArray()
    }

    /**
     * Check whether given path exists.
     * @param path Native content uri path
     * @return bool
     */
    fun exists(path: String?, suppressLog: Boolean = false): Boolean {
        var c: Cursor? = null
        try {
            val mUri = Uri.parse(path)
            val columns = arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID)
            c = context.contentResolver.query(mUri, columns, null, null, null)
            return c!!.count > 0
        } catch (e: Exception) {
            if (!suppressLog) {
                Log.info("[FileUtil] Cannot find file from given path, error: " + e.message)
            }
        } finally {
            closeQuietly(c)
        }
        return false
    }

    /**
     * Check whether given path is a directory
     * @param path content uri path
     * @return bool
     */
    fun isDirectory(path: String): Boolean {
        val resolver = context.contentResolver
        val columns = arrayOf(
            DocumentsContract.Document.COLUMN_MIME_TYPE
        )
        var isDirectory = false
        var c: Cursor? = null
        try {
            val mUri = Uri.parse(path)
            c = resolver.query(mUri, columns, null, null, null)
            c!!.moveToNext()
            val mimeType = c.getString(0)
            isDirectory = mimeType == DocumentsContract.Document.MIME_TYPE_DIR
        } catch (e: Exception) {
            Log.error("[FileUtil]: Cannot list files, error: " + e.message)
        } finally {
            closeQuietly(c)
        }
        return isDirectory
    }

    /**
     * Get file display name from given path
     * @param uri content uri
     * @return String display name
     */
    fun getFilename(uri: Uri): String {
        val resolver = CitronApplication.appContext.contentResolver
        val columns = arrayOf(
            DocumentsContract.Document.COLUMN_DISPLAY_NAME
        )
        var filename = ""
        var c: Cursor? = null
        try {
            c = resolver.query(uri, columns, null, null, null)
            c!!.moveToNext()
            filename = c.getString(0)
        } catch (e: Exception) {
            Log.error("[FileUtil]: Cannot get file size, error: " + e.message)
        } finally {
            closeQuietly(c)
        }
        return filename
    }

    fun getFilesName(path: String): Array<String> {
        val uri = Uri.parse(path)
        val files: MutableList<String> = ArrayList()
        for (file in listFiles(uri)) {
            files.add(file.filename)
        }
        return files.toTypedArray()
    }

    /**
     * Get file size from given path.
     * @param path content uri path
     * @return long file size
     */
    @JvmStatic
    fun getFileSize(path: String): Long {
        val resolver = context.contentResolver
        val columns = arrayOf(
            DocumentsContract.Document.COLUMN_SIZE
        )
        var size: Long = 0
        var c: Cursor? = null
        try {
            val mUri = Uri.parse(path)
            c = resolver.query(mUri, columns, null, null, null)
            c!!.moveToNext()
            size = c.getLong(0)
        } catch (e: Exception) {
            Log.error("[FileUtil]: Cannot get file size, error: " + e.message)
        } finally {
            closeQuietly(c)
        }
        return size
    }

    /**
     * Creates an input stream with a given [Uri] and copies its data to the given path. This will
     * overwrite any pre-existing files.
     *
     * @param sourceUri The [Uri] to copy data from
     * @param destinationParentPath Destination directory
     * @param destinationFilename Optionally renames the file once copied
     */
    fun copyUriToInternalStorage(
        sourceUri: Uri,
        destinationParentPath: String,
        destinationFilename: String = ""
    ): File? =
        try {
            val fileName =
                if (destinationFilename == "") getFilename(sourceUri) else "/$destinationFilename"
            val inputStream = context.contentResolver.openInputStream(sourceUri)!!

            val destinationFile = File("$destinationParentPath$fileName")
            if (destinationFile.exists()) {
                destinationFile.delete()
            }

            destinationFile.outputStream().use { fos ->
                inputStream.use { it.copyTo(fos) }
            }
            destinationFile
        } catch (e: IOException) {
            null
        } catch (e: NullPointerException) {
            null
        }

    /**
     * Extracts the given zip file into the given directory.
     * @param path String representation of a [Uri] or a typical path delimited by '/'
     * @param destDir Location to unzip the contents of [path] into
     * @param progressCallback Lambda that is called with the total number of files and the current
     * progress through the process. Stops execution as soon as possible if this returns true.
     */
    @Throws(SecurityException::class)
    fun unzipToInternalStorage(
        path: String,
        destDir: File,
        progressCallback: (max: Long, progress: Long) -> Boolean = { _, _ -> false }
    ) {
        var totalEntries = 0L
        ZipInputStream(getInputStream(path)).use { zis ->
            var tempEntry = zis.nextEntry
            while (tempEntry != null) {
                tempEntry = zis.nextEntry
                totalEntries++
            }
        }

        var progress = 0L
        ZipInputStream(getInputStream(path)).use { zis ->
            var entry: ZipEntry? = zis.nextEntry
            while (entry != null) {
                if (progressCallback.invoke(totalEntries, progress)) {
                    return@use
                }

                val newFile = File(destDir, entry.name)
                val destinationDirectory = if (entry.isDirectory) newFile else newFile.parentFile

                if (!newFile.canonicalPath.startsWith(destDir.canonicalPath + File.separator)) {
                    throw SecurityException("Zip file attempted path traversal! ${entry.name}")
                }

                if (!destinationDirectory.isDirectory && !destinationDirectory.mkdirs()) {
                    throw IOException("Failed to create directory $destinationDirectory")
                }

                if (!entry.isDirectory) {
                    newFile.outputStream().use { fos -> zis.copyTo(fos) }
                }
                entry = zis.nextEntry
                progress++
            }
        }
    }

    /**
     * Creates a zip file from a directory within internal storage
     * @param inputFile File representation of the item that will be zipped
     * @param rootDir Directory containing the inputFile
     * @param outputStream Stream where the zip file will be output
     * @param progressCallback Lambda that is called with the total number of files and the current
     * progress through the process. Stops execution as soon as possible if this returns true.
     * @param compression Disables compression if true
     */
    fun zipFromInternalStorage(
        inputFile: File,
        rootDir: String,
        outputStream: BufferedOutputStream,
        progressCallback: (max: Long, progress: Long) -> Boolean = { _, _ -> false },
        compression: Boolean = true
    ): TaskState {
        try {
            ZipOutputStream(outputStream).use { zos ->
                if (!compression) {
                    zos.setMethod(ZipOutputStream.DEFLATED)
                    zos.setLevel(Deflater.NO_COMPRESSION)
                }

                var count = 0L
                val totalFiles = inputFile.walkTopDown().count().toLong()
                inputFile.walkTopDown().forEach { file ->
                    if (progressCallback.invoke(totalFiles, count)) {
                        return TaskState.Cancelled
                    }

                    if (!file.isDirectory) {
                        val entryName =
                            file.absolutePath.removePrefix(rootDir).removePrefix("/")
                        val entry = ZipEntry(entryName)
                        zos.putNextEntry(entry)
                        if (file.isFile) {
                            file.inputStream().use { fis -> fis.copyTo(zos) }
                        }
                        count++
                    }
                }
            }
        } catch (e: Exception) {
            Log.error("[FileUtil] Failed creating zip file - ${e.message}")
            return TaskState.Failed
        }
        return TaskState.Completed
    }

    /**
     * Helper function that copies the contents of a DocumentFile folder into a [File]
     * @param file [File] representation of the folder to copy into
     * @param progressCallback Lambda that is called with the total number of files and the current
     * progress through the process. Stops execution as soon as possible if this returns true.
     * @throws IllegalStateException Fails when trying to copy a folder into a file and vice versa
     */
    fun DocumentFile.copyFilesTo(
        file: File,
        progressCallback: (max: Long, progress: Long) -> Boolean = { _, _ -> false }
    ) {
        file.mkdirs()
        if (!this.isDirectory || !file.isDirectory) {
            throw IllegalStateException(
                "[FileUtil] Tried to copy a folder into a file or vice versa"
            )
        }

        var count = 0L
        val totalFiles = this.listFiles().size.toLong()
        this.listFiles().forEach {
            if (progressCallback.invoke(totalFiles, count)) {
                return
            }

            val newFile = File(file, it.name!!)
            if (it.isDirectory) {
                newFile.mkdirs()
                DocumentFile.fromTreeUri(CitronApplication.appContext, it.uri)?.copyFilesTo(newFile)
            } else {
                val inputStream =
                    CitronApplication.appContext.contentResolver.openInputStream(it.uri)
                BufferedInputStream(inputStream).use { bos ->
                    if (!newFile.exists()) {
                        newFile.createNewFile()
                    }
                    newFile.outputStream().use { os -> bos.copyTo(os) }
                }
            }
            count++
        }
    }

    fun isRootTreeUri(uri: Uri): Boolean {
        val paths = uri.pathSegments
        return paths.size == 2 && PATH_TREE == paths[0]
    }

    fun closeQuietly(closeable: AutoCloseable?) {
        if (closeable != null) {
            try {
                closeable.close()
            } catch (rethrown: RuntimeException) {
                throw rethrown
            } catch (ignored: Exception) {
            }
        }
    }

    fun getExtension(uri: Uri): String {
        val fileName = getFilename(uri)
        return fileName.substring(fileName.lastIndexOf(".") + 1)
            .lowercase()
    }

    fun isTreeUriValid(uri: Uri): Boolean {
        val resolver = context.contentResolver
        val columns = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE
        )
        return try {
            val docId: String = if (isRootTreeUri(uri)) {
                DocumentsContract.getTreeDocumentId(uri)
            } else {
                DocumentsContract.getDocumentId(uri)
            }
            val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(uri, docId)
            resolver.query(childrenUri, columns, null, null, null)
            true
        } catch (_: Exception) {
            false
        }
    }

    fun getInputStream(path: String) = if (path.contains("content://")) {
        Uri.parse(path).inputStream()
    } else {
        File(path).inputStream()
    }

    fun getOutputStream(path: String) = if (path.contains("content://")) {
        Uri.parse(path).outputStream()
    } else {
        File(path).outputStream()
    }

    @Throws(IOException::class)
    fun getStringFromFile(file: File): String =
        String(file.readBytes(), StandardCharsets.UTF_8)

    @Throws(IOException::class)
    fun getStringFromInputStream(stream: InputStream): String =
        String(stream.readBytes(), StandardCharsets.UTF_8)

    fun DocumentFile.inputStream(): InputStream =
        CitronApplication.appContext.contentResolver.openInputStream(uri)!!

    fun DocumentFile.outputStream(): OutputStream =
        CitronApplication.appContext.contentResolver.openOutputStream(uri)!!

    fun Uri.inputStream(): InputStream =
        CitronApplication.appContext.contentResolver.openInputStream(this)!!

    fun Uri.outputStream(): OutputStream =
        CitronApplication.appContext.contentResolver.openOutputStream(this)!!

    fun Uri.asDocumentFile(): DocumentFile? =
        DocumentFile.fromSingleUri(CitronApplication.appContext, this)
}
