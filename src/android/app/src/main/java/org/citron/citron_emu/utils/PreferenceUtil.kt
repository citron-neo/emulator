// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

import android.content.SharedPreferences

object PreferenceUtil {
    /**
     * Retrieves a shared preference value and then deletes the value in storage.
     * @param key Associated key for the value in this preferences instance
     * @return Typed value associated with [key]. Null if no such key exists.
     */
    inline fun <reified T> SharedPreferences.migratePreference(key: String): T? {
        if (!this.contains(key)) {
            return null
        }

        val value: Any = when (T::class) {
            String::class -> this.getString(key, "")!!

            Boolean::class -> this.getBoolean(key, false)

            Int::class -> this.getInt(key, 0)

            Float::class -> this.getFloat(key, 0f)

            Long::class -> this.getLong(key, 0)

            else -> throw IllegalStateException("Tried to migrate preference with invalid type!")
        }
        deletePreference(key)
        return value as T
    }

    fun SharedPreferences.deletePreference(key: String) = this.edit().remove(key).apply()
}
