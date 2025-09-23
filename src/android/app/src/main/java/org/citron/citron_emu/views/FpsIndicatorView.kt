// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.views

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

class FpsIndicatorView @JvmOverloads constructor(
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

    private var currentFps: Float = 0f

    fun updateFps(fps: Float) {
        try {
            currentFps = fps
            Log.d("FpsIndicator", "FPS updated: $currentFps")

            // Update color based on FPS performance
            val fpsColor = when {
                currentFps >= 55f -> Color.parseColor("#4CAF50") // Green - Good performance
                currentFps >= 45f -> Color.parseColor("#FF9800") // Orange - Moderate performance
                currentFps >= 30f -> Color.parseColor("#FF5722") // Red orange - Poor performance
                else -> Color.parseColor("#F44336") // Red - Very poor performance
            }

            textPaint.color = fpsColor

            // Always invalidate to trigger a redraw
            invalidate()
            Log.d("FpsIndicator", "View invalidated, FPS: $currentFps")
        } catch (e: Exception) {
            // Fallback in case of any errors
            currentFps = 0f
            Log.e("FpsIndicator", "Error updating FPS", e)
            invalidate()
        }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val desiredWidth = 80
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

        // Draw simple text-based FPS display
        val fpsText = "FPS: ${currentFps.roundToInt()}"
        canvas.drawText(fpsText, 8f, height - 8f, textPaint)

        Log.d("FpsIndicator", "onDraw called - FPS: $fpsText")
    }
}