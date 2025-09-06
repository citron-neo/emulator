// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.adapters

import android.annotation.SuppressLint
import androidx.recyclerview.widget.RecyclerView
import org.citron.citron_emu.viewholder.AbstractViewHolder

/**
 * Generic list class meant to take care of basic lists
 * @param currentList The list to show initially
 */
abstract class AbstractListAdapter<Model : Any, Holder : AbstractViewHolder<Model>>(
    open var currentList: List<Model>
) : RecyclerView.Adapter<Holder>() {
    override fun onBindViewHolder(holder: Holder, position: Int) =
        holder.bind(currentList[position])

    override fun getItemCount(): Int = currentList.size

    /**
     * Adds an item to [currentList] and notifies the underlying adapter of the change. If no parameter
     * is passed in for position, [item] is added to the end of the list. Invokes [callback] last.
     * @param item The item to add to the list
     * @param position Index where [item] will be added
     * @param callback Lambda that's called at the end of the list changes and has the added list
     * position passed in as a parameter
     */
    open fun addItem(item: Model, position: Int = -1, callback: ((position: Int) -> Unit)? = null) {
        val newList = currentList.toMutableList()
        val positionToUpdate: Int
        if (position == -1) {
            newList.add(item)
            currentList = newList
            positionToUpdate = currentList.size - 1
        } else {
            newList.add(position, item)
            currentList = newList
            positionToUpdate = position
        }
        onItemAdded(positionToUpdate, callback)
    }

    protected fun onItemAdded(position: Int, callback: ((Int) -> Unit)? = null) {
        notifyItemInserted(position)
        callback?.invoke(position)
    }

    /**
     * Replaces the [item] at [position] in the [currentList] and notifies the underlying adapter
     * of the change. Invokes [callback] last.
     * @param item New list item
     * @param position Index where [item] will replace the existing list item
     * @param callback Lambda that's called at the end of the list changes and has the changed list
     * position passed in as a parameter
     */
    fun changeItem(item: Model, position: Int, callback: ((position: Int) -> Unit)? = null) {
        val newList = currentList.toMutableList()
        newList[position] = item
        currentList = newList
        onItemChanged(position, callback)
    }

    protected fun onItemChanged(position: Int, callback: ((Int) -> Unit)? = null) {
        notifyItemChanged(position)
        callback?.invoke(position)
    }

    /**
     * Removes the list item at [position] in [currentList] and notifies the underlying adapter
     * of the change. Invokes [callback] last.
     * @param position Index where the list item will be removed
     * @param callback Lambda that's called at the end of the list changes and has the removed list
     * position passed in as a parameter
     */
    fun removeItem(position: Int, callback: ((position: Int) -> Unit)? = null) {
        val newList = currentList.toMutableList()
        newList.removeAt(position)
        currentList = newList
        onItemRemoved(position, callback)
    }

    protected fun onItemRemoved(position: Int, callback: ((Int) -> Unit)? = null) {
        notifyItemRemoved(position)
        callback?.invoke(position)
    }

    /**
     * Replaces [currentList] with [newList] and notifies the underlying adapter of the change.
     * @param newList The new list to replace [currentList]
     */
    @SuppressLint("NotifyDataSetChanged")
    open fun replaceList(newList: List<Model>) {
        currentList = newList
        notifyDataSetChanged()
    }
}
