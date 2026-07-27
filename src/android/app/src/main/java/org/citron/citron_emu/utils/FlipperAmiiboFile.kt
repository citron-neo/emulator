// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

/**
 * Converts a Flipper Zero NTAG215 dump to and from the 540-byte image used by VirtualAmiibo.
 *
 * The original text is retained so Flipper-specific metadata (signature, counters, tearing flags,
 * and format version) survives a game write unchanged.
 */
object FlipperAmiiboFile {
    const val PAGE_COUNT = 135
    const val DATA_SIZE = PAGE_COUNT * 4

    data class Parsed(val originalText: String, val data: ByteArray)

    private val fieldRegex = Regex("""^\uFEFF?\s*([^#:][^:]*?)\s*:\s*(.*?)\s*$""")
    private val pageRegex =
        Regex("""(?mi)^[ \t]*Page[ \t]+(\d+)[ \t]*:[ \t]*([^\r\n#]+?)[ \t]*\r?$""")
    private val byteRegex = Regex("""[0-9a-fA-F]{2}""")

    fun parse(text: String): Parsed? {
        val fields = mutableMapOf<String, String>()
        text.lineSequence().forEach { line ->
            val match = fieldRegex.matchEntire(line.removeSuffix("\r")) ?: return@forEach
            fields.putIfAbsent(match.groupValues[1].trim().lowercase(), match.groupValues[2].trim())
        }

        if (!fields["filetype"].equals("Flipper NFC device", ignoreCase = true)) {
            return null
        }
        if (!isNtag215(fields)) {
            return null
        }
        if (fields["pages total"]?.toIntOrNull() != PAGE_COUNT) {
            return null
        }
        val pagesRead = fields["pages read"]?.toIntOrNull()
        if (pagesRead != null && pagesRead < PAGE_COUNT) {
            return null
        }

        val data = ByteArray(DATA_SIZE)
        val seen = BooleanArray(PAGE_COUNT)
        pageRegex.findAll(text).forEach { match ->
            val page = match.groupValues[1].toIntOrNull() ?: return null
            if (page !in 0 until PAGE_COUNT || seen[page]) {
                return null
            }
            val values = match.groupValues[2].trim().split(Regex("""\s+"""))
            if (values.size != 4 || values.any { !byteRegex.matches(it) }) {
                return null
            }
            values.forEachIndexed { index, value ->
                data[page * 4 + index] = value.toInt(16).toByte()
            }
            seen[page] = true
        }

        return if (seen.all { it }) Parsed(text, data) else null
    }

    fun render(template: String, data: ByteArray): String? {
        if (data.size != DATA_SIZE || parse(template) == null) {
            return null
        }

        var rendered = template
        for (page in 0 until PAGE_COUNT) {
            val lineRegex =
                Regex("""(?mi)^([ \t]*Page[ \t]+$page[ \t]*:[ \t]*)[^\r\n]*(\r?)$""")
            if (!lineRegex.containsMatchIn(rendered)) {
                return null
            }
            val pageData = (0 until 4).joinToString(" ") { index ->
                "%02X".format(data[page * 4 + index].toInt() and 0xFF)
            }
            rendered = lineRegex.replace(rendered) { match ->
                match.groupValues[1] + pageData + match.groupValues[2]
            }
        }

        val uidRegex = Regex("""(?mi)^([ \t]*UID[ \t]*:[ \t]*)[^\r\n]*(\r?)$""")
        if (uidRegex.containsMatchIn(rendered)) {
            val uidOffsets = intArrayOf(0, 1, 2, 4, 5, 6, 7)
            val uid = uidOffsets.joinToString(" ") { offset ->
                "%02X".format(data[offset].toInt() and 0xFF)
            }
            rendered = uidRegex.replace(rendered) { match ->
                match.groupValues[1] + uid + match.groupValues[2]
            }
        }
        return rendered
    }

    private fun isNtag215(fields: Map<String, String>): Boolean {
        val deviceType = fields["device type"] ?: return false
        return when {
            deviceType.equals("NTAG/Ultralight", ignoreCase = true) ->
                fields["ntag/ultralight type"].equals("NTAG215", ignoreCase = true)

            deviceType.equals("NTAG215", ignoreCase = true) -> true

            // Older Flipper format revisions used a generic type and inferred the model from size.
            deviceType.equals("Mifare Ultralight", ignoreCase = true) -> true
            else -> false
        }
    }
}
