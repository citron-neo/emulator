// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class FlipperAmiiboFileTest {
    @Test
    fun parsesCurrentNtag215Format() {
        val parsed = FlipperAmiiboFile.parse(createDump())

        assertNotNull(parsed)
        assertEquals(FlipperAmiiboFile.DATA_SIZE, parsed!!.data.size)
        assertEquals(0, parsed.data[0].toInt())
        assertEquals(134, parsed.data[134 * 4].toInt() and 0xFF)
    }

    @Test
    fun parsesLegacyGenericUltralightFormat() {
        val parsed = FlipperAmiiboFile.parse(
            createDump(
                deviceType = "Mifare Ultralight",
                includeSubtype = false
            )
        )

        assertNotNull(parsed)
    }

    @Test
    fun rejectsOtherNtagModelsAndIncompleteDumps() {
        assertNull(FlipperAmiiboFile.parse(createDump(subtype = "NTAG216")))
        assertNull(
            FlipperAmiiboFile.parse(
                createDump().replace(
                    "Page 134: 86 87 88 89\r\n",
                    ""
                )
            )
        )
    }

    @Test
    fun rendersPagesAndUidWithoutChangingMetadataOrLineEndings() {
        val original = createDump()
        val parsed = FlipperAmiiboFile.parse(original)!!
        val changed = parsed.data.copyOf().apply {
            this[0] = 0x04
            this[1] = 0x11
            this[2] = 0x22
            this[4] = 0x33
            this[5] = 0x44
            this[6] = 0x55
            this[7] = 0x66
            this[40] = 0x7F
        }

        val rendered = FlipperAmiiboFile.render(original, changed)!!
        val reparsed = FlipperAmiiboFile.parse(rendered)!!

        assertArrayEquals(changed, reparsed.data)
        assertEquals(true, rendered.contains("UID: 04 11 22 33 44 55 66\r\n"))
        assertEquals(true, rendered.contains("Page 10: 7F 0B 0C 0D\r\n"))
        assertEquals(true, rendered.contains("Signature: KEEP THIS METADATA\r\n"))
        assertEquals(false, rendered.replace("\r\n", "").contains('\n'))
    }

    private fun createDump(
        deviceType: String = "NTAG/Ultralight",
        subtype: String = "NTAG215",
        includeSubtype: Boolean = true,
    ): String =
        buildString {
            append("Filetype: Flipper NFC device\r\n")
            append("Version: 4\r\n")
            append("Device type: $deviceType\r\n")
            append("UID: 00 01 02 04 05 06 07\r\n")
            append("Data format version: 2\r\n")
            if (includeSubtype) {
                append("NTAG/Ultralight type: $subtype\r\n")
            }
            append("Signature: KEEP THIS METADATA\r\n")
            append("Pages total: 135\r\n")
            append("Pages read: 135\r\n")
            repeat(FlipperAmiiboFile.PAGE_COUNT) { page ->
                val first = page and 0xFF
                append(
                    "Page $page: %02X %02X %02X %02X\r\n".format(
                        first,
                        (page + 1) and 0xFF,
                        (page + 2) and 0xFF,
                        (page + 3) and 0xFF
                    )
                )
            }
        }
}
