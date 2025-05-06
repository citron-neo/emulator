// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.overlay

import android.content.res.Resources
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Rect
import android.graphics.drawable.BitmapDrawable
import android.view.MotionEvent
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt
import org.citron.citron_emu.features.input.NativeInput.ButtonState
import org.citron.citron_emu.features.input.model.NativeAnalog
import org.citron.citron_emu.features.input.model.NativeButton
import org.citron.citron_emu.features.settings.model.BooleanSetting

/**
 * Custom [BitmapDrawable] that is capable
 * of storing it's own ID.
 *
 * @param res                [Resources] instance.
 * @param bitmapOuter        [Bitmap] which represents the outer non-movable part of the joystick.
 * @param bitmapInnerDefault [Bitmap] which represents the default inner movable part of the joystick.
 * @param bitmapInnerPressed [Bitmap] which represents the pressed inner movable part of the joystick.
 * @param rectOuter          [Rect] which represents the outer joystick bounds.
 * @param rectInner          [Rect] which represents the inner joystick bounds.
 * @param joystick           The [NativeAnalog] this Drawable represents.
 * @param button             The [NativeButton] this Drawable represents.
 */
class InputOverlayDrawableJoystick(
    res: Resources,
    bitmapOuter: Bitmap,
    bitmapInnerDefault: Bitmap,
    bitmapInnerPressed: Bitmap,
    rectOuter: Rect,
    rectInner: Rect,
    val joystick: NativeAnalog,
    val button: NativeButton,
    val prefId: String
) {
    // The ID value what motion event is tracking
    var trackId = -1

    var xAxis = 0f
    private var yAxis = 0f

    val width: Int
    val height: Int

    private var opacity: Int = 0

    private var virtBounds: Rect
    private var origBounds: Rect

    private val outerBitmap: BitmapDrawable
    private val defaultStateInnerBitmap: BitmapDrawable
    private val pressedStateInnerBitmap: BitmapDrawable

    private var previousTouchX = 0
    private var previousTouchY = 0
    var controlPositionX = 0
    var controlPositionY = 0

    private val boundsBoxBitmap: BitmapDrawable

    private var pressedState = false

    // TODO: Add button support
    val buttonStatus: Int
        get() = ButtonState.RELEASED
    var bounds: Rect
        get() = outerBitmap.bounds
        set(bounds) {
            outerBitmap.bounds = bounds
        }

    // Nintendo joysticks have y axis inverted
    val realYAxis: Float
        get() = -yAxis

    private val currentStateBitmapDrawable: BitmapDrawable
        get() = if (pressedState) pressedStateInnerBitmap else defaultStateInnerBitmap

    init {
        outerBitmap = BitmapDrawable(res, bitmapOuter)
        defaultStateInnerBitmap = BitmapDrawable(res, bitmapInnerDefault)
        pressedStateInnerBitmap = BitmapDrawable(res, bitmapInnerPressed)
        boundsBoxBitmap = BitmapDrawable(res, bitmapOuter)
        width = bitmapOuter.width
        height = bitmapOuter.height
        bounds = rectOuter
        defaultStateInnerBitmap.bounds = rectInner
        pressedStateInnerBitmap.bounds = rectInner
        virtBounds = bounds
        origBounds = outerBitmap.copyBounds()
        boundsBoxBitmap.alpha = 0
        boundsBoxBitmap.bounds = virtBounds
        setInnerBounds()
    }

    fun draw(canvas: Canvas?) {
        outerBitmap.draw(canvas!!)
        currentStateBitmapDrawable.draw(canvas)
        boundsBoxBitmap.draw(canvas)
    }

    fun updateStatus(event: MotionEvent): Boolean {
        val pointerIndex = event.actionIndex
        val xPosition = event.getX(pointerIndex).toInt()
        val yPosition = event.getY(pointerIndex).toInt()
        val pointerId = event.getPointerId(pointerIndex)
        val motionEvent = event.action and MotionEvent.ACTION_MASK
        val isActionDown =
            motionEvent == MotionEvent.ACTION_DOWN || motionEvent == MotionEvent.ACTION_POINTER_DOWN
        val isActionUp =
            motionEvent == MotionEvent.ACTION_UP || motionEvent == MotionEvent.ACTION_POINTER_UP

        if (isActionDown) {
            if (!bounds.contains(xPosition, yPosition)) {
                return false
            }
            pressedState = true
            outerBitmap.alpha = 0
            boundsBoxBitmap.alpha = opacity
            if (BooleanSetting.JOYSTICK_REL_CENTER.getBoolean()) {
                virtBounds.offset(
                    xPosition - virtBounds.centerX(),
                    yPosition - virtBounds.centerY()
                )
            }
            boundsBoxBitmap.bounds = virtBounds
            trackId = pointerId
        }

        if (isActionUp) {
            if (trackId != pointerId) {
                return false
            }
            pressedState = false
            xAxis = 0.0f
            yAxis = 0.0f
            outerBitmap.alpha = opacity
            boundsBoxBitmap.alpha = 0
            virtBounds = Rect(
                origBounds.left,
                origBounds.top,
                origBounds.right,
                origBounds.bottom
            )
            bounds = Rect(
                origBounds.left,
                origBounds.top,
                origBounds.right,
                origBounds.bottom
            )
            setInnerBounds()
            trackId = -1
            return true
        }

        if (trackId == -1) return false

        for (i in 0 until event.pointerCount) {
            if (trackId != event.getPointerId(i)) {
                continue
            }
            var touchX = event.getX(i)
            var touchY = event.getY(i)
            var maxY = virtBounds.bottom.toFloat()
            var maxX = virtBounds.right.toFloat()
            touchX -= virtBounds.centerX().toFloat()
            maxX -= virtBounds.centerX().toFloat()
            touchY -= virtBounds.centerY().toFloat()
            maxY -= virtBounds.centerY().toFloat()
            val axisX = touchX / maxX
            val axisY = touchY / maxY
            val oldXAxis = xAxis
            val oldYAxis = yAxis

            // Clamp the circle pad input to a circle
            val angle = atan2(axisY.toDouble(), axisX.toDouble()).toFloat()
            var radius = sqrt((axisX * axisX + axisY * axisY).toDouble()).toFloat()
            if (radius > 1.0f) {
                radius = 1.0f
            }
            xAxis = cos(angle.toDouble()).toFloat() * radius
            yAxis = sin(angle.toDouble()).toFloat() * radius
            setInnerBounds()
            return oldXAxis != xAxis && oldYAxis != yAxis
        }
        return false
    }

    fun onConfigureTouch(event: MotionEvent): Boolean {
        val pointerIndex = event.actionIndex
        val fingerPositionX = event.getX(pointerIndex).toInt()
        val fingerPositionY = event.getY(pointerIndex).toInt()

        when (event.action) {
            MotionEvent.ACTION_DOWN -> {
                previousTouchX = fingerPositionX
                previousTouchY = fingerPositionY
                controlPositionX = fingerPositionX - (width / 2)
                controlPositionY = fingerPositionY - (height / 2)
            }

            MotionEvent.ACTION_MOVE -> {
                controlPositionX += fingerPositionX - previousTouchX
                controlPositionY += fingerPositionY - previousTouchY
                bounds = Rect(
                    controlPositionX,
                    controlPositionY,
                    outerBitmap.intrinsicWidth + controlPositionX,
                    outerBitmap.intrinsicHeight + controlPositionY
                )
                virtBounds = Rect(
                    controlPositionX,
                    controlPositionY,
                    outerBitmap.intrinsicWidth + controlPositionX,
                    outerBitmap.intrinsicHeight + controlPositionY
                )
                setInnerBounds()
                bounds = Rect(
                    Rect(
                        controlPositionX,
                        controlPositionY,
                        outerBitmap.intrinsicWidth + controlPositionX,
                        outerBitmap.intrinsicHeight + controlPositionY
                    )
                )
                previousTouchX = fingerPositionX
                previousTouchY = fingerPositionY
            }
        }
        origBounds = outerBitmap.copyBounds()
        return true
    }

    private fun setInnerBounds() {
        var x = virtBounds.centerX() + (xAxis * (virtBounds.width() / 2)).toInt()
        var y = virtBounds.centerY() + (yAxis * (virtBounds.height() / 2)).toInt()
        if (x > virtBounds.centerX() + virtBounds.width() / 2) {
            x =
                virtBounds.centerX() + virtBounds.width() / 2
        }
        if (x < virtBounds.centerX() - virtBounds.width() / 2) {
            x =
                virtBounds.centerX() - virtBounds.width() / 2
        }
        if (y > virtBounds.centerY() + virtBounds.height() / 2) {
            y =
                virtBounds.centerY() + virtBounds.height() / 2
        }
        if (y < virtBounds.centerY() - virtBounds.height() / 2) {
            y =
                virtBounds.centerY() - virtBounds.height() / 2
        }
        val width = pressedStateInnerBitmap.bounds.width() / 2
        val height = pressedStateInnerBitmap.bounds.height() / 2
        defaultStateInnerBitmap.setBounds(
            x - width,
            y - height,
            x + width,
            y + height
        )
        pressedStateInnerBitmap.bounds = defaultStateInnerBitmap.bounds
    }

    fun setPosition(x: Int, y: Int) {
        controlPositionX = x
        controlPositionY = y
    }

    fun setOpacity(value: Int) {
        opacity = value

        defaultStateInnerBitmap.alpha = value
        pressedStateInnerBitmap.alpha = value

        if (trackId == -1) {
            outerBitmap.alpha = value
            boundsBoxBitmap.alpha = 0
        } else {
            outerBitmap.alpha = 0
            boundsBoxBitmap.alpha = value
        }
    }
}
