// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.utils

import android.text.TextUtils
import android.view.View
import android.view.ViewGroup
import android.widget.TextView

object ViewUtils {
    fun showView(view: View, length: Long = 300) {
        view.apply {
            alpha = 0f
            visibility = View.VISIBLE
            isClickable = true
        }.animate().apply {
            duration = length
            alpha(1f)
        }.start()
    }

    fun hideView(view: View, length: Long = 300) {
        if (view.visibility == View.INVISIBLE) {
            return
        }

        view.apply {
            alpha = 1f
            isClickable = false
        }.animate().apply {
            duration = length
            alpha(0f)
        }.withEndAction {
            view.visibility = View.INVISIBLE
        }.start()
    }

    fun View.updateMargins(
        left: Int = -1,
        top: Int = -1,
        right: Int = -1,
        bottom: Int = -1
    ) {
        val layoutParams = this.layoutParams as ViewGroup.MarginLayoutParams
        layoutParams.apply {
            if (left != -1) {
                leftMargin = left
            }
            if (top != -1) {
                topMargin = top
            }
            if (right != -1) {
                rightMargin = right
            }
            if (bottom != -1) {
                bottomMargin = bottom
            }
        }
        this.layoutParams = layoutParams
    }

    /**
     * Shows or hides a view.
     * @param visible Whether a view will be made View.VISIBLE or View.INVISIBLE/GONE.
     * @param gone Optional parameter for hiding a view. Uses View.GONE if true and View.INVISIBLE otherwise.
     */
    fun View.setVisible(visible: Boolean, gone: Boolean = true) {
        visibility = if (visible) {
            View.VISIBLE
        } else {
            if (gone) {
                View.GONE
            } else {
                View.INVISIBLE
            }
        }
    }

    /**
     * Starts a marquee on some text.
     * @param delay Optional parameter for changing the start delay. 3 seconds of delay by default.
     */
    fun TextView.marquee(delay: Long = 3000) {
        ellipsize = null
        marqueeRepeatLimit = -1
        isSingleLine = true
        postDelayed({
            ellipsize = TextUtils.TruncateAt.MARQUEE
            isSelected = true
        }, delay)
    }
}
