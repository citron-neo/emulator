// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

import android.content.ContentResolver
import android.net.Uri
import android.provider.DocumentsContract
import org.citron.citron_emu.NativeLibrary

/**
 * Updates and DLC that sit in the same folder as a game are handed to the core when the game
 * starts, so they load without being installed. The core cannot list a storage access framework
 * folder itself, so the files are found here, by the title ID in their names.
 */
object ExternalContentHelper {
    private val TITLE_ID = Regex("\\[([0-9A-Fa-f]{16})]")

    private fun titleIdOf(fileName: String): Long? =
        TITLE_ID.find(fileName)?.groupValues?.get(1)?.toULongOrNull(16)?.toLong()

    /** The application a title ID belongs to: an update is the base plus 0x800, DLC is one band up. */
    private fun applicationOf(id: Long): Long =
        if ((id and 0x1000L) != 0L) (id xor 0x1000L) and 0xFFFL.inv() else id and 0xFFFL.inv()

    private fun isAddon(id: Long): Boolean = (id and 0x1FFFL) != 0L

    /**
     * Looks beside [gamePath] and registers the update and DLC files of that game. When the
     * game's own name carries no title ID, every update and DLC file in the folder is offered
     * and the core picks by the IDs inside the files.
     */
    fun registerSiblings(gamePath: String?) {
        NativeLibrary.clearExternalContentFiles()
        if (gamePath == null) return
        try {
            val uri = Uri.parse(gamePath)
            if (uri.scheme != ContentResolver.SCHEME_CONTENT) return
            val docId = DocumentsContract.getDocumentId(uri)
            val slash = docId.lastIndexOf('/')
            val colon = docId.indexOf(':')
            val parentId = when {
                slash >= 0 -> docId.substring(0, slash)
                colon >= 0 -> docId.substring(0, colon + 1)
                else -> return
            }
            val ownName = docId.substring(maxOf(slash, colon) + 1)
            val own = titleIdOf(ownName)?.let { applicationOf(it) }
            val parent = DocumentsContract.buildDocumentUriUsingTree(uri, parentId)
            var count = 0
            for (file in FileUtil.listFiles(parent)) {
                if (file.isDirectory || !file.filename.endsWith(".nsp", ignoreCase = true)) continue
                val id = titleIdOf(file.filename) ?: continue
                if (!isAddon(id)) continue
                if (own != null && applicationOf(id) != own) continue
                NativeLibrary.addExternalContentFile(file.uri.toString())
                count++
            }
            Log.info("[ExternalContent] $count update and DLC files beside $ownName")
        } catch (e: Exception) {
            Log.warning("[ExternalContent] Could not look beside the game: ${e.message}")
        }
    }
}
