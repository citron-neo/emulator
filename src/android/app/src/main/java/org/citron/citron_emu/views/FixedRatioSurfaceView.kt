// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.views

import android.content.Context
import android.util.AttributeSet
import android.util.Rational
import android.view.SurfaceView

class FixedRatioSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : SurfaceView(context, attrs, defStyleAttr) {
    private var aspectRatio: Float = 0f // (width / height), 0f is a special value for stretch

    /**
     * Sets the desired aspect ratio for this view
     * @param ratio the ratio to force the view to, or null to stretch to fit
     */
    fun setAspectRatio(ratio: Rational?) {
        aspectRatio = ratio?.toFloat() ?: 0f
        requestLayout()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val displayWidth: Float = MeasureSpec.getSize(widthMeasureSpec).toFloat()
        val displayHeight: Float = MeasureSpec.getSize(heightMeasureSpec).toFloat()

        // Safety check: ensure we have valid dimensions
        if (displayWidth <= 0f || displayHeight <= 0f || aspectRatio == 0f) {
            // Fall back to default behavior for stretch or invalid dimensions
            super.onMeasure(widthMeasureSpec, heightMeasureSpec)
            return
        }

        if (aspectRatio > 0f) {
            val displayAspect = displayWidth / displayHeight
            if (displayAspect < aspectRatio) {
                // Max out width
                val halfHeight = displayHeight / 2
                val surfaceHeight = displayWidth / aspectRatio
                val newTop: Float = halfHeight - (surfaceHeight / 2)
                val newBottom: Float = halfHeight + (surfaceHeight / 2)
                super.onMeasure(
                    widthMeasureSpec,
                    MeasureSpec.makeMeasureSpec(
                        newBottom.toInt() - newTop.toInt(),
                        MeasureSpec.EXACTLY
                    )
                )
                return
            } else {
                // Max out height
                val halfWidth = displayWidth / 2
                val surfaceWidth = displayHeight * aspectRatio
                val newLeft: Float = halfWidth - (surfaceWidth / 2)
                val newRight: Float = halfWidth + (surfaceWidth / 2)
                super.onMeasure(
                    MeasureSpec.makeMeasureSpec(
                        newRight.toInt() - newLeft.toInt(),
                        MeasureSpec.EXACTLY
                    ),
                    heightMeasureSpec
                )
                return
            }
        }
        super.onMeasure(widthMeasureSpec, heightMeasureSpec)
    }
}
