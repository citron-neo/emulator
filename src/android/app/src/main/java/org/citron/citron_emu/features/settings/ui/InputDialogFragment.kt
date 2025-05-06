// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.ui

import android.app.Dialog
import android.graphics.drawable.Animatable2
import android.graphics.drawable.AnimatedVectorDrawable
import android.graphics.drawable.Drawable
import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.activityViewModels
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citron.citron_emu.R
import org.citron.citron_emu.databinding.DialogMappingBinding
import org.citron.citron_emu.features.input.NativeInput
import org.citron.citron_emu.features.input.model.NativeAnalog
import org.citron.citron_emu.features.input.model.NativeButton
import org.citron.citron_emu.features.settings.model.view.AnalogInputSetting
import org.citron.citron_emu.features.settings.model.view.ButtonInputSetting
import org.citron.citron_emu.features.settings.model.view.InputSetting
import org.citron.citron_emu.features.settings.model.view.ModifierInputSetting
import org.citron.citron_emu.utils.InputHandler
import org.citron.citron_emu.utils.ParamPackage

class InputDialogFragment : DialogFragment() {
    private var inputAccepted = false

    private var position: Int = 0

    private lateinit var inputSetting: InputSetting

    private lateinit var binding: DialogMappingBinding

    private val settingsViewModel: SettingsViewModel by activityViewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (settingsViewModel.clickedItem == null) dismiss()

        position = requireArguments().getInt(POSITION)

        InputHandler.updateControllerData()
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        inputSetting = settingsViewModel.clickedItem as InputSetting
        binding = DialogMappingBinding.inflate(layoutInflater)

        val builder = MaterialAlertDialogBuilder(requireContext())
            .setPositiveButton(android.R.string.cancel) { _, _ ->
                NativeInput.stopMapping()
                dismiss()
            }
            .setView(binding.root)

        val playButtonMapAnimation = { twoDirections: Boolean ->
            val stickAnimation: AnimatedVectorDrawable
            val buttonAnimation: AnimatedVectorDrawable
            binding.imageStickAnimation.apply {
                val anim = if (twoDirections) {
                    R.drawable.stick_two_direction_anim
                } else {
                    R.drawable.stick_one_direction_anim
                }
                setBackgroundResource(anim)
                stickAnimation = background as AnimatedVectorDrawable
            }
            binding.imageButtonAnimation.apply {
                setBackgroundResource(R.drawable.button_anim)
                buttonAnimation = background as AnimatedVectorDrawable
            }
            stickAnimation.registerAnimationCallback(object : Animatable2.AnimationCallback() {
                override fun onAnimationEnd(drawable: Drawable?) {
                    buttonAnimation.start()
                }
            })
            buttonAnimation.registerAnimationCallback(object : Animatable2.AnimationCallback() {
                override fun onAnimationEnd(drawable: Drawable?) {
                    stickAnimation.start()
                }
            })
            stickAnimation.start()
        }

        when (val setting = inputSetting) {
            is AnalogInputSetting -> {
                when (setting.nativeAnalog) {
                    NativeAnalog.LStick -> builder.setTitle(
                        getString(R.string.map_control, getString(R.string.left_stick))
                    )

                    NativeAnalog.RStick -> builder.setTitle(
                        getString(R.string.map_control, getString(R.string.right_stick))
                    )
                }

                builder.setMessage(R.string.stick_map_description)

                playButtonMapAnimation.invoke(true)
            }

            is ModifierInputSetting -> {
                builder.setTitle(getString(R.string.map_control, setting.title))
                    .setMessage(R.string.button_map_description)
                playButtonMapAnimation.invoke(false)
            }

            is ButtonInputSetting -> {
                if (setting.nativeButton == NativeButton.DUp ||
                    setting.nativeButton == NativeButton.DDown ||
                    setting.nativeButton == NativeButton.DLeft ||
                    setting.nativeButton == NativeButton.DRight
                ) {
                    builder.setTitle(getString(R.string.map_dpad_direction, setting.title))
                } else {
                    builder.setTitle(getString(R.string.map_control, setting.title))
                }
                builder.setMessage(R.string.button_map_description)
                playButtonMapAnimation.invoke(false)
            }
        }

        return builder.create()
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        view.requestFocus()
        view.setOnFocusChangeListener { v, hasFocus -> if (!hasFocus) v.requestFocus() }
        dialog?.setOnKeyListener { _, _, keyEvent -> onKeyEvent(keyEvent) }
        binding.root.setOnGenericMotionListener { _, motionEvent -> onMotionEvent(motionEvent) }
        NativeInput.beginMapping(inputSetting.inputType.int)
    }

    private fun onKeyEvent(event: KeyEvent): Boolean {
        if (event.source and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK &&
            event.source and InputDevice.SOURCE_GAMEPAD != InputDevice.SOURCE_GAMEPAD
        ) {
            return false
        }

        val action = when (event.action) {
            KeyEvent.ACTION_DOWN -> NativeInput.ButtonState.PRESSED
            KeyEvent.ACTION_UP -> NativeInput.ButtonState.RELEASED
            else -> return false
        }
        val controllerData =
            InputHandler.androidControllers[event.device.controllerNumber] ?: return false
        NativeInput.onGamePadButtonEvent(
            controllerData.getGUID(),
            controllerData.getPort(),
            event.keyCode,
            action
        )
        onInputReceived(event.device)
        return true
    }

    private fun onMotionEvent(event: MotionEvent): Boolean {
        if (event.source and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK &&
            event.source and InputDevice.SOURCE_GAMEPAD != InputDevice.SOURCE_GAMEPAD
        ) {
            return false
        }

        // Temp workaround for DPads that give both axis and button input. The input system can't
        // take in a specific axis direction for a binding so you lose half of the directions for a DPad.

        val controllerData =
            InputHandler.androidControllers[event.device.controllerNumber] ?: return false
        event.device.motionRanges.forEach {
            NativeInput.onGamePadAxisEvent(
                controllerData.getGUID(),
                controllerData.getPort(),
                it.axis,
                event.getAxisValue(it.axis)
            )
            onInputReceived(event.device)
        }
        return true
    }

    private fun onInputReceived(device: InputDevice) {
        val params = ParamPackage(NativeInput.getNextInput())
        if (params.has("engine") && isInputAcceptable(params) && !inputAccepted) {
            inputAccepted = true
            setResult(params, device)
        }
    }

    private fun setResult(params: ParamPackage, device: InputDevice) {
        NativeInput.stopMapping()
        params.set("display", "${device.name} ${params.get("port", 0)}")
        when (val item = settingsViewModel.clickedItem as InputSetting) {
            is ModifierInputSetting,
            is ButtonInputSetting -> {
                // Invert DPad up and left bindings by default
                val tempSetting = inputSetting as? ButtonInputSetting
                if (tempSetting != null) {
                    if (tempSetting.nativeButton == NativeButton.DUp ||
                        tempSetting.nativeButton == NativeButton.DLeft &&
                        params.has("axis")
                    ) {
                        params.set("invert", "-")
                    }
                }

                item.setSelectedValue(params)
                settingsViewModel.setAdapterItemChanged(position)
            }

            is AnalogInputSetting -> {
                var analogParam = NativeInput.getStickParam(item.playerIndex, item.nativeAnalog)
                analogParam = adjustAnalogParam(params, analogParam, item.analogDirection.param)

                // Invert Y-Axis by default
                analogParam.set("invert_y", "-")

                item.setSelectedValue(analogParam)
                settingsViewModel.setReloadListAndNotifyDataset(true)
            }
        }
        dismiss()
    }

    private fun adjustAnalogParam(
        inputParam: ParamPackage,
        analogParam: ParamPackage,
        buttonName: String
    ): ParamPackage {
        // The poller returned a complete axis, so set all the buttons
        if (inputParam.has("axis_x") && inputParam.has("axis_y")) {
            return inputParam
        }

        // Check if the current configuration has either no engine or an axis binding.
        // Clears out the old binding and adds one with analog_from_button.
        if (!analogParam.has("engine") || analogParam.has("axis_x") || analogParam.has("axis_y")) {
            analogParam.clear()
            analogParam.set("engine", "analog_from_button")
        }
        analogParam.set(buttonName, inputParam.serialize())
        return analogParam
    }

    private fun isInputAcceptable(params: ParamPackage): Boolean {
        if (InputHandler.registeredControllers.size == 1) {
            return true
        }

        if (params.has("motion")) {
            return true
        }

        val currentDevice = settingsViewModel.getCurrentDeviceParams(params)
        if (currentDevice.get("engine", "any") == "any") {
            return true
        }

        val guidMatch = params.get("guid", "") == currentDevice.get("guid", "") ||
            params.get("guid", "") == currentDevice.get("guid2", "")
        return params.get("engine", "") == currentDevice.get("engine", "") &&
            guidMatch &&
            params.get("port", 0) == currentDevice.get("port", 0)
    }

    companion object {
        const val TAG = "InputDialogFragment"

        const val POSITION = "Position"

        fun newInstance(
            inputMappingViewModel: SettingsViewModel,
            setting: InputSetting,
            position: Int
        ): InputDialogFragment {
            inputMappingViewModel.clickedItem = setting
            val args = Bundle()
            args.putInt(POSITION, position)
            val fragment = InputDialogFragment()
            fragment.arguments = args
            return fragment
        }
    }
}
