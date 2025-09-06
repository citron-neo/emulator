// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.views

import android.content.Context
import android.graphics.*
import android.util.AttributeSet
import android.util.Log
import android.view.MotionEvent
import android.view.View
import kotlin.math.*

class ShaderBuildingOverlayView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    private val backgroundPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#CC000000") // More opaque background
        style = Paint.Style.FILL
    }

    private val borderPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#FF9800") // Orange border for shader building
        style = Paint.Style.STROKE
        strokeWidth = 2f
    }

    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 18f
        typeface = Typeface.DEFAULT_BOLD
        textAlign = Paint.Align.LEFT
        setShadowLayer(2f, 1f, 1f, Color.BLACK)
    }

    private val smallTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 14f
        typeface = Typeface.DEFAULT
        textAlign = Paint.Align.LEFT
        setShadowLayer(2f, 1f, 1f, Color.BLACK)
    }

    private val graphPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#4CAF50") // Green for good performance
        style = Paint.Style.STROKE
        strokeWidth = 3f
    }

    private val graphFillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#4CAF50")
        style = Paint.Style.FILL
        alpha = 60
    }

    private val graphBackgroundPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#40000000")
        style = Paint.Style.FILL
    }

    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#40FFFFFF")
        style = Paint.Style.STROKE
        strokeWidth = 1f
    }

    private val backgroundRect = RectF()
    private val graphRect = RectF()

    // Performance data
    private var currentFps: Float = 0f
    private var currentFrameTime: Float = 0f
    private var emulationSpeed: Float = 0f
    private var shadersBuilding: Int = 0

    // Graph data
    private val frameTimeHistory = mutableListOf<Float>()
    private val maxHistorySize = 120 // 2 seconds at 60 FPS
    private var minFrameTime: Float = 16.67f
    private var maxFrameTime: Float = 16.67f
    private var avgFrameTime: Float = 16.67f

    // Animation
    private var animationProgress: Float = 0f
    private var isAnimating: Boolean = false

    // Touch handling for dragging
    private var lastTouchX: Float = 0f
    private var lastTouchY: Float = 0f
    private var isDragging: Boolean = false

    fun updatePerformanceStats(fps: Float, frameTime: Float, speed: Float, shaders: Int) {
        try {
            currentFps = fps
            currentFrameTime = frameTime
            emulationSpeed = speed
            shadersBuilding = shaders

            // Update frame time history for graph
            if (frameTime > 0f) {
                frameTimeHistory.add(frameTime)
                if (frameTimeHistory.size > maxHistorySize) {
                    frameTimeHistory.removeAt(0)
                }

                // Update min/max/avg
                if (frameTimeHistory.isNotEmpty()) {
                    minFrameTime = frameTimeHistory.minOrNull() ?: 16.67f
                    maxFrameTime = frameTimeHistory.maxOrNull() ?: 16.67f
                    avgFrameTime = frameTimeHistory.average().toFloat()
                }
            }

            // Start animation if shaders are building
            if (shadersBuilding > 0 && !isAnimating) {
                isAnimating = true
                post(object : Runnable {
                    override fun run() {
                        if (isAnimating) {
                            animationProgress += 0.1f
                            if (animationProgress >= 1f) {
                                animationProgress = 0f
                            }
                            invalidate()
                            postDelayed(this, 50) // 20 FPS animation
                        }
                    }
                })
            } else if (shadersBuilding == 0) {
                isAnimating = false
            }

            invalidate()
        } catch (e: Exception) {
            Log.e("ShaderBuildingOverlay", "Error updating performance stats", e)
        }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val desiredWidth = 280
        val desiredHeight = 140

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
        graphRect.set(20f, 80f, w - 20f, h - 20f)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        // Draw background with rounded corners
        canvas.drawRoundRect(backgroundRect, 12f, 12f, backgroundPaint)
        canvas.drawRoundRect(backgroundRect, 12f, 12f, borderPaint)

        val padding = 16f
        var yOffset = padding + 20f
        val lineHeight = 22f

        // Draw title
        textPaint.color = Color.WHITE
        canvas.drawText("CITRON Performance", padding, yOffset, textPaint)
        yOffset += lineHeight

        // Draw FPS
        val fpsColor = when {
            currentFps >= 55f -> Color.parseColor("#4CAF50") // Green
            currentFps >= 45f -> Color.parseColor("#FF9800") // Orange
            currentFps >= 30f -> Color.parseColor("#FF5722") // Red orange
            else -> Color.parseColor("#F44336") // Red
        }
        textPaint.color = fpsColor
        canvas.drawText("FPS: ${currentFps.roundToInt()}", padding, yOffset, textPaint)
        yOffset += lineHeight - 4f

        // Draw frame time
        smallTextPaint.color = fpsColor
        canvas.drawText("Frame: ${String.format("%.1f", currentFrameTime)} ms", padding, yOffset, smallTextPaint)
        yOffset += lineHeight - 4f

        // Draw emulation speed
        canvas.drawText("Speed: ${emulationSpeed.roundToInt()}%", padding, yOffset, smallTextPaint)
        yOffset += lineHeight - 4f

        // Draw shader building info with animation
        if (shadersBuilding > 0) {
            val shaderColor = Color.parseColor("#FF9800") // Orange
            smallTextPaint.color = shaderColor

            // Animated dots
            val dots = when ((animationProgress * 3).toInt()) {
                0 -> "Building: $shadersBuilding shader(s)"
                1 -> "Building: $shadersBuilding shader(s) ."
                2 -> "Building: $shadersBuilding shader(s) .."
                else -> "Building: $shadersBuilding shader(s) ..."
            }
            canvas.drawText(dots, padding, yOffset, smallTextPaint)
        }

        // Draw performance graph
        drawPerformanceGraph(canvas)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.action) {
            MotionEvent.ACTION_DOWN -> {
                lastTouchX = event.x
                lastTouchY = event.y
                isDragging = true
                borderPaint.color = Color.parseColor("#FF5722") // Red border when dragging
                invalidate()
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (isDragging) {
                    val deltaX = event.x - lastTouchX
                    val deltaY = event.y - lastTouchY

                    // Update position with boundary constraints
                    val newX = (x + deltaX).coerceIn(0f, (parent as View).width - width.toFloat())
                    val newY = (y + deltaY).coerceIn(0f, (parent as View).height - height.toFloat())

                    x = newX
                    y = newY

                    lastTouchX = event.x
                    lastTouchY = event.y
                    invalidate()
                }
                return true
            }
            MotionEvent.ACTION_UP -> {
                isDragging = false
                borderPaint.color = Color.parseColor("#FF9800") // Orange border when not dragging
                invalidate()
                return true
            }
        }
        return super.onTouchEvent(event)
    }

    fun resetPosition() {
        x = 0f
        y = 0f
        invalidate()
    }

    private fun drawPerformanceGraph(canvas: Canvas) {
        if (frameTimeHistory.isEmpty()) return

        // Draw graph background
        canvas.drawRoundRect(graphRect, 8f, 8f, graphBackgroundPaint)

        // Draw grid lines
        val gridSpacing = graphRect.height() / 4
        for (i in 1..3) {
            val y = graphRect.top + i * gridSpacing
            canvas.drawLine(graphRect.left, y, graphRect.right, y, gridPaint)
        }

        // Draw frame time line
        val path = Path()
        val pointSpacing = graphRect.width() / (frameTimeHistory.size - 1)

        for (i in frameTimeHistory.indices) {
            val x = graphRect.left + i * pointSpacing
            val normalizedFrameTime = (frameTimeHistory[i] - minFrameTime) / (maxFrameTime - minFrameTime)
            val y = graphRect.bottom - (normalizedFrameTime * graphRect.height())

            if (i == 0) {
                path.moveTo(x, y)
            } else {
                path.lineTo(x, y)
            }
        }

        // Draw fill under the line
        val fillPath = Path(path)
        fillPath.lineTo(graphRect.right, graphRect.bottom)
        fillPath.lineTo(graphRect.left, graphRect.bottom)
        fillPath.close()
        canvas.drawPath(fillPath, graphFillPaint)

        // Draw the line
        canvas.drawPath(path, graphPaint)

        // Draw statistics
        val statsText = "Min: ${String.format("%.1f", minFrameTime)}ms | " +
                       "Avg: ${String.format("%.1f", avgFrameTime)}ms | " +
                       "Max: ${String.format("%.1f", maxFrameTime)}ms"
        smallTextPaint.color = Color.WHITE
        canvas.drawText(statsText, graphRect.left, graphRect.bottom + 16f, smallTextPaint)
    }
}
