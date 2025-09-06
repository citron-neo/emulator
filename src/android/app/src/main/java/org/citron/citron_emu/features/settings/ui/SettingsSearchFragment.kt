// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.features.settings.ui

import android.content.Context
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.InputMethodManager
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.core.widget.doOnTextChanged
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.android.material.divider.MaterialDividerItemDecoration
import com.google.android.material.transition.MaterialSharedAxis
import info.debatty.java.stringsimilarity.Cosine
import org.citron.citron_emu.R
import org.citron.citron_emu.databinding.FragmentSettingsSearchBinding
import org.citron.citron_emu.features.settings.model.view.SettingsItem
import org.citron.citron_emu.utils.NativeConfig
import org.citron.citron_emu.utils.ViewUtils.setVisible
import org.citron.citron_emu.utils.ViewUtils.updateMargins
import org.citron.citron_emu.utils.collect

class SettingsSearchFragment : Fragment() {
    private var _binding: FragmentSettingsSearchBinding? = null
    private val binding get() = _binding!!

    private var settingsAdapter: SettingsAdapter? = null

    private val settingsViewModel: SettingsViewModel by activityViewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
        returnTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
        reenterTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
        exitTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentSettingsSearchBinding.inflate(layoutInflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        if (savedInstanceState != null) {
            binding.searchText.setText(savedInstanceState.getString(SEARCH_TEXT))
        }

        settingsAdapter = SettingsAdapter(this, requireContext())

        val dividerDecoration = MaterialDividerItemDecoration(
            requireContext(),
            LinearLayoutManager.VERTICAL
        )
        dividerDecoration.isLastItemDecorated = false
        binding.settingsList.apply {
            adapter = settingsAdapter
            layoutManager = LinearLayoutManager(requireContext())
            addItemDecoration(dividerDecoration)
        }

        focusSearch()

        binding.backButton.setOnClickListener { settingsViewModel.setShouldNavigateBack(true) }
        binding.searchBackground.setOnClickListener { focusSearch() }
        binding.clearButton.setOnClickListener { binding.searchText.setText("") }
        binding.searchText.doOnTextChanged { _, _, _, _ ->
            search()
            binding.settingsList.smoothScrollToPosition(0)
        }
        settingsViewModel.shouldReloadSettingsList.collect(viewLifecycleOwner) {
            if (it) {
                settingsViewModel.setShouldReloadSettingsList(false)
                search()
            }
        }

        search()

        setInsets()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putString(SEARCH_TEXT, binding.searchText.text.toString())
    }

    private fun search() {
        val searchTerm = binding.searchText.text.toString().lowercase()
        binding.clearButton.setVisible(visible = searchTerm.isNotEmpty(), gone = false)
        if (searchTerm.isEmpty()) {
            binding.noResultsView.setVisible(visible = false, gone = false)
            settingsAdapter?.submitList(emptyList())
            return
        }

        val baseList = SettingsItem.settingsItems
        val similarityAlgorithm = if (searchTerm.length > 2) Cosine() else Cosine(1)
        val sortedList: List<SettingsItem> = baseList.mapNotNull { item ->
            val title = item.value.title.lowercase()
            val similarity = similarityAlgorithm.similarity(searchTerm, title)
            if (similarity > 0.08) {
                Pair(similarity, item)
            } else {
                null
            }
        }.sortedByDescending { it.first }.mapNotNull {
            val item = it.second.value
            val pairedSettingKey = item.setting.pairedSettingKey
            val optionalSetting: SettingsItem? = if (pairedSettingKey.isNotEmpty()) {
                val pairedSettingValue = NativeConfig.getBoolean(pairedSettingKey, false)
                if (pairedSettingValue) it.second.value else null
            } else {
                it.second.value
            }
            optionalSetting
        }
        settingsAdapter?.submitList(sortedList)
        binding.noResultsView.setVisible(visible = sortedList.isEmpty(), gone = false)
    }

    private fun focusSearch() {
        binding.searchText.requestFocus()
        val imm = requireActivity()
            .getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager?
        imm?.showSoftInput(binding.searchText, InputMethodManager.SHOW_IMPLICIT)
    }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { _: View, windowInsets: WindowInsetsCompat ->
            val extraListSpacing = resources.getDimensionPixelSize(R.dimen.spacing_med)
            val sideMargin = resources.getDimensionPixelSize(R.dimen.spacing_medlarge)
            val topMargin = resources.getDimensionPixelSize(R.dimen.spacing_chip)

            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())

            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right

            binding.settingsList.updatePadding(bottom = barInsets.bottom + extraListSpacing)
            binding.frameSearch.updatePadding(
                left = leftInsets + sideMargin,
                top = barInsets.top + topMargin,
                right = rightInsets + sideMargin
            )
            binding.noResultsView.updatePadding(
                left = leftInsets,
                right = rightInsets,
                bottom = barInsets.bottom
            )

            binding.settingsList.updateMargins(
                left = leftInsets + sideMargin,
                right = rightInsets + sideMargin
            )
            binding.divider.updateMargins(
                left = leftInsets + sideMargin,
                right = rightInsets + sideMargin
            )

            windowInsets
        }

    companion object {
        const val SEARCH_TEXT = "SearchText"
    }
}
