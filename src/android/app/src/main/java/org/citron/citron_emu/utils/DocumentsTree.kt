// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import java.io.File
import java.util.*
import org.citron.citron_emu.model.MinimalDocumentFile

class DocumentsTree {
    private var root: DocumentsNode? = null

    fun setRoot(rootUri: Uri?) {
        root = null
        root = DocumentsNode()
        root!!.uri = rootUri
        root!!.isDirectory = true
    }

    fun openContentUri(filepath: String, openMode: String?): Int {
        val node = resolvePath(filepath) ?: return -1
        return FileUtil.openContentUri(node.uri.toString(), openMode)
    }

    fun getFileSize(filepath: String): Long {
        val node = resolvePath(filepath)
        return if (node == null || node.isDirectory) {
            0
        } else {
            FileUtil.getFileSize(node.uri.toString())
        }
    }

    fun exists(filepath: String): Boolean {
        return resolvePath(filepath) != null
    }

    fun isDirectory(filepath: String): Boolean {
        val node = resolvePath(filepath)
        return node != null && node.isDirectory
    }

    fun getParentDirectory(filepath: String): String {
        val node = resolvePath(filepath)!!
        val parentNode = node.parent
        if (parentNode != null && parentNode.isDirectory) {
            return parentNode.uri!!.toString()
        }
        return node.uri!!.toString()
    }

    fun getFilename(filepath: String): String {
        val node = resolvePath(filepath)
        if (node != null) {
            return node.name!!
        }
        return filepath
    }

    private fun resolvePath(filepath: String): DocumentsNode? {
        val tokens = StringTokenizer(filepath, File.separator, false)
        var iterator = root
        while (tokens.hasMoreTokens()) {
            val token = tokens.nextToken()
            if (token.isEmpty()) continue
            iterator = find(iterator, token)
            if (iterator == null) return null
        }
        return iterator
    }

    private fun find(parent: DocumentsNode?, filename: String): DocumentsNode? {
        if (parent!!.isDirectory && !parent.loaded) {
            structTree(parent)
        }
        return parent.children[filename]
    }

    /**
     * Construct current level directory tree
     * @param parent parent node of this level
     */
    private fun structTree(parent: DocumentsNode) {
        val documents = FileUtil.listFiles(parent.uri!!)
        for (document in documents) {
            val node = DocumentsNode(document)
            node.parent = parent
            parent.children[node.name] = node
        }
        parent.loaded = true
    }

    private class DocumentsNode {
        var parent: DocumentsNode? = null
        val children: MutableMap<String?, DocumentsNode> = HashMap()
        var name: String? = null
        var uri: Uri? = null
        var loaded = false
        var isDirectory = false

        constructor()
        constructor(document: MinimalDocumentFile) {
            name = document.filename
            uri = document.uri
            isDirectory = document.isDirectory
            loaded = !isDirectory
        }

        private constructor(document: DocumentFile, isCreateDir: Boolean) {
            name = document.name
            uri = document.uri
            isDirectory = isCreateDir
            loaded = true
        }

        private fun rename(name: String) {
            if (parent == null) {
                return
            }
            parent!!.children.remove(this.name)
            this.name = name
            parent!!.children[name] = this
        }
    }

    companion object {
        fun isNativePath(path: String): Boolean {
            return if (path.isNotEmpty()) {
                path[0] == '/'
            } else {
                false
            }
        }
    }
}
