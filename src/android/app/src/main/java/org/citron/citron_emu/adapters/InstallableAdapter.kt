// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.adapters

import android.view.LayoutInflater
import android.view.ViewGroup
import org.citron.citron_emu.databinding.CardInstallableBinding
import org.citron.citron_emu.model.Installable
import org.citron.citron_emu.utils.ViewUtils.setVisible
import org.citron.citron_emu.viewholder.AbstractViewHolder

class InstallableAdapter(installables: List<Installable>) :
    AbstractListAdapter<Installable, InstallableAdapter.InstallableViewHolder>(installables) {
    override fun onCreateViewHolder(
        parent: ViewGroup,
        viewType: Int
    ): InstallableAdapter.InstallableViewHolder {
        CardInstallableBinding.inflate(LayoutInflater.from(parent.context), parent, false)
            .also { return InstallableViewHolder(it) }
    }

    inner class InstallableViewHolder(val binding: CardInstallableBinding) :
        AbstractViewHolder<Installable>(binding) {
        override fun bind(model: Installable) {
            binding.title.setText(model.titleId)
            binding.description.setText(model.descriptionId)

            binding.buttonInstall.setVisible(model.install != null)
            binding.buttonInstall.setOnClickListener { model.install?.invoke() }
            binding.buttonExport.setVisible(model.export != null)
            binding.buttonExport.setOnClickListener { model.export?.invoke() }
        }
    }
}
