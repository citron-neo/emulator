// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.adapters

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.core.content.res.ResourcesCompat
import androidx.lifecycle.LifecycleOwner
import org.citron.citron_emu.databinding.CardInstallableIconBinding
import org.citron.citron_emu.databinding.CardSimpleOutlinedBinding
import org.citron.citron_emu.model.GameProperty
import org.citron.citron_emu.model.InstallableProperty
import org.citron.citron_emu.model.SubmenuProperty
import org.citron.citron_emu.utils.ViewUtils.marquee
import org.citron.citron_emu.utils.ViewUtils.setVisible
import org.citron.citron_emu.utils.collect
import org.citron.citron_emu.viewholder.AbstractViewHolder

class GamePropertiesAdapter(
    private val viewLifecycle: LifecycleOwner,
    private var properties: List<GameProperty>
) : AbstractListAdapter<GameProperty, AbstractViewHolder<GameProperty>>(properties) {
    override fun onCreateViewHolder(
        parent: ViewGroup,
        viewType: Int
    ): AbstractViewHolder<GameProperty> {
        val inflater = LayoutInflater.from(parent.context)
        return when (viewType) {
            PropertyType.Submenu.ordinal -> {
                SubmenuPropertyViewHolder(
                    CardSimpleOutlinedBinding.inflate(
                        inflater,
                        parent,
                        false
                    )
                )
            }

            else -> InstallablePropertyViewHolder(
                CardInstallableIconBinding.inflate(
                    inflater,
                    parent,
                    false
                )
            )
        }
    }

    override fun getItemViewType(position: Int): Int {
        return when (properties[position]) {
            is SubmenuProperty -> PropertyType.Submenu.ordinal
            else -> PropertyType.Installable.ordinal
        }
    }

    inner class SubmenuPropertyViewHolder(val binding: CardSimpleOutlinedBinding) :
        AbstractViewHolder<GameProperty>(binding) {
        override fun bind(model: GameProperty) {
            val submenuProperty = model as SubmenuProperty

            binding.root.setOnClickListener {
                submenuProperty.action.invoke()
            }

            binding.title.setText(submenuProperty.titleId)
            binding.description.setText(submenuProperty.descriptionId)
            binding.icon.setImageDrawable(
                ResourcesCompat.getDrawable(
                    binding.icon.context.resources,
                    submenuProperty.iconId,
                    binding.icon.context.theme
                )
            )

            binding.details.marquee()
            if (submenuProperty.details != null) {
                binding.details.setVisible(true)
                binding.details.text = submenuProperty.details.invoke()
            } else if (submenuProperty.detailsFlow != null) {
                binding.details.setVisible(true)
                submenuProperty.detailsFlow.collect(viewLifecycle) { binding.details.text = it }
            } else {
                binding.details.setVisible(false)
            }
        }
    }

    inner class InstallablePropertyViewHolder(val binding: CardInstallableIconBinding) :
        AbstractViewHolder<GameProperty>(binding) {
        override fun bind(model: GameProperty) {
            val installableProperty = model as InstallableProperty

            binding.title.setText(installableProperty.titleId)
            binding.description.setText(installableProperty.descriptionId)
            binding.icon.setImageDrawable(
                ResourcesCompat.getDrawable(
                    binding.icon.context.resources,
                    installableProperty.iconId,
                    binding.icon.context.theme
                )
            )

            binding.buttonInstall.setVisible(installableProperty.install != null)
            binding.buttonInstall.setOnClickListener { installableProperty.install?.invoke() }
            binding.buttonExport.setVisible(installableProperty.export != null)
            binding.buttonExport.setOnClickListener { installableProperty.export?.invoke() }
        }
    }

    enum class PropertyType {
        Submenu,
        Installable
    }
}
