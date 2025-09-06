// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.ui.viewholder

import android.view.View
import org.citron.citron_emu.databinding.ListItemSettingBinding
import org.citron.citron_emu.features.settings.model.view.IntSingleChoiceSetting
import org.citron.citron_emu.features.settings.model.view.SettingsItem
import org.citron.citron_emu.features.settings.model.view.SingleChoiceSetting
import org.citron.citron_emu.features.settings.model.view.StringSingleChoiceSetting
import org.citron.citron_emu.features.settings.ui.SettingsAdapter
import org.citron.citron_emu.utils.ViewUtils.setVisible

class SingleChoiceViewHolder(val binding: ListItemSettingBinding, adapter: SettingsAdapter) :
    SettingViewHolder(binding.root, adapter) {
    private lateinit var setting: SettingsItem

    override fun bind(item: SettingsItem) {
        setting = item
        binding.textSettingName.text = setting.title
        binding.textSettingDescription.setVisible(item.description.isNotEmpty())
        binding.textSettingDescription.text = item.description

        binding.textSettingValue.setVisible(true)
        when (item) {
            is SingleChoiceSetting -> {
                val resMgr = binding.textSettingValue.context.resources
                val values = resMgr.getIntArray(item.valuesId)
                for (i in values.indices) {
                    if (values[i] == item.getSelectedValue()) {
                        binding.textSettingValue.text = resMgr.getStringArray(item.choicesId)[i]
                        break
                    }
                }
            }

            is StringSingleChoiceSetting -> {
                binding.textSettingValue.text = item.getSelectedValue()
            }

            is IntSingleChoiceSetting -> {
                binding.textSettingValue.text = item.getChoiceAt(item.getSelectedValue())
            }
        }
        if (binding.textSettingValue.text.isEmpty()) {
            binding.textSettingValue.setVisible(false)
        }

        binding.buttonClear.setVisible(setting.clearable)
        binding.buttonClear.setOnClickListener {
            adapter.onClearClick(setting, bindingAdapterPosition)
        }

        setStyle(setting.isEditable, binding)
    }

    override fun onClick(clicked: View) {
        if (!setting.isEditable) {
            return
        }

        when (setting) {
            is SingleChoiceSetting -> adapter.onSingleChoiceClick(
                setting as SingleChoiceSetting,
                bindingAdapterPosition
            )

            is StringSingleChoiceSetting -> {
                adapter.onStringSingleChoiceClick(
                    setting as StringSingleChoiceSetting,
                    bindingAdapterPosition
                )
            }

            is IntSingleChoiceSetting -> {
                adapter.onIntSingleChoiceClick(
                    setting as IntSingleChoiceSetting,
                    bindingAdapterPosition
                )
            }
        }
    }

    override fun onLongClick(clicked: View): Boolean {
        if (setting.isEditable) {
            return adapter.onLongClick(setting, bindingAdapterPosition)
        }
        return false
    }
}
