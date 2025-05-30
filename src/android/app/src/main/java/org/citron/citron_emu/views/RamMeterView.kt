// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.views

import android.app.ActivityManager
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import android.util.AttributeSet
import android.util.Log
import android.view.View
import kotlin.math.roundToInt

class RamMeterView @JvmOverloads constructor(
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
        textSize = 20f
        typeface = Typeface.DEFAULT_BOLD
        textAlign = Paint.Align.CENTER
        setShadowLayer(2f, 1f, 1f, Color.BLACK)
    }

    private val smallTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 14f
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

    private val meterBackgroundPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#40FFFFFF") // Semi-transparent white
        style = Paint.Style.FILL
    }

    private val meterFillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.GREEN
        style = Paint.Style.FILL
    }

    private var ramUsagePercent: Float = 0f
    private var usedRamMB: Long = 0L
    private var totalRamMB: Long = 0L
    private var ramIcon: String = "🧠"

    private val backgroundRect = RectF()
    private val meterBackgroundRect = RectF()
    private val meterFillRect = RectF()

    fun updateRamUsage() {
        try {
            val activityManager = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
            val memoryInfo = ActivityManager.MemoryInfo()
            activityManager.getMemoryInfo(memoryInfo)

            totalRamMB = memoryInfo.totalMem / (1024 * 1024)
            val availableRamMB = memoryInfo.availMem / (1024 * 1024)
            usedRamMB = totalRamMB - availableRamMB
            ramUsagePercent = (usedRamMB.toFloat() / totalRamMB.toFloat()) * 100f

            // Update meter color based on usage
            val meterColor = when {
                ramUsagePercent < 50f -> Color.parseColor("#4CAF50") // Green
                ramUsagePercent < 75f -> Color.parseColor("#FF9800") // Orange
                ramUsagePercent < 90f -> Color.parseColor("#FF5722") // Red orange
                else -> Color.parseColor("#F44336") // Red
            }

            meterFillPaint.color = meterColor
            textPaint.color = meterColor
            smallTextPaint.color = meterColor
            borderPaint.color = meterColor

            // Update icon based on usage
            ramIcon = when {
                ramUsagePercent < 50f -> "🧠" // Normal brain
                ramUsagePercent < 75f -> "⚡" // Warning
                ramUsagePercent < 90f -> "🔥" // Hot
                else -> "💥" // Critical
            }

            invalidate()
            Log.d("RamMeter", "RAM usage updated: ${ramUsagePercent.roundToInt()}% (${usedRamMB}MB/${totalRamMB}MB)")
        } catch (e: Exception) {
            Log.e("RamMeter", "Error updating RAM usage", e)
            ramUsagePercent = 0f
            usedRamMB = 0L
            totalRamMB = 0L
            invalidate()
        }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val desiredWidth = 140
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

        // Setup meter rectangles - compact horizontal bar at the bottom
        val meterLeft = 30f
        val meterTop = h - 18f
        val meterRight = w - 10f
        val meterBottom = h - 10f

        meterBackgroundRect.set(meterLeft, meterTop, meterRight, meterBottom)
        meterFillRect.set(meterLeft, meterTop, meterRight, meterBottom)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        // Draw background with rounded corners
        canvas.drawRoundRect(backgroundRect, 12f, 12f, backgroundPaint)
        canvas.drawRoundRect(backgroundRect, 12f, 12f, borderPaint)

        val centerX = width / 2f
        val centerY = height / 2f

        // Draw RAM icon on the left
        canvas.drawText(ramIcon, 18f, centerY - 8f, iconPaint)

        // Draw percentage text at the top center
        val percentText = "${ramUsagePercent.roundToInt()}%"
        canvas.drawText(percentText, centerX, centerY - 8f, textPaint)

        // Draw memory usage text below percentage
        val usedGB = usedRamMB / 1024f
        val totalGB = totalRamMB / 1024f
        val memoryText = if (totalGB >= 1.0f) {
            "%.1fGB/%.1fGB".format(usedGB, totalGB)
        } else {
            "${usedRamMB}MB/${totalRamMB}MB"
        }
        canvas.drawText(memoryText, centerX, centerY + 8f, smallTextPaint)

        // Draw RAM meter background at the bottom
        canvas.drawRoundRect(meterBackgroundRect, 4f, 4f, meterBackgroundPaint)

        // Draw RAM meter fill
        val fillWidth = meterBackgroundRect.width() * (ramUsagePercent / 100f)
        meterFillRect.right = meterBackgroundRect.left + fillWidth
        canvas.drawRoundRect(meterFillRect, 4f, 4f, meterFillPaint)

        Log.d("RamMeter", "onDraw called - Usage: $percentText, Memory: $memoryText")
    }
}