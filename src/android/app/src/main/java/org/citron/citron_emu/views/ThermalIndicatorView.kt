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

    private val backgroundPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#80000000") // Semi-transparent black
        style = Paint.Style.FILL
    }

    private val borderPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        style = Paint.Style.STROKE
        strokeWidth = 2f
    }

    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 22f
        typeface = Typeface.DEFAULT_BOLD
        textAlign = Paint.Align.CENTER
        setShadowLayer(2f, 1f, 1f, Color.BLACK)
    }

    private val smallTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 16f
        typeface = Typeface.DEFAULT
        textAlign = Paint.Align.CENTER
        setShadowLayer(2f, 1f, 1f, Color.BLACK)
    }

    private val iconPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 24f
        textAlign = Paint.Align.CENTER
        setShadowLayer(2f, 1f, 1f, Color.BLACK)
    }

    private var batteryTemperature: Float = 0f
    private var thermalStatus: String = "🌡️"

    private val backgroundRect = RectF()

    fun updateTemperature(temperature: Float) {
        try {
            batteryTemperature = temperature
            Log.d("ThermalIndicator", "Battery temperature updated: ${batteryTemperature}°C")

            // Update thermal status icon based on temperature
            thermalStatus = when {
                batteryTemperature < 20f -> "❄️"  // Cold
                batteryTemperature < 30f -> "🌡️"  // Normal
                batteryTemperature < 40f -> "🔥"  // Warm
                batteryTemperature < 50f -> "🥵"  // Hot
                else -> "☢️"  // Critical
            }

            // Update text color based on temperature
            val tempColor = when {
                batteryTemperature < 20f -> Color.parseColor("#87CEEB") // Sky blue
                batteryTemperature < 30f -> Color.WHITE // White
                batteryTemperature < 40f -> Color.parseColor("#FFA500") // Orange
                batteryTemperature < 50f -> Color.parseColor("#FF4500") // Red orange
                else -> Color.parseColor("#FF0000") // Red
            }

            textPaint.color = tempColor
            smallTextPaint.color = tempColor
            borderPaint.color = tempColor

            // Always invalidate to trigger a redraw
            invalidate()
            Log.d("ThermalIndicator", "View invalidated, temperature: ${batteryTemperature}°C, status: $thermalStatus")
        } catch (e: Exception) {
            // Fallback in case of any errors
            batteryTemperature = 25f
            thermalStatus = "🌡️"
            Log.e("ThermalIndicator", "Error updating temperature", e)
            invalidate()
        }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val desiredWidth = 120
        val desiredHeight = 60

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

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        backgroundRect.set(4f, 4f, w - 4f, h - 4f)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        // Draw background with rounded corners
        canvas.drawRoundRect(backgroundRect, 12f, 12f, backgroundPaint)
        canvas.drawRoundRect(backgroundRect, 12f, 12f, borderPaint)

        val centerX = width / 2f
        val centerY = height / 2f

        // Draw thermal icon on the left
        canvas.drawText(thermalStatus, 18f, centerY - 8f, iconPaint)

        // Draw temperature in Celsius (main temperature)
        val celsiusText = "${batteryTemperature.roundToInt()}°C"
        canvas.drawText(celsiusText, centerX, centerY - 8f, textPaint)

        // Draw temperature in Fahrenheit (smaller, below)
        val fahrenheit = (batteryTemperature * 9f / 5f + 32f).roundToInt()
        val fahrenheitText = "${fahrenheit}°F"
        canvas.drawText(fahrenheitText, centerX, centerY + 12f, smallTextPaint)

        Log.d("ThermalIndicator", "onDraw called - Celsius: $celsiusText, Fahrenheit: $fahrenheitText")
    }
}