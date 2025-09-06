// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.ui

import android.app.Dialog
import android.content.DialogInterface
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.activityViewModels
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.slider.Slider
import org.citron.citron_emu.R
import org.citron.citron_emu.databinding.DialogEditTextBinding
import org.citron.citron_emu.databinding.DialogSliderBinding
import org.citron.citron_emu.features.input.NativeInput
import org.citron.citron_emu.features.input.model.AnalogDirection
import org.citron.citron_emu.features.settings.model.view.AnalogInputSetting
import org.citron.citron_emu.features.settings.model.view.ButtonInputSetting
import org.citron.citron_emu.features.settings.model.view.IntSingleChoiceSetting
import org.citron.citron_emu.features.settings.model.view.SettingsItem
import org.citron.citron_emu.features.settings.model.view.SingleChoiceSetting
import org.citron.citron_emu.features.settings.model.view.SliderSetting
import org.citron.citron_emu.features.settings.model.view.StringInputSetting
import org.citron.citron_emu.features.settings.model.view.StringSingleChoiceSetting
import org.citron.citron_emu.utils.ParamPackage
import org.citron.citron_emu.utils.collect

class SettingsDialogFragment : DialogFragment(), DialogInterface.OnClickListener {
    private var type = 0
    private var position = 0

    private var defaultCancelListener =
        DialogInterface.OnClickListener { _: DialogInterface?, _: Int -> closeDialog() }

    private val settingsViewModel: SettingsViewModel by activityViewModels()

    private lateinit var sliderBinding: DialogSliderBinding
    private lateinit var stringInputBinding: DialogEditTextBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        type = requireArguments().getInt(TYPE)
        position = requireArguments().getInt(POSITION)

        if (settingsViewModel.clickedItem == null) dismiss()
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        return when (type) {
            TYPE_RESET_SETTING -> {
                MaterialAlertDialogBuilder(requireContext())
                    .setMessage(R.string.reset_setting_confirmation)
                    .setPositiveButton(android.R.string.ok) { _: DialogInterface, _: Int ->
                        when (val item = settingsViewModel.clickedItem) {
                            is AnalogInputSetting -> {
                                val stickParam = NativeInput.getStickParam(
                                    item.playerIndex,
                                    item.nativeAnalog
                                )
                                if (stickParam.get("engine", "") == "analog_from_button") {
                                    when (item.analogDirection) {
                                        AnalogDirection.Up -> stickParam.erase("up")
                                        AnalogDirection.Down -> stickParam.erase("down")
                                        AnalogDirection.Left -> stickParam.erase("left")
                                        AnalogDirection.Right -> stickParam.erase("right")
                                    }
                                    NativeInput.setStickParam(
                                        item.playerIndex,
                                        item.nativeAnalog,
                                        stickParam
                                    )
                                    settingsViewModel.setAdapterItemChanged(position)
                                } else {
                                    NativeInput.setStickParam(
                                        item.playerIndex,
                                        item.nativeAnalog,
                                        ParamPackage()
                                    )
                                    settingsViewModel.setDatasetChanged(true)
                                }
                            }

                            is ButtonInputSetting -> {
                                NativeInput.setButtonParam(
                                    item.playerIndex,
                                    item.nativeButton,
                                    ParamPackage()
                                )
                                settingsViewModel.setAdapterItemChanged(position)
                            }

                            else -> {
                                settingsViewModel.clickedItem!!.setting.reset()
                                settingsViewModel.setAdapterItemChanged(position)
                            }
                        }
                    }
                    .setNegativeButton(android.R.string.cancel, null)
                    .create()
            }

            SettingsItem.TYPE_SINGLE_CHOICE -> {
                val item = settingsViewModel.clickedItem as SingleChoiceSetting
                val value = getSelectionForSingleChoiceValue(item)
                MaterialAlertDialogBuilder(requireContext())
                    .setTitle(item.title)
                    .setSingleChoiceItems(item.choicesId, value, this)
                    .create()
            }

            SettingsItem.TYPE_SLIDER -> {
                sliderBinding = DialogSliderBinding.inflate(layoutInflater)
                val item = settingsViewModel.clickedItem as SliderSetting

                settingsViewModel.setSliderTextValue(item.getSelectedValue().toFloat(), item.units)
                sliderBinding.slider.apply {
                    valueFrom = item.min.toFloat()
                    valueTo = item.max.toFloat()
                    value = settingsViewModel.sliderProgress.value.toFloat()
                    addOnChangeListener { _: Slider, value: Float, _: Boolean ->
                        settingsViewModel.setSliderTextValue(value, item.units)
                    }
                }

                MaterialAlertDialogBuilder(requireContext())
                    .setTitle(item.title)
                    .setView(sliderBinding.root)
                    .setPositiveButton(android.R.string.ok, this)
                    .setNegativeButton(android.R.string.cancel, defaultCancelListener)
                    .create()
            }

            SettingsItem.TYPE_STRING_INPUT -> {
                stringInputBinding = DialogEditTextBinding.inflate(layoutInflater)
                val item = settingsViewModel.clickedItem as StringInputSetting
                stringInputBinding.editText.setText(item.getSelectedValue())
                MaterialAlertDialogBuilder(requireContext())
                    .setTitle(item.title)
                    .setView(stringInputBinding.root)
                    .setPositiveButton(android.R.string.ok, this)
                    .setNegativeButton(android.R.string.cancel, defaultCancelListener)
                    .create()
            }

            SettingsItem.TYPE_STRING_SINGLE_CHOICE -> {
                val item = settingsViewModel.clickedItem as StringSingleChoiceSetting
                MaterialAlertDialogBuilder(requireContext())
                    .setTitle(item.title)
                    .setSingleChoiceItems(item.choices, item.selectedValueIndex, this)
                    .create()
            }

            SettingsItem.TYPE_INT_SINGLE_CHOICE -> {
                val item = settingsViewModel.clickedItem as IntSingleChoiceSetting
                MaterialAlertDialogBuilder(requireContext())
                    .setTitle(item.title)
                    .setSingleChoiceItems(item.choices, item.selectedValueIndex, this)
                    .create()
            }

            else -> super.onCreateDialog(savedInstanceState)
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View? {
        return when (type) {
            SettingsItem.TYPE_SLIDER -> sliderBinding.root
            SettingsItem.TYPE_STRING_INPUT -> stringInputBinding.root
            else -> super.onCreateView(inflater, container, savedInstanceState)
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        when (type) {
            SettingsItem.TYPE_SLIDER -> {
                settingsViewModel.sliderTextValue.collect(viewLifecycleOwner) {
                    sliderBinding.textValue.text = it
                }
                settingsViewModel.sliderProgress.collect(viewLifecycleOwner) {
                    sliderBinding.slider.value = it.toFloat()
                }
            }
        }
    }

    override fun onClick(dialog: DialogInterface, which: Int) {
        when (settingsViewModel.clickedItem) {
            is SingleChoiceSetting -> {
                val scSetting = settingsViewModel.clickedItem as SingleChoiceSetting
                val value = getValueForSingleChoiceSelection(scSetting, which)
                scSetting.setSelectedValue(value)
            }

            is StringSingleChoiceSetting -> {
                val scSetting = settingsViewModel.clickedItem as StringSingleChoiceSetting
                val value = scSetting.getValueAt(which)
                scSetting.setSelectedValue(value)
            }

            is IntSingleChoiceSetting -> {
                val scSetting = settingsViewModel.clickedItem as IntSingleChoiceSetting
                val value = scSetting.getValueAt(which)
                scSetting.setSelectedValue(value)
            }

            is SliderSetting -> {
                val sliderSetting = settingsViewModel.clickedItem as SliderSetting
                sliderSetting.setSelectedValue(settingsViewModel.sliderProgress.value)
            }

            is StringInputSetting -> {
                val stringInputSetting = settingsViewModel.clickedItem as StringInputSetting
                stringInputSetting.setSelectedValue(
                    (stringInputBinding.editText.text ?: "").toString()
                )
            }
        }
        closeDialog()
    }

    private fun closeDialog() {
        settingsViewModel.setAdapterItemChanged(position)
        settingsViewModel.clickedItem = null
        settingsViewModel.setSliderProgress(-1f)
        dismiss()
    }

    private fun getValueForSingleChoiceSelection(item: SingleChoiceSetting, which: Int): Int {
        val valuesId = item.valuesId
        return if (valuesId > 0) {
            val valuesArray = requireContext().resources.getIntArray(valuesId)
            valuesArray[which]
        } else {
            which
        }
    }

    private fun getSelectionForSingleChoiceValue(item: SingleChoiceSetting): Int {
        val value = item.getSelectedValue()
        val valuesId = item.valuesId
        if (valuesId > 0) {
            val valuesArray = requireContext().resources.getIntArray(valuesId)
            for (index in valuesArray.indices) {
                val current = valuesArray[index]
                if (current == value) {
                    return index
                }
            }
        } else {
            return value
        }
        return -1
    }

    companion object {
        const val TAG = "SettingsDialogFragment"

        const val TYPE_RESET_SETTING = -1

        const val TITLE = "Title"
        const val TYPE = "Type"
        const val POSITION = "Position"

        fun newInstance(
            settingsViewModel: SettingsViewModel,
            clickedItem: SettingsItem,
            type: Int,
            position: Int
        ): SettingsDialogFragment {
            when (type) {
                SettingsItem.TYPE_HEADER,
                SettingsItem.TYPE_SWITCH,
                SettingsItem.TYPE_SUBMENU,
                SettingsItem.TYPE_DATETIME_SETTING,
                SettingsItem.TYPE_RUNNABLE ->
                    throw IllegalArgumentException("[SettingsDialogFragment] Incompatible type!")

                SettingsItem.TYPE_SLIDER -> settingsViewModel.setSliderProgress(
                    (clickedItem as SliderSetting).getSelectedValue().toFloat()
                )
            }
            settingsViewModel.clickedItem = clickedItem

            val args = Bundle()
            args.putInt(TYPE, type)
            args.putInt(POSITION, position)
            val fragment = SettingsDialogFragment()
            fragment.arguments = args
            return fragment
        }
    }
}
