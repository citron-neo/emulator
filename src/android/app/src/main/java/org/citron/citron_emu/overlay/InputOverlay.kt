// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.overlay

import android.app.Activity
import android.content.Context
import android.content.SharedPreferences
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Point
import android.graphics.Rect
import android.graphics.drawable.Drawable
import android.graphics.drawable.VectorDrawable
import android.os.Build
import android.util.AttributeSet
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.SurfaceView
import android.view.View
import android.view.View.OnTouchListener
import android.view.WindowInsets
import androidx.core.content.ContextCompat
import androidx.window.layout.WindowMetricsCalculator
import kotlin.math.max
import kotlin.math.min
import org.citron.citron_emu.features.input.NativeInput
import org.citron.citron_emu.R
import org.citron.citron_emu.features.input.model.NativeAnalog
import org.citron.citron_emu.features.input.model.NativeButton
import org.citron.citron_emu.features.input.model.NpadStyleIndex
import org.citron.citron_emu.features.settings.model.BooleanSetting
import org.citron.citron_emu.features.settings.model.IntSetting
import org.citron.citron_emu.overlay.model.OverlayControl
import org.citron.citron_emu.overlay.model.OverlayControlData
import org.citron.citron_emu.overlay.model.OverlayLayout
import org.citron.citron_emu.utils.NativeConfig

/**
 * Draws the interactive input overlay on top of the
 * [SurfaceView] that is rendering emulation.
 */
class InputOverlay(context: Context, attrs: AttributeSet?) :
    SurfaceView(context, attrs),
    OnTouchListener {
    private val overlayButtons: MutableSet<InputOverlayDrawableButton> = HashSet()
    private val overlayDpads: MutableSet<InputOverlayDrawableDpad> = HashSet()
    private val overlayJoysticks: MutableSet<InputOverlayDrawableJoystick> = HashSet()

    private var inEditMode = false
    private var buttonBeingConfigured: InputOverlayDrawableButton? = null
    private var dpadBeingConfigured: InputOverlayDrawableDpad? = null
    private var joystickBeingConfigured: InputOverlayDrawableJoystick? = null

    private lateinit var windowInsets: WindowInsets

    var layout = OverlayLayout.Landscape

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        super.onLayout(changed, left, top, right, bottom)

        windowInsets = rootWindowInsets

        val overlayControlData = NativeConfig.getOverlayControlData()
        if (overlayControlData.isEmpty()) {
            populateDefaultConfig()
        } else {
            checkForNewControls(overlayControlData)
        }

        // Load the controls.
        refreshControls()

        // Set the on touch listener.
        setOnTouchListener(this)

        // Force draw
        setWillNotDraw(false)

        // Request focus for the overlay so it has priority on presses.
        requestFocus()
    }

    override fun draw(canvas: Canvas) {
        super.draw(canvas)
        for (button in overlayButtons) {
            button.draw(canvas)
        }
        for (dpad in overlayDpads) {
            dpad.draw(canvas)
        }
        for (joystick in overlayJoysticks) {
            joystick.draw(canvas)
        }
    }

    override fun onTouch(v: View, event: MotionEvent): Boolean {
        if (inEditMode) {
            return onTouchWhileEditing(event)
        }

        var shouldUpdateView = false
        val playerIndex = when (NativeInput.getStyleIndex(0)) {
            NpadStyleIndex.Handheld -> 8
            else -> 0
        }

        for (button in overlayButtons) {
            if (!button.updateStatus(event)) {
                continue
            }
            NativeInput.onOverlayButtonEvent(
                playerIndex,
                button.button,
                button.status
            )
            playHaptics(event)
            shouldUpdateView = true
        }

        for (dpad in overlayDpads) {
            if (!dpad.updateStatus(event, BooleanSetting.DPAD_SLIDE.getBoolean())) {
                continue
            }
            NativeInput.onOverlayButtonEvent(
                playerIndex,
                dpad.up,
                dpad.upStatus
            )
            NativeInput.onOverlayButtonEvent(
                playerIndex,
                dpad.down,
                dpad.downStatus
            )
            NativeInput.onOverlayButtonEvent(
                playerIndex,
                dpad.left,
                dpad.leftStatus
            )
            NativeInput.onOverlayButtonEvent(
                playerIndex,
                dpad.right,
                dpad.rightStatus
            )
            playHaptics(event)
            shouldUpdateView = true
        }

        for (joystick in overlayJoysticks) {
            if (!joystick.updateStatus(event)) {
                continue
            }
            NativeInput.onOverlayJoystickEvent(
                playerIndex,
                joystick.joystick,
                joystick.xAxis,
                joystick.realYAxis
            )
            NativeInput.onOverlayButtonEvent(
                playerIndex,
                joystick.button,
                joystick.buttonStatus
            )
            playHaptics(event)
            shouldUpdateView = true
        }

        if (shouldUpdateView) {
            invalidate()
        }

        if (!BooleanSetting.TOUCHSCREEN.getBoolean()) {
            return true
        }

        val pointerIndex = event.actionIndex
        val xPosition = event.getX(pointerIndex).toInt()
        val yPosition = event.getY(pointerIndex).toInt()
        val pointerId = event.getPointerId(pointerIndex)
        val motionEvent = event.action and MotionEvent.ACTION_MASK
        val isActionDown =
            motionEvent == MotionEvent.ACTION_DOWN || motionEvent == MotionEvent.ACTION_POINTER_DOWN
        val isActionMove = motionEvent == MotionEvent.ACTION_MOVE
        val isActionUp =
            motionEvent == MotionEvent.ACTION_UP || motionEvent == MotionEvent.ACTION_POINTER_UP

        if (isActionDown && !isTouchInputConsumed(pointerId)) {
            NativeInput.onTouchPressed(pointerId, xPosition.toFloat(), yPosition.toFloat())
        }

        if (isActionMove) {
            for (i in 0 until event.pointerCount) {
                val fingerId = event.getPointerId(i)
                if (isTouchInputConsumed(fingerId)) {
                    continue
                }
                NativeInput.onTouchMoved(fingerId, event.getX(i), event.getY(i))
            }
        }

        if (isActionUp && !isTouchInputConsumed(pointerId)) {
            NativeInput.onTouchReleased(pointerId)
        }

        return true
    }

    private fun playHaptics(event: MotionEvent) {
        if (BooleanSetting.HAPTIC_FEEDBACK.getBoolean()) {
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN,
                MotionEvent.ACTION_POINTER_DOWN ->
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)

                MotionEvent.ACTION_UP,
                MotionEvent.ACTION_POINTER_UP ->
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY_RELEASE)
            }
        }
    }

    private fun isTouchInputConsumed(track_id: Int): Boolean {
        for (button in overlayButtons) {
            if (button.trackId == track_id) {
                return true
            }
        }
        for (dpad in overlayDpads) {
            if (dpad.trackId == track_id) {
                return true
            }
        }
        for (joystick in overlayJoysticks) {
            if (joystick.trackId == track_id) {
                return true
            }
        }
        return false
    }

    private fun onTouchWhileEditing(event: MotionEvent): Boolean {
        val pointerIndex = event.actionIndex
        val fingerPositionX = event.getX(pointerIndex).toInt()
        val fingerPositionY = event.getY(pointerIndex).toInt()

        for (button in overlayButtons) {
            // Determine the button state to apply based on the MotionEvent action flag.
            when (event.action and MotionEvent.ACTION_MASK) {
                MotionEvent.ACTION_DOWN,
                MotionEvent.ACTION_POINTER_DOWN ->
                    // If no button is being moved now, remember the currently touched button to move.
                    if (buttonBeingConfigured == null &&
                        button.bounds.contains(fingerPositionX, fingerPositionY)
                    ) {
                        buttonBeingConfigured = button
                        buttonBeingConfigured!!.onConfigureTouch(event)
                    }

                MotionEvent.ACTION_MOVE -> if (buttonBeingConfigured != null) {
                    buttonBeingConfigured!!.onConfigureTouch(event)
                    invalidate()
                    return true
                }

                MotionEvent.ACTION_UP,
                MotionEvent.ACTION_POINTER_UP -> if (buttonBeingConfigured === button) {
                    // Persist button position by saving new place.
                    saveControlPosition(
                        buttonBeingConfigured!!.overlayControlData.id,
                        buttonBeingConfigured!!.bounds.centerX(),
                        buttonBeingConfigured!!.bounds.centerY(),
                        layout
                    )
                    buttonBeingConfigured = null
                }
            }
        }

        for (dpad in overlayDpads) {
            // Determine the button state to apply based on the MotionEvent action flag.
            when (event.action and MotionEvent.ACTION_MASK) {
                MotionEvent.ACTION_DOWN,
                MotionEvent.ACTION_POINTER_DOWN ->
                    // If no button is being moved now, remember the currently touched button to move.
                    if (buttonBeingConfigured == null &&
                        dpad.bounds.contains(fingerPositionX, fingerPositionY)
                    ) {
                        dpadBeingConfigured = dpad
                        dpadBeingConfigured!!.onConfigureTouch(event)
                    }

                MotionEvent.ACTION_MOVE -> if (dpadBeingConfigured != null) {
                    dpadBeingConfigured!!.onConfigureTouch(event)
                    invalidate()
                    return true
                }

                MotionEvent.ACTION_UP,
                MotionEvent.ACTION_POINTER_UP -> if (dpadBeingConfigured === dpad) {
                    // Persist button position by saving new place.
                    saveControlPosition(
                        OverlayControl.COMBINED_DPAD.id,
                        dpadBeingConfigured!!.bounds.centerX(),
                        dpadBeingConfigured!!.bounds.centerY(),
                        layout
                    )
                    dpadBeingConfigured = null
                }
            }
        }

        for (joystick in overlayJoysticks) {
            when (event.action) {
                MotionEvent.ACTION_DOWN,
                MotionEvent.ACTION_POINTER_DOWN -> if (joystickBeingConfigured == null &&
                    joystick.bounds.contains(fingerPositionX, fingerPositionY)
                ) {
                    joystickBeingConfigured = joystick
                    joystickBeingConfigured!!.onConfigureTouch(event)
                }

                MotionEvent.ACTION_MOVE -> if (joystickBeingConfigured != null) {
                    joystickBeingConfigured!!.onConfigureTouch(event)
                    invalidate()
                }

                MotionEvent.ACTION_UP,
                MotionEvent.ACTION_POINTER_UP -> if (joystickBeingConfigured != null) {
                    saveControlPosition(
                        joystickBeingConfigured!!.prefId,
                        joystickBeingConfigured!!.bounds.centerX(),
                        joystickBeingConfigured!!.bounds.centerY(),
                        layout
                    )
                    joystickBeingConfigured = null
                }
            }
        }

        return true
    }

    private fun addOverlayControls(layout: OverlayLayout) {
        val windowSize = getSafeScreenSize(context, Pair(measuredWidth, measuredHeight))
        val overlayControlData = NativeConfig.getOverlayControlData()
        for (data in overlayControlData) {
            if (!data.enabled) {
                continue
            }

            val position = data.positionFromLayout(layout)
            when (data.id) {
                OverlayControl.BUTTON_A.id -> {
                    overlayButtons.add(
                        initializeOverlayButton(
                            context,
                            windowSize,
                            R.drawable.facebutton_a,
                            R.drawable.facebutton_a_depressed,
                            NativeButton.A,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.BUTTON_B.id -> {
                    overlayButtons.add(
                        initializeOverlayButton(
                            context,
                            windowSize,
                            R.drawable.facebutton_b,
                            R.drawable.facebutton_b_depressed,
                            NativeButton.B,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.BUTTON_X.id -> {
                    overlayButtons.add(
                        initializeOverlayButton(
                            context,
                            windowSize,
                            R.drawable.facebutton_x,
                            R.drawable.facebutton_x_depressed,
                            NativeButton.X,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.BUTTON_Y.id -> {
                    overlayButtons.add(
                        initializeOverlayButton(
                            context,
                            windowSize,
                            R.drawable.facebutton_y,
                            R.drawable.facebutton_y_depressed,
                            NativeButton.Y,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.BUTTON_PLUS.id -> {
                    overlayButtons.add(
                        initializeOverlayButton(
                            context,
                            windowSize,
                            R.drawable.facebutton_plus,
                            R.drawable.facebutton_plus_depressed,
                            NativeButton.Plus,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.BUTTON_MINUS.id -> {
                    overlayButtons.add(
                        initializeOverlayButton(
                            context,
                            windowSize,
                            R.drawable.facebutton_minus,
                            R.drawable.facebutton_minus_depressed,
                            NativeButton.Minus,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.BUTTON_HOME.id -> {
                    overlayButtons.add(
                        initializeOverlayButton(
                            context,
                            windowSize,
                            R.drawable.facebutton_home,
                            R.drawable.facebutton_home_depressed,
                            NativeButton.Home,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.BUTTON_CAPTURE.id -> {
                    overlayButtons.add(
                        initializeOverlayButton(
                            context,
                            windowSize,
                            R.drawable.facebutton_screenshot,
                            R.drawable.facebutton_screenshot_depressed,
                            NativeButton.Capture,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.BUTTON_L.id -> {
                    overlayButtons.add(
                        initializeOverlayButton(
                            context,
                            windowSize,
                            R.drawable.l_shoulder,
                            R.drawable.l_shoulder_depressed,
                            NativeButton.L,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.BUTTON_R.id -> {
                    overlayButtons.add(
                        initializeOverlayButton(
                            context,
                            windowSize,
                            R.drawable.r_shoulder,
                            R.drawable.r_shoulder_depressed,
                            NativeButton.R,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.BUTTON_ZL.id -> {
                    overlayButtons.add(
                        initializeOverlayButton(
                            context,
                            windowSize,
                            R.drawable.zl_trigger,
                            R.drawable.zl_trigger_depressed,
                            NativeButton.ZL,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.BUTTON_ZR.id -> {
                    overlayButtons.add(
                        initializeOverlayButton(
                            context,
                            windowSize,
                            R.drawable.zr_trigger,
                            R.drawable.zr_trigger_depressed,
                            NativeButton.ZR,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.BUTTON_STICK_L.id -> {
                    overlayButtons.add(
                        initializeOverlayButton(
                            context,
                            windowSize,
                            R.drawable.button_l3,
                            R.drawable.button_l3_depressed,
                            NativeButton.LStick,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.BUTTON_STICK_R.id -> {
                    overlayButtons.add(
                        initializeOverlayButton(
                            context,
                            windowSize,
                            R.drawable.button_r3,
                            R.drawable.button_r3_depressed,
                            NativeButton.RStick,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.STICK_L.id -> {
                    overlayJoysticks.add(
                        initializeOverlayJoystick(
                            context,
                            windowSize,
                            R.drawable.joystick_range,
                            R.drawable.joystick,
                            R.drawable.joystick_depressed,
                            NativeAnalog.LStick,
                            NativeButton.LStick,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.STICK_R.id -> {
                    overlayJoysticks.add(
                        initializeOverlayJoystick(
                            context,
                            windowSize,
                            R.drawable.joystick_range,
                            R.drawable.joystick,
                            R.drawable.joystick_depressed,
                            NativeAnalog.RStick,
                            NativeButton.RStick,
                            data,
                            position
                        )
                    )
                }

                OverlayControl.COMBINED_DPAD.id -> {
                    overlayDpads.add(
                        initializeOverlayDpad(
                            context,
                            windowSize,
                            R.drawable.dpad_standard,
                            R.drawable.dpad_standard_cardinal_depressed,
                            R.drawable.dpad_standard_diagonal_depressed,
                            position
                        )
                    )
                }
            }
        }
    }

    fun refreshControls() {
        // Remove all the overlay buttons from the HashSet.
        overlayButtons.clear()
        overlayDpads.clear()
        overlayJoysticks.clear()

        // Add all the enabled overlay items back to the HashSet.
        if (BooleanSetting.SHOW_INPUT_OVERLAY.getBoolean()) {
            addOverlayControls(layout)
        }
        invalidate()
    }

    private fun saveControlPosition(id: String, x: Int, y: Int, layout: OverlayLayout) {
        val windowSize = getSafeScreenSize(context, Pair(measuredWidth, measuredHeight))
        val min = windowSize.first
        val max = windowSize.second
        val overlayControlData = NativeConfig.getOverlayControlData()
        val data = overlayControlData.firstOrNull { it.id == id }
        val newPosition = Pair((x - min.x).toDouble() / max.x, (y - min.y).toDouble() / max.y)
        when (layout) {
            OverlayLayout.Landscape -> data?.landscapePosition = newPosition
            OverlayLayout.Portrait -> data?.portraitPosition = newPosition
            OverlayLayout.Foldable -> data?.foldablePosition = newPosition
        }
        NativeConfig.setOverlayControlData(overlayControlData)
    }

    fun setIsInEditMode(editMode: Boolean) {
        inEditMode = editMode
    }

    /**
     * Applies and saves all default values for the overlay
     */
    private fun populateDefaultConfig() {
        val newConfig = OverlayControl.entries.map { it.toOverlayControlData() }
        NativeConfig.setOverlayControlData(newConfig.toTypedArray())
        NativeConfig.saveGlobalConfig()
    }

    /**
     * Checks if any new controls were added to OverlayControl that do not exist within deserialized
     * config and adds / saves them if necessary
     *
     * @param overlayControlData Overlay control data from [NativeConfig.getOverlayControlData]
     */
    private fun checkForNewControls(overlayControlData: Array<OverlayControlData>) {
        val missingControls = mutableListOf<OverlayControlData>()
        OverlayControl.entries.forEach { defaultControl ->
            val controlData = overlayControlData.firstOrNull { it.id == defaultControl.id }
            if (controlData == null) {
                missingControls.add(defaultControl.toOverlayControlData())
            }
        }

        if (missingControls.isNotEmpty()) {
            NativeConfig.setOverlayControlData(
                arrayOf(*overlayControlData, *(missingControls.toTypedArray()))
            )
            NativeConfig.saveGlobalConfig()
        }
    }

    fun resetLayoutVisibilityAndPlacement() {
        defaultOverlayPositionByLayout(layout)

        val overlayControlData = NativeConfig.getOverlayControlData()
        overlayControlData.forEach {
            it.enabled = OverlayControl.from(it.id)?.defaultVisibility == true
        }
        NativeConfig.setOverlayControlData(overlayControlData)

        refreshControls()
    }

    private fun defaultOverlayPositionByLayout(layout: OverlayLayout) {
        val overlayControlData = NativeConfig.getOverlayControlData()
        for (data in overlayControlData) {
            val defaultControlData = OverlayControl.from(data.id) ?: continue
            val position = defaultControlData.getDefaultPositionForLayout(layout)
            when (layout) {
                OverlayLayout.Landscape -> data.landscapePosition = position
                OverlayLayout.Portrait -> data.portraitPosition = position
                OverlayLayout.Foldable -> data.foldablePosition = position
            }
        }
        NativeConfig.setOverlayControlData(overlayControlData)
    }

    override fun isInEditMode(): Boolean {
        return inEditMode
    }

    companion object {
        // Increase this number every time there is a breaking change to every overlay layout
        const val OVERLAY_VERSION = 1

        // Increase the corresponding layout version number whenever that layout has a breaking change
        private const val LANDSCAPE_OVERLAY_VERSION = 1
        private const val PORTRAIT_OVERLAY_VERSION = 1
        private const val FOLDABLE_OVERLAY_VERSION = 1
        val overlayLayoutVersions = listOf(
            LANDSCAPE_OVERLAY_VERSION,
            PORTRAIT_OVERLAY_VERSION,
            FOLDABLE_OVERLAY_VERSION
        )

        /**
         * Resizes a [Bitmap] by a given scale factor
         *
         * @param context       Context for getting the vector drawable
         * @param drawableId    The ID of the drawable to scale.
         * @param scale         The scale factor for the bitmap.
         * @return The scaled [Bitmap]
         */
        private fun getBitmap(context: Context, drawableId: Int, scale: Float): Bitmap {
            val vectorDrawable = ContextCompat.getDrawable(context, drawableId) as VectorDrawable

            val bitmap = Bitmap.createBitmap(
                (vectorDrawable.intrinsicWidth * scale).toInt(),
                (vectorDrawable.intrinsicHeight * scale).toInt(),
                Bitmap.Config.ARGB_8888
            )

            val dm = context.resources.displayMetrics
            val minScreenDimension = min(dm.widthPixels, dm.heightPixels)

            val maxBitmapDimension = max(bitmap.width, bitmap.height)
            val bitmapScale = scale * minScreenDimension / maxBitmapDimension

            val scaledBitmap = Bitmap.createScaledBitmap(
                bitmap,
                (bitmap.width * bitmapScale).toInt(),
                (bitmap.height * bitmapScale).toInt(),
                true
            )

            val canvas = Canvas(scaledBitmap)
            vectorDrawable.setBounds(0, 0, canvas.width, canvas.height)
            vectorDrawable.draw(canvas)
            return scaledBitmap
        }

        /**
         * Gets the safe screen size for drawing the overlay
         *
         * @param context   Context for getting the window metrics
         * @return A pair of points, the first being the top left corner of the safe area,
         *                  the second being the bottom right corner of the safe area
         */
        private fun getSafeScreenSize(
            context: Context,
            screenSize: Pair<Int, Int>
        ): Pair<Point, Point> {
            // Get screen size
            val windowMetrics = WindowMetricsCalculator.getOrCreate()
                .computeCurrentWindowMetrics(context as Activity)
            var maxX = screenSize.first.toFloat()
            var maxY = screenSize.second.toFloat()
            var minX = 0
            var minY = 0

            // If we have API access, calculate the safe area to draw the overlay
            var cutoutLeft = 0
            var cutoutBottom = 0
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                val insets = context.windowManager.currentWindowMetrics.windowInsets.displayCutout
                if (insets != null) {
                    if (insets.boundingRectTop.bottom != 0 &&
                        insets.boundingRectTop.bottom > maxY / 2
                    ) {
                        maxY = insets.boundingRectTop.bottom.toFloat()
                    }
                    if (insets.boundingRectRight.left != 0 &&
                        insets.boundingRectRight.left > maxX / 2
                    ) {
                        maxX = insets.boundingRectRight.left.toFloat()
                    }

                    minX = insets.boundingRectLeft.right - insets.boundingRectLeft.left
                    minY = insets.boundingRectBottom.top - insets.boundingRectBottom.bottom

                    cutoutLeft = insets.boundingRectRight.right - insets.boundingRectRight.left
                    cutoutBottom = insets.boundingRectTop.top - insets.boundingRectTop.bottom
                }
            }

            // This makes sure that if we have an inset on one side of the screen, we mirror it on
            // the other side. Since removing space from one of the max values messes with the scale,
            // we also have to account for it using our min values.
            if (maxX.toInt() != windowMetrics.bounds.width()) minX += cutoutLeft
            if (maxY.toInt() != windowMetrics.bounds.height()) minY += cutoutBottom
            if (minX > 0 && maxX.toInt() == windowMetrics.bounds.width()) {
                maxX -= (minX * 2)
            } else if (minX > 0) {
                maxX -= minX
            }
            if (minY > 0 && maxY.toInt() == windowMetrics.bounds.height()) {
                maxY -= (minY * 2)
            } else if (minY > 0) {
                maxY -= minY
            }

            return Pair(Point(minX, minY), Point(maxX.toInt(), maxY.toInt()))
        }

        /**
         * Initializes an InputOverlayDrawableButton, given by resId, with all of the
         * parameters set for it to be properly shown on the InputOverlay.
         *
         *
         * This works due to the way the X and Y coordinates are stored within
         * the [SharedPreferences].
         *
         *
         * In the input overlay configuration menu,
         * once a touch event begins and then ends (ie. Organizing the buttons to one's own liking for the overlay).
         * the X and Y coordinates of the button at the END of its touch event
         * (when you remove your finger/stylus from the touchscreen) are then stored in a native .
         *
         * Technically no modifications should need to be performed on the returned
         * InputOverlayDrawableButton. Simply add it to the HashSet of overlay items and wait
         * for Android to call the onDraw method.
         *
         * @param context            The current [Context].
         * @param windowSize         The size of the window to draw the overlay on.
         * @param defaultResId       The resource ID of the [Drawable] to get the [Bitmap] of (Default State).
         * @param pressedResId       The resource ID of the [Drawable] to get the [Bitmap] of (Pressed State).
         * @param buttonId           Identifier for determining what type of button the initialized InputOverlayDrawableButton represents.
         * @param overlayControlData Identifier for determining where a button appears on screen.
         * @param position           The position on screen as represented by an x and y value between 0 and 1.
         * @return An [InputOverlayDrawableButton] with the correct drawing bounds set.
         */
        private fun initializeOverlayButton(
            context: Context,
            windowSize: Pair<Point, Point>,
            defaultResId: Int,
            pressedResId: Int,
            button: NativeButton,
            overlayControlData: OverlayControlData,
            position: Pair<Double, Double>
        ): InputOverlayDrawableButton {
            // Resources handle for fetching the initial Drawable resource.
            val res = context.resources

            // Decide scale based on button preference ID and user preference
            var scale: Float = when (overlayControlData.id) {
                OverlayControl.BUTTON_HOME.id,
                OverlayControl.BUTTON_CAPTURE.id,
                OverlayControl.BUTTON_PLUS.id,
                OverlayControl.BUTTON_MINUS.id -> 0.07f

                OverlayControl.BUTTON_L.id,
                OverlayControl.BUTTON_R.id,
                OverlayControl.BUTTON_ZL.id,
                OverlayControl.BUTTON_ZR.id -> 0.26f

                OverlayControl.BUTTON_STICK_L.id,
                OverlayControl.BUTTON_STICK_R.id -> 0.155f

                else -> 0.11f
            }
            scale *= (IntSetting.OVERLAY_SCALE.getInt() + 50).toFloat()
            scale /= 100f

            // Initialize the InputOverlayDrawableButton.
            val defaultStateBitmap = getBitmap(context, defaultResId, scale)
            val pressedStateBitmap = getBitmap(context, pressedResId, scale)
            val overlayDrawable = InputOverlayDrawableButton(
                res,
                defaultStateBitmap,
                pressedStateBitmap,
                button,
                overlayControlData
            )

            // Get the minimum and maximum coordinates of the screen where the button can be placed.
            val min = windowSize.first
            val max = windowSize.second

            // The X and Y coordinates of the InputOverlayDrawableButton on the InputOverlay.
            // These were set in the input overlay configuration menu.
            val drawableX = (position.first * max.x + min.x).toInt()
            val drawableY = (position.second * max.y + min.y).toInt()
            val width = overlayDrawable.width
            val height = overlayDrawable.height

            // Now set the bounds for the InputOverlayDrawableButton.
            // This will dictate where on the screen (and the what the size) the InputOverlayDrawableButton will be.
            overlayDrawable.setBounds(
                drawableX - (width / 2),
                drawableY - (height / 2),
                drawableX + (width / 2),
                drawableY + (height / 2)
            )

            // Need to set the image's position
            overlayDrawable.setPosition(
                drawableX - (width / 2),
                drawableY - (height / 2)
            )
            overlayDrawable.setOpacity(IntSetting.OVERLAY_OPACITY.getInt() * 255 / 100)
            return overlayDrawable
        }

        /**
         * Initializes an [InputOverlayDrawableDpad]
         *
         * @param context                   The current [Context].
         * @param windowSize                The size of the window to draw the overlay on.
         * @param defaultResId              The [Bitmap] resource ID of the default state.
         * @param pressedOneDirectionResId  The [Bitmap] resource ID of the pressed state in one direction.
         * @param pressedTwoDirectionsResId The [Bitmap] resource ID of the pressed state in two directions.
         * @param position                  The position on screen as represented by an x and y value between 0 and 1.
         * @return The initialized [InputOverlayDrawableDpad]
         */
        private fun initializeOverlayDpad(
            context: Context,
            windowSize: Pair<Point, Point>,
            defaultResId: Int,
            pressedOneDirectionResId: Int,
            pressedTwoDirectionsResId: Int,
            position: Pair<Double, Double>
        ): InputOverlayDrawableDpad {
            // Resources handle for fetching the initial Drawable resource.
            val res = context.resources

            // Decide scale based on button ID and user preference
            var scale = 0.25f
            scale *= (IntSetting.OVERLAY_SCALE.getInt() + 50).toFloat()
            scale /= 100f

            // Initialize the InputOverlayDrawableDpad.
            val defaultStateBitmap =
                getBitmap(context, defaultResId, scale)
            val pressedOneDirectionStateBitmap = getBitmap(context, pressedOneDirectionResId, scale)
            val pressedTwoDirectionsStateBitmap =
                getBitmap(context, pressedTwoDirectionsResId, scale)

            val overlayDrawable = InputOverlayDrawableDpad(
                res,
                defaultStateBitmap,
                pressedOneDirectionStateBitmap,
                pressedTwoDirectionsStateBitmap
            )

            // Get the minimum and maximum coordinates of the screen where the button can be placed.
            val min = windowSize.first
            val max = windowSize.second

            // The X and Y coordinates of the InputOverlayDrawableDpad on the InputOverlay.
            // These were set in the input overlay configuration menu.
            val drawableX = (position.first * max.x + min.x).toInt()
            val drawableY = (position.second * max.y + min.y).toInt()
            val width = overlayDrawable.width
            val height = overlayDrawable.height

            // Now set the bounds for the InputOverlayDrawableDpad.
            // This will dictate where on the screen (and the what the size) the InputOverlayDrawableDpad will be.
            overlayDrawable.setBounds(
                drawableX - (width / 2),
                drawableY - (height / 2),
                drawableX + (width / 2),
                drawableY + (height / 2)
            )

            // Need to set the image's position
            overlayDrawable.setPosition(drawableX - (width / 2), drawableY - (height / 2))
            overlayDrawable.setOpacity(IntSetting.OVERLAY_OPACITY.getInt() * 255 / 100)
            return overlayDrawable
        }

        /**
         * Initializes an [InputOverlayDrawableJoystick]
         *
         * @param context         The current [Context]
         * @param windowSize      The size of the window to draw the overlay on.
         * @param resOuter        Resource ID for the outer image of the joystick (the static image that shows the circular bounds).
         * @param defaultResInner Resource ID for the default inner image of the joystick (the one you actually move around).
         * @param pressedResInner Resource ID for the pressed inner image of the joystick.
         * @param joystick        Identifier for which joystick this is.
         * @param buttonId          Identifier for which joystick button this is.
         * @param overlayControlData Identifier for determining where a button appears on screen.
         * @param position           The position on screen as represented by an x and y value between 0 and 1.
         * @return The initialized [InputOverlayDrawableJoystick].
         */
        private fun initializeOverlayJoystick(
            context: Context,
            windowSize: Pair<Point, Point>,
            resOuter: Int,
            defaultResInner: Int,
            pressedResInner: Int,
            joystick: NativeAnalog,
            button: NativeButton,
            overlayControlData: OverlayControlData,
            position: Pair<Double, Double>
        ): InputOverlayDrawableJoystick {
            // Resources handle for fetching the initial Drawable resource.
            val res = context.resources

            // Decide scale based on user preference
            var scale = 0.3f
            scale *= (IntSetting.OVERLAY_SCALE.getInt() + 50).toFloat()
            scale /= 100f

            // Initialize the InputOverlayDrawableJoystick.
            val bitmapOuter = getBitmap(context, resOuter, scale)
            val bitmapInnerDefault = getBitmap(context, defaultResInner, 1.0f)
            val bitmapInnerPressed = getBitmap(context, pressedResInner, 1.0f)

            // Get the minimum and maximum coordinates of the screen where the button can be placed.
            val min = windowSize.first
            val max = windowSize.second

            // The X and Y coordinates of the InputOverlayDrawableButton on the InputOverlay.
            // These were set in the input overlay configuration menu.
            val drawableX = (position.first * max.x + min.x).toInt()
            val drawableY = (position.second * max.y + min.y).toInt()
            val outerScale = 1.66f

            // Now set the bounds for the InputOverlayDrawableJoystick.
            // This will dictate where on the screen (and the what the size) the InputOverlayDrawableJoystick will be.
            val outerSize = bitmapOuter.width
            val outerRect = Rect(
                drawableX - (outerSize / 2),
                drawableY - (outerSize / 2),
                drawableX + (outerSize / 2),
                drawableY + (outerSize / 2)
            )
            val innerRect =
                Rect(0, 0, (outerSize / outerScale).toInt(), (outerSize / outerScale).toInt())

            // Send the drawableId to the joystick so it can be referenced when saving control position.
            val overlayDrawable = InputOverlayDrawableJoystick(
                res,
                bitmapOuter,
                bitmapInnerDefault,
                bitmapInnerPressed,
                outerRect,
                innerRect,
                joystick,
                button,
                overlayControlData.id
            )

            // Need to set the image's position
            overlayDrawable.setPosition(drawableX, drawableY)
            overlayDrawable.setOpacity(IntSetting.OVERLAY_OPACITY.getInt() * 255 / 100)
            return overlayDrawable
        }
    }
}
