// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.adapters

import android.text.Html
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.res.ResourcesCompat
import androidx.lifecycle.ViewModelProvider
import com.google.android.material.button.MaterialButton
import org.citron.citron_emu.databinding.PageSetupBinding
import org.citron.citron_emu.model.HomeViewModel
import org.citron.citron_emu.model.SetupCallback
import org.citron.citron_emu.model.SetupPage
import org.citron.citron_emu.model.StepState
import org.citron.citron_emu.utils.ViewUtils
import org.citron.citron_emu.utils.ViewUtils.setVisible
import org.citron.citron_emu.viewholder.AbstractViewHolder

class SetupAdapter(val activity: AppCompatActivity, pages: List<SetupPage>) :
    AbstractListAdapter<SetupPage, SetupAdapter.SetupPageViewHolder>(pages) {
    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): SetupPageViewHolder {
        PageSetupBinding.inflate(LayoutInflater.from(parent.context), parent, false)
            .also { return SetupPageViewHolder(it) }
    }

    inner class SetupPageViewHolder(val binding: PageSetupBinding) :
        AbstractViewHolder<SetupPage>(binding), SetupCallback {
        override fun bind(model: SetupPage) {
            if (model.stepCompleted.invoke() == StepState.COMPLETE) {
                binding.buttonAction.setVisible(visible = false, gone = false)
                binding.textConfirmation.setVisible(true)
            }

            binding.icon.setImageDrawable(
                ResourcesCompat.getDrawable(
                    activity.resources,
                    model.iconId,
                    activity.theme
                )
            )
            binding.textTitle.text = activity.resources.getString(model.titleId)
            binding.textDescription.text =
                Html.fromHtml(activity.resources.getString(model.descriptionId), 0)

            binding.buttonAction.apply {
                text = activity.resources.getString(model.buttonTextId)
                if (model.buttonIconId != 0) {
                    icon = ResourcesCompat.getDrawable(
                        activity.resources,
                        model.buttonIconId,
                        activity.theme
                    )
                }
                iconGravity =
                    if (model.leftAlignedIcon) {
                        MaterialButton.ICON_GRAVITY_START
                    } else {
                        MaterialButton.ICON_GRAVITY_END
                    }
                setOnClickListener {
                    model.buttonAction.invoke(this@SetupPageViewHolder)
                }
            }
        }

        override fun onStepCompleted() {
            ViewUtils.hideView(binding.buttonAction, 200)
            ViewUtils.showView(binding.textConfirmation, 200)
            ViewModelProvider(activity)[HomeViewModel::class.java].setShouldPageForward(true)
        }
    }
}
