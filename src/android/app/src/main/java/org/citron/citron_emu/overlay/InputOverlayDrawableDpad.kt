// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.overlay

import android.content.res.Resources
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Rect
import android.graphics.drawable.BitmapDrawable
import android.view.MotionEvent
import org.citron.citron_emu.features.input.NativeInput.ButtonState
import org.citron.citron_emu.features.input.model.NativeButton

/**
 * Custom [BitmapDrawable] that is capable
 * of storing it's own ID.
 *
 * @param res                             [Resources] instance.
 * @param defaultStateBitmap              [Bitmap] of the default state.
 * @param pressedOneDirectionStateBitmap  [Bitmap] of the pressed state in one direction.
 * @param pressedTwoDirectionsStateBitmap [Bitmap] of the pressed state in two direction.
 */
class InputOverlayDrawableDpad(
    res: Resources,
    defaultStateBitmap: Bitmap,
    pressedOneDirectionStateBitmap: Bitmap,
    pressedTwoDirectionsStateBitmap: Bitmap
) {
    /**
     * Gets one of the InputOverlayDrawableDpad's button IDs.
     *
     * @return the requested InputOverlayDrawableDpad's button ID.
     */
    // The ID identifying what type of button this Drawable represents.
    val up = NativeButton.DUp
    val down = NativeButton.DDown
    val left = NativeButton.DLeft
    val right = NativeButton.DRight
    var trackId: Int

    val width: Int
    val height: Int

    private val defaultStateBitmap: BitmapDrawable
    private val pressedOneDirectionStateBitmap: BitmapDrawable
    private val pressedTwoDirectionsStateBitmap: BitmapDrawable

    private var previousTouchX = 0
    private var previousTouchY = 0
    private var controlPositionX = 0
    private var controlPositionY = 0

    private var upButtonState = false
    private var downButtonState = false
    private var leftButtonState = false
    private var rightButtonState = false

    init {
        this.defaultStateBitmap = BitmapDrawable(res, defaultStateBitmap)
        this.pressedOneDirectionStateBitmap = BitmapDrawable(res, pressedOneDirectionStateBitmap)
        this.pressedTwoDirectionsStateBitmap = BitmapDrawable(res, pressedTwoDirectionsStateBitmap)
        width = this.defaultStateBitmap.intrinsicWidth
        height = this.defaultStateBitmap.intrinsicHeight
        trackId = -1
    }

    fun updateStatus(event: MotionEvent, dpad_slide: Boolean): Boolean {
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
            trackId = pointerId
        }
        if (isActionUp) {
            if (trackId != pointerId) {
                return false
            }
            trackId = -1
            upButtonState = false
            downButtonState = false
            leftButtonState = false
            rightButtonState = false
            return true
        }
        if (trackId == -1) {
            return false
        }
        if (!dpad_slide && !isActionDown) {
            return false
        }
        for (i in 0 until event.pointerCount) {
            if (trackId != event.getPointerId(i)) {
                continue
            }

            var touchX = event.getX(i)
            var touchY = event.getY(i)
            var maxY = bounds.bottom.toFloat()
            var maxX = bounds.right.toFloat()
            touchX -= bounds.centerX().toFloat()
            maxX -= bounds.centerX().toFloat()
            touchY -= bounds.centerY().toFloat()
            maxY -= bounds.centerY().toFloat()
            val axisX = touchX / maxX
            val axisY = touchY / maxY
            val oldUpState = upButtonState
            val oldDownState = downButtonState
            val oldLeftState = leftButtonState
            val oldRightState = rightButtonState

            upButtonState = axisY < -VIRT_AXIS_DEADZONE
            downButtonState = axisY > VIRT_AXIS_DEADZONE
            leftButtonState = axisX < -VIRT_AXIS_DEADZONE
            rightButtonState = axisX > VIRT_AXIS_DEADZONE
            return oldUpState != upButtonState ||
                oldDownState != downButtonState ||
                oldLeftState != leftButtonState ||
                oldRightState != rightButtonState
        }
        return false
    }

    fun draw(canvas: Canvas) {
        val px = controlPositionX + width / 2
        val py = controlPositionY + height / 2

        // Pressed up
        if (upButtonState && !leftButtonState && !rightButtonState) {
            pressedOneDirectionStateBitmap.draw(canvas)
            return
        }

        // Pressed down
        if (downButtonState && !leftButtonState && !rightButtonState) {
            canvas.save()
            canvas.rotate(180f, px.toFloat(), py.toFloat())
            pressedOneDirectionStateBitmap.draw(canvas)
            canvas.restore()
            return
        }

        // Pressed left
        if (leftButtonState && !upButtonState && !downButtonState) {
            canvas.save()
            canvas.rotate(270f, px.toFloat(), py.toFloat())
            pressedOneDirectionStateBitmap.draw(canvas)
            canvas.restore()
            return
        }

        // Pressed right
        if (rightButtonState && !upButtonState && !downButtonState) {
            canvas.save()
            canvas.rotate(90f, px.toFloat(), py.toFloat())
            pressedOneDirectionStateBitmap.draw(canvas)
            canvas.restore()
            return
        }

        // Pressed up left
        if (upButtonState && leftButtonState && !rightButtonState) {
            pressedTwoDirectionsStateBitmap.draw(canvas)
            return
        }

        // Pressed up right
        if (upButtonState && !leftButtonState && rightButtonState) {
            canvas.save()
            canvas.rotate(90f, px.toFloat(), py.toFloat())
            pressedTwoDirectionsStateBitmap.draw(canvas)
            canvas.restore()
            return
        }

        // Pressed down right
        if (downButtonState && !leftButtonState && rightButtonState) {
            canvas.save()
            canvas.rotate(180f, px.toFloat(), py.toFloat())
            pressedTwoDirectionsStateBitmap.draw(canvas)
            canvas.restore()
            return
        }

        // Pressed down left
        if (downButtonState && leftButtonState && !rightButtonState) {
            canvas.save()
            canvas.rotate(270f, px.toFloat(), py.toFloat())
            pressedTwoDirectionsStateBitmap.draw(canvas)
            canvas.restore()
            return
        }

        // Not pressed
        defaultStateBitmap.draw(canvas)
    }

    val upStatus: Int
        get() = if (upButtonState) ButtonState.PRESSED else ButtonState.RELEASED
    val downStatus: Int
        get() = if (downButtonState) ButtonState.PRESSED else ButtonState.RELEASED
    val leftStatus: Int
        get() = if (leftButtonState) ButtonState.PRESSED else ButtonState.RELEASED
    val rightStatus: Int
        get() = if (rightButtonState) ButtonState.PRESSED else ButtonState.RELEASED

    fun onConfigureTouch(event: MotionEvent): Boolean {
        val pointerIndex = event.actionIndex
        val fingerPositionX = event.getX(pointerIndex).toInt()
        val fingerPositionY = event.getY(pointerIndex).toInt()

        when (event.action) {
            MotionEvent.ACTION_DOWN -> {
                previousTouchX = fingerPositionX
                previousTouchY = fingerPositionY
            }

            MotionEvent.ACTION_MOVE -> {
                controlPositionX += fingerPositionX - previousTouchX
                controlPositionY += fingerPositionY - previousTouchY
                setBounds(
                    controlPositionX,
                    controlPositionY,
                    width + controlPositionX,
                    height + controlPositionY
                )
                previousTouchX = fingerPositionX
                previousTouchY = fingerPositionY
            }
        }
        return true
    }

    fun setPosition(x: Int, y: Int) {
        controlPositionX = x
        controlPositionY = y
    }

    fun setBounds(left: Int, top: Int, right: Int, bottom: Int) {
        defaultStateBitmap.setBounds(left, top, right, bottom)
        pressedOneDirectionStateBitmap.setBounds(left, top, right, bottom)
        pressedTwoDirectionsStateBitmap.setBounds(left, top, right, bottom)
    }

    fun setOpacity(value: Int) {
        defaultStateBitmap.alpha = value
        pressedOneDirectionStateBitmap.alpha = value
        pressedTwoDirectionsStateBitmap.alpha = value
    }

    val bounds: Rect
        get() = defaultStateBitmap.bounds

    companion object {
        const val VIRT_AXIS_DEADZONE = 0.5f
    }
}
