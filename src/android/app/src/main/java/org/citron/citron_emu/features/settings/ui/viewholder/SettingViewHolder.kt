// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.ui.viewholder

import android.view.View
import androidx.recyclerview.widget.RecyclerView
import org.citron.citron_emu.databinding.ListItemSettingBinding
import org.citron.citron_emu.databinding.ListItemSettingSwitchBinding
import org.citron.citron_emu.features.settings.model.view.SettingsItem
import org.citron.citron_emu.features.settings.ui.SettingsAdapter

abstract class SettingViewHolder(itemView: View, protected val adapter: SettingsAdapter) :
    RecyclerView.ViewHolder(itemView), View.OnClickListener, View.OnLongClickListener {

    init {
        itemView.setOnClickListener(this)
        itemView.setOnLongClickListener(this)
    }

    /**
     * Called by the adapter to set this ViewHolder's child views to display the list item
     * it must now represent.
     *
     * @param item The list item that should be represented by this ViewHolder.
     */
    abstract fun bind(item: SettingsItem)

    /**
     * Called when this ViewHolder's view is clicked on. Implementations should usually pass
     * this event up to the adapter.
     *
     * @param clicked The view that was clicked on.
     */
    abstract override fun onClick(clicked: View)

    abstract override fun onLongClick(clicked: View): Boolean

    fun setStyle(isEditable: Boolean, binding: ListItemSettingBinding) {
        val opacity = if (isEditable) 1.0f else 0.5f
        binding.textSettingName.alpha = opacity
        binding.textSettingDescription.alpha = opacity
        binding.textSettingValue.alpha = opacity
        binding.buttonClear.isEnabled = isEditable
    }

    fun setStyle(isEditable: Boolean, binding: ListItemSettingSwitchBinding) {
        binding.switchWidget.isEnabled = isEditable
        val opacity = if (isEditable) 1.0f else 0.5f
        binding.textSettingName.alpha = opacity
        binding.textSettingDescription.alpha = opacity
        binding.buttonClear.isEnabled = isEditable
    }
}
