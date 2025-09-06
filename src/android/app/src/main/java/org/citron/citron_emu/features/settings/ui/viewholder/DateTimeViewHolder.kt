// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.ui.viewholder

import android.view.View
import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import org.citron.citron_emu.databinding.ListItemSettingBinding
import org.citron.citron_emu.features.settings.model.view.DateTimeSetting
import org.citron.citron_emu.features.settings.model.view.SettingsItem
import org.citron.citron_emu.features.settings.ui.SettingsAdapter
import org.citron.citron_emu.utils.ViewUtils.setVisible

class DateTimeViewHolder(val binding: ListItemSettingBinding, adapter: SettingsAdapter) :
    SettingViewHolder(binding.root, adapter) {
    private lateinit var setting: DateTimeSetting

    override fun bind(item: SettingsItem) {
        setting = item as DateTimeSetting
        binding.textSettingName.text = item.title
        binding.textSettingDescription.setVisible(item.description.isNotEmpty())
        binding.textSettingDescription.text = item.description
        binding.textSettingValue.setVisible(true)
        val epochTime = setting.getValue()
        val instant = Instant.ofEpochMilli(epochTime * 1000)
        val zonedTime = ZonedDateTime.ofInstant(instant, ZoneId.of("UTC"))
        val dateFormatter = DateTimeFormatter.ofLocalizedDateTime(FormatStyle.MEDIUM)
        binding.textSettingValue.text = dateFormatter.format(zonedTime)

        binding.buttonClear.setVisible(setting.clearable)
        binding.buttonClear.setOnClickListener {
            adapter.onClearClick(setting, bindingAdapterPosition)
        }

        setStyle(setting.isEditable, binding)
    }

    override fun onClick(clicked: View) {
        if (setting.isEditable) {
            adapter.onDateTimeClick(setting, bindingAdapterPosition)
        }
    }

    override fun onLongClick(clicked: View): Boolean {
        if (setting.isEditable) {
            return adapter.onLongClick(setting, bindingAdapterPosition)
        }
        return false
    }
}
