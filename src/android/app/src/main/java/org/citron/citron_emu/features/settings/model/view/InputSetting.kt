// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.model.view

import androidx.annotation.StringRes
import org.citron.citron_emu.R
import org.citron.citron_emu.CitronApplication
import org.citron.citron_emu.features.input.NativeInput
import org.citron.citron_emu.features.input.model.ButtonName
import org.citron.citron_emu.features.input.model.InputType
import org.citron.citron_emu.utils.ParamPackage

sealed class InputSetting(
    @StringRes titleId: Int,
    titleString: String
) : SettingsItem(emptySetting, titleId, titleString, 0, "") {
    override val type = TYPE_INPUT
    abstract val inputType: InputType
    abstract val playerIndex: Int

    protected val context get() = CitronApplication.appContext

    abstract fun getSelectedValue(): String

    abstract fun setSelectedValue(param: ParamPackage)

    protected fun getDisplayString(params: ParamPackage, control: String): String {
        val deviceName = params.get("display", "")
        deviceName.ifEmpty {
            return context.getString(R.string.not_set)
        }
        return "$deviceName: $control"
    }

    private fun getDirectionName(direction: String): String =
        when (direction) {
            "up" -> context.getString(R.string.up)
            "down" -> context.getString(R.string.down)
            "left" -> context.getString(R.string.left)
            "right" -> context.getString(R.string.right)
            else -> direction
        }

    protected fun buttonToText(param: ParamPackage): String {
        if (!param.has("engine")) {
            return context.getString(R.string.not_set)
        }

        val toggle = if (param.get("toggle", false)) "~" else ""
        val inverted = if (param.get("inverted", false)) "!" else ""
        val invert = if (param.get("invert", "+") == "-") "-" else ""
        val turbo = if (param.get("turbo", false)) "$" else ""
        val commonButtonName = NativeInput.getButtonName(param)

        if (commonButtonName == ButtonName.Invalid) {
            return context.getString(R.string.invalid)
        }

        if (commonButtonName == ButtonName.Engine) {
            return param.get("engine", "")
        }

        if (commonButtonName == ButtonName.Value) {
            if (param.has("hat")) {
                val hat = getDirectionName(param.get("direction", ""))
                return context.getString(R.string.qualified_hat, turbo, toggle, inverted, hat)
            }
            if (param.has("axis")) {
                val axis = param.get("axis", "")
                return context.getString(
                    R.string.qualified_button_stick_axis,
                    toggle,
                    inverted,
                    invert,
                    axis
                )
            }
            if (param.has("button")) {
                val button = param.get("button", "")
                return context.getString(R.string.qualified_button, turbo, toggle, inverted, button)
            }
        }

        return context.getString(R.string.unknown)
    }

    protected fun analogToText(param: ParamPackage, direction: String): String {
        if (!param.has("engine")) {
            return context.getString(R.string.not_set)
        }

        if (param.get("engine", "") == "analog_from_button") {
            return buttonToText(ParamPackage(param.get(direction, "")))
        }

        if (!param.has("axis_x") || !param.has("axis_y")) {
            return context.getString(R.string.unknown)
        }

        val xAxis = param.get("axis_x", "")
        val yAxis = param.get("axis_y", "")
        val xInvert = param.get("invert_x", "+") == "-"
        val yInvert = param.get("invert_y", "+") == "-"

        if (direction == "modifier") {
            return context.getString(R.string.unused)
        }

        when (direction) {
            "up" -> {
                val yInvertString = if (yInvert) "+" else "-"
                return context.getString(R.string.qualified_axis, yAxis, yInvertString)
            }

            "down" -> {
                val yInvertString = if (yInvert) "-" else "+"
                return context.getString(R.string.qualified_axis, yAxis, yInvertString)
            }

            "left" -> {
                val xInvertString = if (xInvert) "+" else "-"
                return context.getString(R.string.qualified_axis, xAxis, xInvertString)
            }

            "right" -> {
                val xInvertString = if (xInvert) "-" else "+"
                return context.getString(R.string.qualified_axis, xAxis, xInvertString)
            }
        }

        return context.getString(R.string.unknown)
    }
}
