// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.views

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import android.os.BatteryManager
import android.util.AttributeSet
import android.util.Log
import android.view.View
import androidx.core.content.ContextCompat
import org.citron.citron_emu.R
import kotlin.math.roundToInt

class ThermalIndicatorView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 18f
        typeface = Typeface.DEFAULT_BOLD
        textAlign = Paint.Align.LEFT
        setShadowLayer(2f, 1f, 1f, Color.BLACK)
    }

    private var batteryTemperature: Float = 0f

    fun updateTemperature(temperature: Float) {
        try {
            batteryTemperature = temperature
            Log.d("ThermalIndicator", "Battery temperature updated: ${batteryTemperature}°C")

            // Update text color based on temperature
            val tempColor = when {
                batteryTemperature < 20f -> Color.parseColor("#87CEEB") // Sky blue
                batteryTemperature < 30f -> Color.WHITE // White
                batteryTemperature < 40f -> Color.parseColor("#FFA500") // Orange
                batteryTemperature < 50f -> Color.parseColor("#FF4500") // Red orange
                else -> Color.parseColor("#FF0000") // Red
            }

            textPaint.color = tempColor

            // Always invalidate to trigger a redraw
            invalidate()
            Log.d("ThermalIndicator", "View invalidated, temperature: ${batteryTemperature}°C")
        } catch (e: Exception) {
            // Fallback in case of any errors
            batteryTemperature = 25f
            Log.e("ThermalIndicator", "Error updating temperature", e)
            invalidate()
        }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val desiredWidth = 100
        val desiredHeight = 30

        val widthMode = MeasureSpec.getMode(widthMeasureSpec)
        val widthSize = MeasureSpec.getSize(widthMeasureSpec)
        val heightMode = MeasureSpec.getMode(heightMeasureSpec)
        val heightSize = MeasureSpec.getSize(heightMeasureSpec)

        val width = when (widthMode) {
            MeasureSpec.EXACTLY -> widthSize
            MeasureSpec.AT_MOST -> minOf(desiredWidth, widthSize)
            else -> desiredWidth
        }

        val height = when (heightMode) {
            MeasureSpec.EXACTLY -> heightSize
            MeasureSpec.AT_MOST -> minOf(desiredHeight, heightSize)
            else -> desiredHeight
        }

        setMeasuredDimension(width, height)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        // Draw simple text-based temperature display
        val tempText = "TEMP: ${batteryTemperature.roundToInt()}°C"
        canvas.drawText(tempText, 8f, height - 8f, textPaint)

        Log.d("ThermalIndicator", "onDraw called - Temperature: $tempText")
    }
}