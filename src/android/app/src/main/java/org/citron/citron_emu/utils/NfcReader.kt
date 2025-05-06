// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.citron.citron_emu.utils

import android.app.Activity
import android.app.PendingIntent
import android.content.Intent
import android.content.IntentFilter
import android.nfc.NfcAdapter
import android.nfc.Tag
import android.nfc.tech.NfcA
import android.os.Build
import android.os.Handler
import android.os.Looper
import java.io.IOException
import org.citron.citron_emu.features.input.NativeInput

class NfcReader(private val activity: Activity) {
    private var nfcAdapter: NfcAdapter? = null
    private var pendingIntent: PendingIntent? = null

    fun initialize() {
        nfcAdapter = NfcAdapter.getDefaultAdapter(activity) ?: return

        pendingIntent = PendingIntent.getActivity(
            activity,
            0,
            Intent(activity, activity.javaClass),
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE
            } else {
                PendingIntent.FLAG_UPDATE_CURRENT
            }
        )

        val tagDetected = IntentFilter(NfcAdapter.ACTION_TAG_DISCOVERED)
        tagDetected.addCategory(Intent.CATEGORY_DEFAULT)
    }

    fun startScanning() {
        nfcAdapter?.enableForegroundDispatch(activity, pendingIntent, null, null)
    }

    fun stopScanning() {
        nfcAdapter?.disableForegroundDispatch(activity)
    }

    fun onNewIntent(intent: Intent) {
        val action = intent.action
        if (NfcAdapter.ACTION_TAG_DISCOVERED != action &&
            NfcAdapter.ACTION_TECH_DISCOVERED != action &&
            NfcAdapter.ACTION_NDEF_DISCOVERED != action
        ) {
            return
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val tag =
                intent.getParcelableExtra(NfcAdapter.EXTRA_TAG, Tag::class.java) ?: return
            readTagData(tag)
            return
        }

        val tag =
            intent.getParcelableExtra<Tag>(NfcAdapter.EXTRA_TAG) ?: return
        readTagData(tag)
    }

    private fun readTagData(tag: Tag) {
        if (!tag.techList.contains("android.nfc.tech.NfcA")) {
            return
        }

        val amiibo = NfcA.get(tag) ?: return
        amiibo.connect()

        val tagData = ntag215ReadAll(amiibo) ?: return
        NativeInput.onReadNfcTag(tagData)

        nfcAdapter?.ignore(
            tag,
            1000,
            { NativeInput.onRemoveNfcTag() },
            Handler(Looper.getMainLooper())
        )
    }

    private fun ntag215ReadAll(amiibo: NfcA): ByteArray? {
        val bufferSize = amiibo.maxTransceiveLength
        val tagSize = 0x21C
        val pageSize = 4
        val lastPage = tagSize / pageSize - 1
        val tagData = ByteArray(tagSize)

        // We need to read the ntag in steps otherwise we overflow the buffer
        for (i in 0..tagSize step bufferSize - 1) {
            val dataStart = i / pageSize
            var dataEnd = (i + bufferSize) / pageSize

            if (dataEnd > lastPage) {
                dataEnd = lastPage
            }

            try {
                val data = ntag215FastRead(amiibo, dataStart, dataEnd - 1)
                System.arraycopy(data, 0, tagData, i, (dataEnd - dataStart) * pageSize)
            } catch (e: IOException) {
                return null
            }
        }
        return tagData
    }

    private fun ntag215Read(amiibo: NfcA, page: Int): ByteArray? {
        return amiibo.transceive(
            byteArrayOf(
                0x30.toByte(),
                (page and 0xFF).toByte()
            )
        )
    }

    private fun ntag215FastRead(amiibo: NfcA, start: Int, end: Int): ByteArray? {
        return amiibo.transceive(
            byteArrayOf(
                0x3A.toByte(),
                (start and 0xFF).toByte(),
                (end and 0xFF).toByte()
            )
        )
    }

    private fun ntag215PWrite(
        amiibo: NfcA,
        page: Int,
        data1: Int,
        data2: Int,
        data3: Int,
        data4: Int
    ): ByteArray? {
        return amiibo.transceive(
            byteArrayOf(
                0xA2.toByte(),
                (page and 0xFF).toByte(),
                (data1 and 0xFF).toByte(),
                (data2 and 0xFF).toByte(),
                (data3 and 0xFF).toByte(),
                (data4 and 0xFF).toByte()
            )
        )
    }

    private fun ntag215PwdAuth(
        amiibo: NfcA,
        data1: Int,
        data2: Int,
        data3: Int,
        data4: Int
    ): ByteArray? {
        return amiibo.transceive(
            byteArrayOf(
                0x1B.toByte(),
                (data1 and 0xFF).toByte(),
                (data2 and 0xFF).toByte(),
                (data3 and 0xFF).toByte(),
                (data4 and 0xFF).toByte()
            )
        )
    }
}
