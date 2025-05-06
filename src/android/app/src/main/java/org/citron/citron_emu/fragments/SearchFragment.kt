// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.fragments

import android.content.Context
import android.content.SharedPreferences
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.InputMethodManager
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.core.widget.doOnTextChanged
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.preference.PreferenceManager
import info.debatty.java.stringsimilarity.Jaccard
import info.debatty.java.stringsimilarity.JaroWinkler
import java.util.Locale
import org.citron.citron_emu.R
import org.citron.citron_emu.CitronApplication
import org.citron.citron_emu.adapters.GameAdapter
import org.citron.citron_emu.databinding.FragmentSearchBinding
import org.citron.citron_emu.layout.AutofitGridLayoutManager
import org.citron.citron_emu.model.Game
import org.citron.citron_emu.model.GamesViewModel
import org.citron.citron_emu.model.HomeViewModel
import org.citron.citron_emu.utils.ViewUtils.setVisible
import org.citron.citron_emu.utils.collect

class SearchFragment : Fragment() {
    private var _binding: FragmentSearchBinding? = null
    private val binding get() = _binding!!

    private val gamesViewModel: GamesViewModel by activityViewModels()
    private val homeViewModel: HomeViewModel by activityViewModels()

    private lateinit var preferences: SharedPreferences

    companion object {
        private const val SEARCH_TEXT = "SearchText"
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentSearchBinding.inflate(layoutInflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeViewModel.setNavigationVisibility(visible = true, animated = true)
        homeViewModel.setStatusBarShadeVisibility(true)
        preferences = PreferenceManager.getDefaultSharedPreferences(CitronApplication.appContext)

        if (savedInstanceState != null) {
            binding.searchText.setText(savedInstanceState.getString(SEARCH_TEXT))
        }

        binding.gridGamesSearch.apply {
            layoutManager = AutofitGridLayoutManager(
                requireContext(),
                requireContext().resources.getDimensionPixelSize(R.dimen.card_width)
            )
            adapter = GameAdapter(requireActivity() as AppCompatActivity)
        }

        binding.chipGroup.setOnCheckedStateChangeListener { _, _ -> filterAndSearch() }

        binding.searchText.doOnTextChanged { text: CharSequence?, _: Int, _: Int, _: Int ->
            binding.clearButton.setVisible(text.toString().isNotEmpty())
            filterAndSearch()
        }

        gamesViewModel.searchFocused.collect(
            viewLifecycleOwner,
            resetState = { gamesViewModel.setSearchFocused(false) }
        ) { if (it) focusSearch() }
        gamesViewModel.games.collect(viewLifecycleOwner) { filterAndSearch() }
        gamesViewModel.searchedGames.collect(viewLifecycleOwner) {
            (binding.gridGamesSearch.adapter as GameAdapter).submitList(it)
            binding.noResultsView.setVisible(it.isNotEmpty())
        }

        binding.clearButton.setOnClickListener { binding.searchText.setText("") }

        binding.searchBackground.setOnClickListener { focusSearch() }

        setInsets()
        filterAndSearch()
    }

    private inner class ScoredGame(val score: Double, val item: Game)

    private fun filterAndSearch() {
        val baseList = gamesViewModel.games.value
        val filteredList: List<Game> = when (binding.chipGroup.checkedChipId) {
            R.id.chip_recently_played -> {
                baseList.filter {
                    val lastPlayedTime = preferences.getLong(it.keyLastPlayedTime, 0L)
                    lastPlayedTime > (System.currentTimeMillis() - 24 * 60 * 60 * 1000)
                }.sortedByDescending { preferences.getLong(it.keyLastPlayedTime, 0L) }
            }

            R.id.chip_recently_added -> {
                baseList.filter {
                    val addedTime = preferences.getLong(it.keyAddedToLibraryTime, 0L)
                    addedTime > (System.currentTimeMillis() - 24 * 60 * 60 * 1000)
                }.sortedByDescending { preferences.getLong(it.keyAddedToLibraryTime, 0L) }
            }

            R.id.chip_homebrew -> baseList.filter { it.isHomebrew }

            R.id.chip_retail -> baseList.filter { !it.isHomebrew }

            else -> baseList
        }

        if (binding.searchText.text.toString().isEmpty() &&
            binding.chipGroup.checkedChipId != View.NO_ID
        ) {
            gamesViewModel.setSearchedGames(filteredList)
            return
        }

        val searchTerm = binding.searchText.text.toString().lowercase(Locale.getDefault())
        val searchAlgorithm = if (searchTerm.length > 1) Jaccard(2) else JaroWinkler()
        val sortedList: List<Game> = filteredList.mapNotNull { game ->
            val title = game.title.lowercase(Locale.getDefault())
            val score = searchAlgorithm.similarity(searchTerm, title)
            if (score > 0.03) {
                ScoredGame(score, game)
            } else {
                null
            }
        }.sortedByDescending { it.score }.map { it.item }
        gamesViewModel.setSearchedGames(sortedList)
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        if (_binding != null) {
            outState.putString(SEARCH_TEXT, binding.searchText.text.toString())
        }
    }

    private fun focusSearch() {
        if (_binding != null) {
            binding.searchText.requestFocus()
            val imm = requireActivity()
                .getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager?
            imm?.showSoftInput(binding.searchText, InputMethodManager.SHOW_IMPLICIT)
        }
    }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { view: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            val extraListSpacing = resources.getDimensionPixelSize(R.dimen.spacing_med)
            val spacingNavigation = resources.getDimensionPixelSize(R.dimen.spacing_navigation)
            val spacingNavigationRail =
                resources.getDimensionPixelSize(R.dimen.spacing_navigation_rail)
            val chipSpacing = resources.getDimensionPixelSize(R.dimen.spacing_chip)

            binding.constraintSearch.updatePadding(
                left = barInsets.left + cutoutInsets.left,
                top = barInsets.top,
                right = barInsets.right + cutoutInsets.right
            )

            binding.gridGamesSearch.updatePadding(
                top = extraListSpacing,
                bottom = barInsets.bottom + spacingNavigation + extraListSpacing
            )
            binding.noResultsView.updatePadding(bottom = spacingNavigation + barInsets.bottom)

            val mlpDivider = binding.divider.layoutParams as ViewGroup.MarginLayoutParams
            if (ViewCompat.getLayoutDirection(view) == ViewCompat.LAYOUT_DIRECTION_LTR) {
                binding.frameSearch.updatePadding(left = spacingNavigationRail)
                binding.gridGamesSearch.updatePadding(left = spacingNavigationRail)
                binding.noResultsView.updatePadding(left = spacingNavigationRail)
                binding.chipGroup.updatePadding(
                    left = chipSpacing + spacingNavigationRail,
                    right = chipSpacing
                )
                mlpDivider.leftMargin = chipSpacing + spacingNavigationRail
                mlpDivider.rightMargin = chipSpacing
            } else {
                binding.frameSearch.updatePadding(right = spacingNavigationRail)
                binding.gridGamesSearch.updatePadding(right = spacingNavigationRail)
                binding.noResultsView.updatePadding(right = spacingNavigationRail)
                binding.chipGroup.updatePadding(
                    left = chipSpacing,
                    right = chipSpacing + spacingNavigationRail
                )
                mlpDivider.leftMargin = chipSpacing
                mlpDivider.rightMargin = chipSpacing + spacingNavigationRail
            }
            binding.divider.layoutParams = mlpDivider

            windowInsets
        }
}
