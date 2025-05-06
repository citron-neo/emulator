// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.adapters

import android.annotation.SuppressLint
import androidx.recyclerview.widget.AsyncDifferConfig
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import org.citron.citron_emu.viewholder.AbstractViewHolder
import androidx.recyclerview.widget.RecyclerView

/**
 * Generic adapter that implements an [AsyncDifferConfig] and covers some of the basic boilerplate
 * code used in every [RecyclerView].
 * Type assigned to [Model] must inherit from [Object] in order to be compared properly.
 * @param exact Decides whether each item will be compared by reference or by their contents
 */
abstract class AbstractDiffAdapter<Model : Any, Holder : AbstractViewHolder<Model>>(
    exact: Boolean = true
) : ListAdapter<Model, Holder>(AsyncDifferConfig.Builder(DiffCallback<Model>(exact)).build()) {
    override fun onBindViewHolder(holder: Holder, position: Int) =
        holder.bind(currentList[position])

    private class DiffCallback<Model>(val exact: Boolean) : DiffUtil.ItemCallback<Model>() {
        override fun areItemsTheSame(oldItem: Model & Any, newItem: Model & Any): Boolean {
            if (exact) {
                return oldItem === newItem
            }
            return oldItem == newItem
        }

        @SuppressLint("DiffUtilEquals")
        override fun areContentsTheSame(oldItem: Model & Any, newItem: Model & Any): Boolean {
            return oldItem == newItem
        }
    }
}
