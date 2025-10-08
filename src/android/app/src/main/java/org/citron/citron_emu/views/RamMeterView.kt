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

    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 18f
        typeface = Typeface.DEFAULT_BOLD
        textAlign = Paint.Align.LEFT
        setShadowLayer(2f, 1f, 1f, Color.BLACK)
    }

    private var ramUsagePercent: Float = 0f
    private var usedRamMB: Long = 0L
    private var totalRamMB: Long = 0L

    fun updateRamUsage() {
        try {
            // Safety check: ensure view is attached and has valid dimensions
            if (!isAttachedToWindow || width <= 0 || height <= 0) {
                Log.w("RamMeter", "View not ready for update (attached: $isAttachedToWindow, width: $width, height: $height)")
                return
            }

            val activityManager = context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager
            if (activityManager == null) {
                Log.e("RamMeter", "ActivityManager service not available")
                return
            }

            val memoryInfo = ActivityManager.MemoryInfo()
            activityManager.getMemoryInfo(memoryInfo)

            totalRamMB = memoryInfo.totalMem / (1024 * 1024)
            val availableRamMB = memoryInfo.availMem / (1024 * 1024)
            usedRamMB = totalRamMB - availableRamMB
            ramUsagePercent = (usedRamMB.toFloat() / totalRamMB.toFloat()) * 100f

            // Update text color based on usage
            val ramColor = when {
                ramUsagePercent < 50f -> Color.parseColor("#4CAF50") // Green
                ramUsagePercent < 75f -> Color.parseColor("#FF9800") // Orange
                ramUsagePercent < 90f -> Color.parseColor("#FF5722") // Red orange
                else -> Color.parseColor("#F44336") // Red
            }

            textPaint.color = ramColor

            invalidate()
            Log.d("RamMeter", "RAM usage updated: ${ramUsagePercent.roundToInt()}% (${usedRamMB}MB/${totalRamMB}MB)")
        } catch (e: Exception) {
            Log.e("RamMeter", "Error updating RAM usage", e)
            ramUsagePercent = 0f
            usedRamMB = 0L
            totalRamMB = 0L
        }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val desiredWidth = 120
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

        // Safety check: ensure view has valid dimensions
        if (width <= 0 || height <= 0) {
            Log.w("RamMeter", "onDraw called with invalid dimensions (width: $width, height: $height)")
            return
        }

        try {
            // Draw simple text-based RAM display
            val usedGB = usedRamMB / 1024f
            val totalGB = totalRamMB / 1024f
            val ramText = if (totalGB >= 1.0f) {
                "RAM: ${ramUsagePercent.roundToInt()}% (%.1fGB/%.1fGB)".format(usedGB, totalGB)
            } else {
                "RAM: ${ramUsagePercent.roundToInt()}% (${usedRamMB}MB/${totalRamMB}MB)"
            }
            canvas.drawText(ramText, 8f, height - 8f, textPaint)

            Log.d("RamMeter", "onDraw called - RAM: $ramText")
        } catch (e: Exception) {
            Log.e("RamMeter", "Error in onDraw", e)
        }
    }
}