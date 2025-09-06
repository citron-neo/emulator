// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.adapters

import org.citron.citron_emu.model.SelectableItem
import org.citron.citron_emu.viewholder.AbstractViewHolder

/**
 * Generic list class meant to take care of single selection UI updates
 * @param currentList The list to show initially
 * @param defaultSelection The default selection to use if no list items are selected by
 * [SelectableItem.selected] or if the currently selected item is removed from the list
 */
abstract class AbstractSingleSelectionList<
    Model : SelectableItem,
    Holder : AbstractViewHolder<Model>
    >(
    final override var currentList: List<Model>,
    private val defaultSelection: DefaultSelection = DefaultSelection.Start
) : AbstractListAdapter<Model, Holder>(currentList) {
    var selectedItem = getDefaultSelection()

    init {
        findSelectedItem()
    }

    /**
     * Changes the selection state of the [SelectableItem] that was selected and the previously selected
     * item and notifies the underlying adapter of the change for those items. Invokes [callback] last.
     * Does nothing if [position] is the same as the currently selected item.
     * @param position Index of the item that was selected
     * @param callback Lambda that's called at the end of the list changes and has the selected list
     * position passed in as a parameter
     */
    fun selectItem(position: Int, callback: ((position: Int) -> Unit)? = null) {
        if (position == selectedItem) {
            return
        }

        val previouslySelectedItem = selectedItem
        selectedItem = position
        if (currentList.indices.contains(selectedItem)) {
            currentList[selectedItem].onSelectionStateChanged(true)
        }
        if (currentList.indices.contains(previouslySelectedItem)) {
            currentList[previouslySelectedItem].onSelectionStateChanged(false)
        }
        onItemChanged(previouslySelectedItem)
        onItemChanged(selectedItem)
        callback?.invoke(position)
    }

    /**
     * Removes a given item from the list and notifies the underlying adapter of the change. If the
     * currently selected item was the item that was removed, the item at the position provided
     * by [defaultSelection] will be made the new selection. Invokes [callback] last.
     * @param position Index of the item that was removed
     * @param callback Lambda that's called at the end of the list changes and has the removed and
     * selected list positions passed in as parameters
     */
    fun removeSelectableItem(
        position: Int,
        callback: ((removedPosition: Int, selectedPosition: Int) -> Unit)?
    ) {
        removeItem(position)
        if (position == selectedItem) {
            selectedItem = getDefaultSelection()
            currentList[selectedItem].onSelectionStateChanged(true)
            onItemChanged(selectedItem)
        } else if (position < selectedItem) {
            selectedItem--
        }
        callback?.invoke(position, selectedItem)
    }

    override fun addItem(item: Model, position: Int, callback: ((Int) -> Unit)?) {
        super.addItem(item, position, callback)
        if (position <= selectedItem && position != -1) {
            selectedItem++
        }
    }

    override fun replaceList(newList: List<Model>) {
        super.replaceList(newList)
        findSelectedItem()
    }

    private fun findSelectedItem() {
        for (i in currentList.indices) {
            if (currentList[i].selected) {
                selectedItem = i
                break
            }
        }
    }

    private fun getDefaultSelection(): Int =
        when (defaultSelection) {
            DefaultSelection.Start -> currentList.indices.first
            DefaultSelection.End -> currentList.indices.last
        }

    enum class DefaultSelection { Start, End }
}
