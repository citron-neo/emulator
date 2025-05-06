// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

// Kotlin version of src/common/param_package.h
class ParamPackage(serialized: String = "") {
    private val KEY_VALUE_SEPARATOR = ":"
    private val PARAM_SEPARATOR = ","

    private val ESCAPE_CHARACTER = "$"
    private val KEY_VALUE_SEPARATOR_ESCAPE = "$0"
    private val PARAM_SEPARATOR_ESCAPE = "$1"
    private val ESCAPE_CHARACTER_ESCAPE = "$2"

    private val EMPTY_PLACEHOLDER = "[empty]"

    val data = mutableMapOf<String, String>()

    init {
        val pairs = serialized.split(PARAM_SEPARATOR)
        for (pair in pairs) {
            val keyValue = pair.split(KEY_VALUE_SEPARATOR).toMutableList()
            if (keyValue.size != 2) {
                Log.error("[ParamPackage] Invalid key pair $keyValue")
                continue
            }

            keyValue.forEachIndexed { i: Int, _: String ->
                keyValue[i] = keyValue[i].replace(KEY_VALUE_SEPARATOR_ESCAPE, KEY_VALUE_SEPARATOR)
                keyValue[i] = keyValue[i].replace(PARAM_SEPARATOR_ESCAPE, PARAM_SEPARATOR)
                keyValue[i] = keyValue[i].replace(ESCAPE_CHARACTER_ESCAPE, ESCAPE_CHARACTER)
            }

            set(keyValue[0], keyValue[1])
        }
    }

    constructor(params: List<Pair<String, String>>) : this() {
        params.forEach {
            data[it.first] = it.second
        }
    }

    fun serialize(): String {
        if (data.isEmpty()) {
            return EMPTY_PLACEHOLDER
        }

        val result = StringBuilder()
        data.forEach {
            val keyValue = mutableListOf(it.key, it.value)
            keyValue.forEachIndexed { i, _ ->
                keyValue[i] = keyValue[i].replace(ESCAPE_CHARACTER, ESCAPE_CHARACTER_ESCAPE)
                keyValue[i] = keyValue[i].replace(PARAM_SEPARATOR, PARAM_SEPARATOR_ESCAPE)
                keyValue[i] = keyValue[i].replace(KEY_VALUE_SEPARATOR, KEY_VALUE_SEPARATOR_ESCAPE)
            }
            result.append("${keyValue[0]}$KEY_VALUE_SEPARATOR${keyValue[1]}$PARAM_SEPARATOR")
        }
        return result.removeSuffix(PARAM_SEPARATOR).toString()
    }

    fun get(key: String, defaultValue: String): String =
        if (has(key)) {
            data[key]!!
        } else {
            Log.debug("[ParamPackage] key $key not found")
            defaultValue
        }

    fun get(key: String, defaultValue: Int): Int =
        if (has(key)) {
            try {
                data[key]!!.toInt()
            } catch (e: NumberFormatException) {
                Log.debug("[ParamPackage] failed to convert ${data[key]!!} to int")
                defaultValue
            }
        } else {
            Log.debug("[ParamPackage] key $key not found")
            defaultValue
        }

    private fun Int.toBoolean(): Boolean =
        if (this == 1) {
            true
        } else if (this == 0) {
            false
        } else {
            throw Exception("Tried to convert a value to a boolean that was not 0 or 1!")
        }

    fun get(key: String, defaultValue: Boolean): Boolean =
        if (has(key)) {
            try {
                get(key, if (defaultValue) 1 else 0).toBoolean()
            } catch (e: Exception) {
                Log.debug("[ParamPackage] failed to convert ${data[key]!!} to boolean")
                defaultValue
            }
        } else {
            Log.debug("[ParamPackage] key $key not found")
            defaultValue
        }

    fun get(key: String, defaultValue: Float): Float =
        if (has(key)) {
            try {
                data[key]!!.toFloat()
            } catch (e: NumberFormatException) {
                Log.debug("[ParamPackage] failed to convert ${data[key]!!} to float")
                defaultValue
            }
        } else {
            Log.debug("[ParamPackage] key $key not found")
            defaultValue
        }

    fun set(key: String, value: String) {
        data[key] = value
    }

    fun set(key: String, value: Int) {
        data[key] = value.toString()
    }

    fun Boolean.toInt(): Int = if (this) 1 else 0
    fun set(key: String, value: Boolean) {
        data[key] = value.toInt().toString()
    }

    fun set(key: String, value: Float) {
        data[key] = value.toString()
    }

    fun has(key: String): Boolean = data.containsKey(key)

    fun erase(key: String) = data.remove(key)

    fun clear() = data.clear()
}
