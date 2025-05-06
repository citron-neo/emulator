// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.ui

import android.content.Context
import android.icu.util.Calendar
import android.icu.util.TimeZone
import android.text.format.DateFormat
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.PopupMenu
import androidx.fragment.app.Fragment
import androidx.lifecycle.ViewModelProvider
import androidx.navigation.findNavController
import androidx.recyclerview.widget.AsyncDifferConfig
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import com.google.android.material.datepicker.MaterialDatePicker
import com.google.android.material.timepicker.MaterialTimePicker
import com.google.android.material.timepicker.TimeFormat
import org.citron.citron_emu.R
import org.citron.citron_emu.SettingsNavigationDirections
import org.citron.citron_emu.databinding.ListItemSettingBinding
import org.citron.citron_emu.databinding.ListItemSettingInputBinding
import org.citron.citron_emu.databinding.ListItemSettingSwitchBinding
import org.citron.citron_emu.databinding.ListItemSettingsHeaderBinding
import org.citron.citron_emu.features.input.NativeInput
import org.citron.citron_emu.features.input.model.AnalogDirection
import org.citron.citron_emu.features.settings.model.AbstractIntSetting
import org.citron.citron_emu.features.settings.model.view.*
import org.citron.citron_emu.features.settings.ui.viewholder.*
import org.citron.citron_emu.utils.ParamPackage

class SettingsAdapter(
    private val fragment: Fragment,
    private val context: Context
) : ListAdapter<SettingsItem, SettingViewHolder>(
    AsyncDifferConfig.Builder(DiffCallback()).build()
) {
    private val settingsViewModel: SettingsViewModel
        get() = ViewModelProvider(fragment.requireActivity())[SettingsViewModel::class.java]

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): SettingViewHolder {
        val inflater = LayoutInflater.from(parent.context)
        return when (viewType) {
            SettingsItem.TYPE_HEADER -> {
                HeaderViewHolder(ListItemSettingsHeaderBinding.inflate(inflater), this)
            }

            SettingsItem.TYPE_SWITCH -> {
                SwitchSettingViewHolder(ListItemSettingSwitchBinding.inflate(inflater), this)
            }

            SettingsItem.TYPE_SINGLE_CHOICE, SettingsItem.TYPE_STRING_SINGLE_CHOICE -> {
                SingleChoiceViewHolder(ListItemSettingBinding.inflate(inflater), this)
            }

            SettingsItem.TYPE_SLIDER -> {
                SliderViewHolder(ListItemSettingBinding.inflate(inflater), this)
            }

            SettingsItem.TYPE_SUBMENU -> {
                SubmenuViewHolder(ListItemSettingBinding.inflate(inflater), this)
            }

            SettingsItem.TYPE_DATETIME_SETTING -> {
                DateTimeViewHolder(ListItemSettingBinding.inflate(inflater), this)
            }

            SettingsItem.TYPE_RUNNABLE -> {
                RunnableViewHolder(ListItemSettingBinding.inflate(inflater), this)
            }

            SettingsItem.TYPE_INPUT -> {
                InputViewHolder(ListItemSettingInputBinding.inflate(inflater), this)
            }

            SettingsItem.TYPE_INT_SINGLE_CHOICE -> {
                SingleChoiceViewHolder(ListItemSettingBinding.inflate(inflater), this)
            }

            SettingsItem.TYPE_INPUT_PROFILE -> {
                InputProfileViewHolder(ListItemSettingBinding.inflate(inflater), this)
            }

            SettingsItem.TYPE_STRING_INPUT -> {
                StringInputViewHolder(ListItemSettingBinding.inflate(inflater), this)
            }

            else -> {
                HeaderViewHolder(ListItemSettingsHeaderBinding.inflate(inflater), this)
            }
        }
    }

    override fun onBindViewHolder(holder: SettingViewHolder, position: Int) {
        holder.bind(currentList[position])
    }

    override fun getItemCount(): Int = currentList.size

    override fun getItemViewType(position: Int): Int {
        return currentList[position].type
    }

    fun onBooleanClick(item: SwitchSetting, checked: Boolean, position: Int) {
        item.setChecked(checked)
        notifyItemChanged(position)
        settingsViewModel.setShouldReloadSettingsList(true)
    }

    fun onSingleChoiceClick(item: SingleChoiceSetting, position: Int) {
        SettingsDialogFragment.newInstance(
            settingsViewModel,
            item,
            SettingsItem.TYPE_SINGLE_CHOICE,
            position
        ).show(fragment.childFragmentManager, SettingsDialogFragment.TAG)
    }

    fun onStringSingleChoiceClick(item: StringSingleChoiceSetting, position: Int) {
        SettingsDialogFragment.newInstance(
            settingsViewModel,
            item,
            SettingsItem.TYPE_STRING_SINGLE_CHOICE,
            position
        ).show(fragment.childFragmentManager, SettingsDialogFragment.TAG)
    }

    fun onIntSingleChoiceClick(item: IntSingleChoiceSetting, position: Int) {
        SettingsDialogFragment.newInstance(
            settingsViewModel,
            item,
            SettingsItem.TYPE_INT_SINGLE_CHOICE,
            position
        ).show(fragment.childFragmentManager, SettingsDialogFragment.TAG)
    }

    fun onDateTimeClick(item: DateTimeSetting, position: Int) {
        val storedTime = item.getValue() * 1000

        // Helper to extract hour and minute from epoch time
        val calendar: Calendar = Calendar.getInstance()
        calendar.timeInMillis = storedTime
        calendar.timeZone = TimeZone.getTimeZone("UTC")

        var timeFormat: Int = TimeFormat.CLOCK_12H
        if (DateFormat.is24HourFormat(context)) {
            timeFormat = TimeFormat.CLOCK_24H
        }

        val datePicker: MaterialDatePicker<Long> = MaterialDatePicker.Builder.datePicker()
            .setSelection(storedTime)
            .setTitleText(R.string.select_rtc_date)
            .build()
        val timePicker: MaterialTimePicker = MaterialTimePicker.Builder()
            .setTimeFormat(timeFormat)
            .setHour(calendar.get(Calendar.HOUR_OF_DAY))
            .setMinute(calendar.get(Calendar.MINUTE))
            .setTitleText(R.string.select_rtc_time)
            .build()

        datePicker.addOnPositiveButtonClickListener {
            timePicker.show(
                fragment.childFragmentManager,
                "TimePicker"
            )
        }
        timePicker.addOnPositiveButtonClickListener {
            var epochTime: Long = datePicker.selection!! / 1000
            epochTime += timePicker.hour.toLong() * 60 * 60
            epochTime += timePicker.minute.toLong() * 60
            if (item.getValue() != epochTime) {
                notifyItemChanged(position)
                item.setValue(epochTime)
            }
        }
        datePicker.show(
            fragment.childFragmentManager,
            "DatePicker"
        )
    }

    fun onSliderClick(item: SliderSetting, position: Int) {
        SettingsDialogFragment.newInstance(
            settingsViewModel,
            item,
            SettingsItem.TYPE_SLIDER,
            position
        ).show(fragment.childFragmentManager, SettingsDialogFragment.TAG)
    }

    fun onSubmenuClick(item: SubmenuSetting) {
        val action = SettingsNavigationDirections.actionGlobalSettingsFragment(item.menuKey, null)
        fragment.view?.findNavController()?.navigate(action)
    }

    fun onInputProfileClick(item: InputProfileSetting, position: Int) {
        InputProfileDialogFragment.newInstance(
            settingsViewModel,
            item,
            position
        ).show(fragment.childFragmentManager, InputProfileDialogFragment.TAG)
    }

    fun onInputClick(item: InputSetting, position: Int) {
        InputDialogFragment.newInstance(
            settingsViewModel,
            item,
            position
        ).show(fragment.childFragmentManager, InputDialogFragment.TAG)
    }

    fun onInputOptionsClick(anchor: View, item: InputSetting, position: Int) {
        val popup = PopupMenu(context, anchor)
        popup.menuInflater.inflate(R.menu.menu_input_options, popup.menu)

        popup.menu.apply {
            val invertAxis = findItem(R.id.invert_axis)
            val invertButton = findItem(R.id.invert_button)
            val toggleButton = findItem(R.id.toggle_button)
            val turboButton = findItem(R.id.turbo_button)
            val setThreshold = findItem(R.id.set_threshold)
            val toggleAxis = findItem(R.id.toggle_axis)
            when (item) {
                is AnalogInputSetting -> {
                    val params = NativeInput.getStickParam(item.playerIndex, item.nativeAnalog)

                    invertAxis.isVisible = true
                    invertAxis.isCheckable = true
                    invertAxis.isChecked = when (item.analogDirection) {
                        AnalogDirection.Left, AnalogDirection.Right -> {
                            params.get("invert_x", "+") == "-"
                        }

                        AnalogDirection.Up, AnalogDirection.Down -> {
                            params.get("invert_y", "+") == "-"
                        }
                    }
                    invertAxis.setOnMenuItemClickListener {
                        if (item.analogDirection == AnalogDirection.Left ||
                            item.analogDirection == AnalogDirection.Right
                        ) {
                            val invertValue = params.get("invert_x", "+") == "-"
                            val invertString = if (invertValue) "+" else "-"
                            params.set("invert_x", invertString)
                        } else if (
                            item.analogDirection == AnalogDirection.Up ||
                            item.analogDirection == AnalogDirection.Down
                        ) {
                            val invertValue = params.get("invert_y", "+") == "-"
                            val invertString = if (invertValue) "+" else "-"
                            params.set("invert_y", invertString)
                        }
                        true
                    }

                    popup.setOnDismissListener {
                        NativeInput.setStickParam(item.playerIndex, item.nativeAnalog, params)
                        settingsViewModel.setDatasetChanged(true)
                    }
                }

                is ButtonInputSetting -> {
                    val params = NativeInput.getButtonParam(item.playerIndex, item.nativeButton)
                    if (params.has("code") || params.has("button") || params.has("hat")) {
                        val buttonInvert = params.get("inverted", false)
                        invertButton.isVisible = true
                        invertButton.isCheckable = true
                        invertButton.isChecked = buttonInvert
                        invertButton.setOnMenuItemClickListener {
                            params.set("inverted", !buttonInvert)
                            true
                        }

                        val toggle = params.get("toggle", false)
                        toggleButton.isVisible = true
                        toggleButton.isCheckable = true
                        toggleButton.isChecked = toggle
                        toggleButton.setOnMenuItemClickListener {
                            params.set("toggle", !toggle)
                            true
                        }

                        val turbo = params.get("turbo", false)
                        turboButton.isVisible = true
                        turboButton.isCheckable = true
                        turboButton.isChecked = turbo
                        turboButton.setOnMenuItemClickListener {
                            params.set("turbo", !turbo)
                            true
                        }
                    } else if (params.has("axis")) {
                        val axisInvert = params.get("invert", "+") == "-"
                        invertAxis.isVisible = true
                        invertAxis.isCheckable = true
                        invertAxis.isChecked = axisInvert
                        invertAxis.setOnMenuItemClickListener {
                            params.set("invert", if (!axisInvert) "-" else "+")
                            true
                        }

                        val buttonInvert = params.get("inverted", false)
                        invertButton.isVisible = true
                        invertButton.isCheckable = true
                        invertButton.isChecked = buttonInvert
                        invertButton.setOnMenuItemClickListener {
                            params.set("inverted", !buttonInvert)
                            true
                        }

                        setThreshold.isVisible = true
                        val thresholdSetting = object : AbstractIntSetting {
                            override val key = ""

                            override fun getInt(needsGlobal: Boolean): Int =
                                (params.get("threshold", 0.5f) * 100).toInt()

                            override fun setInt(value: Int) {
                                params.set("threshold", value.toFloat() / 100)
                                NativeInput.setButtonParam(
                                    item.playerIndex,
                                    item.nativeButton,
                                    params
                                )
                            }

                            override val defaultValue = 50

                            override fun getValueAsString(needsGlobal: Boolean): String =
                                getInt(needsGlobal).toString()

                            override fun reset() = setInt(defaultValue)
                        }
                        setThreshold.setOnMenuItemClickListener {
                            onSliderClick(
                                SliderSetting(thresholdSetting, R.string.set_threshold),
                                position
                            )
                            true
                        }

                        val axisToggle = params.get("toggle", false)
                        toggleAxis.isVisible = true
                        toggleAxis.isCheckable = true
                        toggleAxis.isChecked = axisToggle
                        toggleAxis.setOnMenuItemClickListener {
                            params.set("toggle", !axisToggle)
                            true
                        }
                    }

                    popup.setOnDismissListener {
                        NativeInput.setButtonParam(item.playerIndex, item.nativeButton, params)
                        settingsViewModel.setAdapterItemChanged(position)
                    }
                }

                is ModifierInputSetting -> {
                    val stickParams = NativeInput.getStickParam(item.playerIndex, item.nativeAnalog)
                    val modifierParams = ParamPackage(stickParams.get("modifier", ""))

                    val invert = modifierParams.get("inverted", false)
                    invertButton.isVisible = true
                    invertButton.isCheckable = true
                    invertButton.isChecked = invert
                    invertButton.setOnMenuItemClickListener {
                        modifierParams.set("inverted", !invert)
                        stickParams.set("modifier", modifierParams.serialize())
                        true
                    }

                    val toggle = modifierParams.get("toggle", false)
                    toggleButton.isVisible = true
                    toggleButton.isCheckable = true
                    toggleButton.isChecked = toggle
                    toggleButton.setOnMenuItemClickListener {
                        modifierParams.set("toggle", !toggle)
                        stickParams.set("modifier", modifierParams.serialize())
                        true
                    }

                    popup.setOnDismissListener {
                        NativeInput.setStickParam(
                            item.playerIndex,
                            item.nativeAnalog,
                            stickParams
                        )
                        settingsViewModel.setAdapterItemChanged(position)
                    }
                }
            }
        }
        popup.show()
    }

    fun onStringInputClick(item: StringInputSetting, position: Int) {
        SettingsDialogFragment.newInstance(
            settingsViewModel,
            item,
            SettingsItem.TYPE_STRING_INPUT,
            position
        ).show(fragment.childFragmentManager, SettingsDialogFragment.TAG)
    }

    fun onLongClick(item: SettingsItem, position: Int): Boolean {
        SettingsDialogFragment.newInstance(
            settingsViewModel,
            item,
            SettingsDialogFragment.TYPE_RESET_SETTING,
            position
        ).show(fragment.childFragmentManager, SettingsDialogFragment.TAG)

        return true
    }

    fun onClearClick(item: SettingsItem, position: Int) {
        item.setting.global = true
        notifyItemChanged(position)
        settingsViewModel.setShouldReloadSettingsList(true)
    }

    private class DiffCallback : DiffUtil.ItemCallback<SettingsItem>() {
        override fun areItemsTheSame(oldItem: SettingsItem, newItem: SettingsItem): Boolean {
            return oldItem.setting.key == newItem.setting.key
        }

        override fun areContentsTheSame(oldItem: SettingsItem, newItem: SettingsItem): Boolean {
            return oldItem.setting.key == newItem.setting.key
        }
    }
}
