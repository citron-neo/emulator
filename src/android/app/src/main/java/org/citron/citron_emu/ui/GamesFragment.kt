// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.ui

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.android.material.color.MaterialColors
import org.citron.citron_emu.R
import org.citron.citron_emu.adapters.GameAdapter
import org.citron.citron_emu.databinding.FragmentGamesBinding
import org.citron.citron_emu.layout.AutofitGridLayoutManager
import org.citron.citron_emu.model.GamesViewModel
import org.citron.citron_emu.model.HomeViewModel
import org.citron.citron_emu.utils.ViewUtils.setVisible
import org.citron.citron_emu.utils.ViewUtils.updateMargins
import org.citron.citron_emu.utils.collect

class GamesFragment : Fragment() {
    private var _binding: FragmentGamesBinding? = null
    private val binding get() = _binding!!

    private val gamesViewModel: GamesViewModel by activityViewModels()
    private val homeViewModel: HomeViewModel by activityViewModels()

    private lateinit var gameAdapter: GameAdapter
    private var isListView = false

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentGamesBinding.inflate(inflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeViewModel.setNavigationVisibility(visible = true, animated = true)
        homeViewModel.setStatusBarShadeVisibility(true)

        gameAdapter = GameAdapter(requireActivity() as AppCompatActivity)

        binding.gridGames.apply {
            layoutManager = AutofitGridLayoutManager(
                requireContext(),
                requireContext().resources.getDimensionPixelSize(R.dimen.card_width)
            )
            adapter = gameAdapter
        }

        // Set up button for view switching
        binding.btnViewToggle.setOnClickListener {
            toggleViewMode()
        }

        binding.swipeRefresh.apply {
            // Add swipe down to refresh gesture
            setOnRefreshListener {
                gamesViewModel.reloadGames(false)
            }

            // Set theme color to the refresh animation's background
            setProgressBackgroundColorSchemeColor(
                MaterialColors.getColor(
                    binding.swipeRefresh,
                    com.google.android.material.R.attr.colorPrimary
                )
            )
            setColorSchemeColors(
                MaterialColors.getColor(
                    binding.swipeRefresh,
                    com.google.android.material.R.attr.colorOnPrimary
                )
            )

            // Make sure the loading indicator appears even if the layout is told to refresh before being fully drawn
            post {
                if (_binding == null) {
                    return@post
                }
                binding.swipeRefresh.isRefreshing = gamesViewModel.isReloading.value
            }
        }

        gamesViewModel.isReloading.collect(viewLifecycleOwner) {
            binding.swipeRefresh.isRefreshing = it
            binding.noticeText.setVisible(
                visible = gamesViewModel.games.value.isEmpty() && !it,
                gone = false
            )
        }
        gamesViewModel.games.collect(viewLifecycleOwner) {
            gameAdapter.submitList(it)
        }
        gamesViewModel.shouldSwapData.collect(
            viewLifecycleOwner,
            resetState = { gamesViewModel.setShouldSwapData(false) }
        ) {
            if (it) {
                gameAdapter.submitList(gamesViewModel.games.value)
            }
        }
        gamesViewModel.shouldScrollToTop.collect(
            viewLifecycleOwner,
            resetState = { gamesViewModel.setShouldScrollToTop(false) }
        ) { if (it) scrollToTop() }

        setInsets()
    }

    private fun toggleViewMode() {
        isListView = !isListView

        if (isListView) {
            // Switch to list view
            binding.gridGames.layoutManager = LinearLayoutManager(requireContext())
            binding.btnViewToggle.setIconResource(R.drawable.ic_view_grid)
            binding.btnViewToggle.contentDescription = getString(R.string.switch_to_grid_view)
        } else {
            // Switch to grid view
            binding.gridGames.layoutManager = AutofitGridLayoutManager(
                requireContext(),
                requireContext().resources.getDimensionPixelSize(R.dimen.card_width)
            )
            binding.btnViewToggle.setIconResource(R.drawable.ic_view_list)
            binding.btnViewToggle.contentDescription = getString(R.string.switch_to_list_view)
        }

        gameAdapter.setListView(isListView)
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private fun scrollToTop() {
        if (_binding != null) {
            binding.gridGames.smoothScrollToPosition(0)
        }
    }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { view: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            val extraListSpacing = resources.getDimensionPixelSize(R.dimen.spacing_large)
            val spacingNavigation = resources.getDimensionPixelSize(R.dimen.spacing_navigation)
            val spacingNavigationRail =
                resources.getDimensionPixelSize(R.dimen.spacing_navigation_rail)

            binding.gridGames.updatePadding(
                top = barInsets.top + extraListSpacing,
                bottom = barInsets.bottom + spacingNavigation + extraListSpacing
            )

            binding.swipeRefresh.setProgressViewEndTarget(
                false,
                barInsets.top + resources.getDimensionPixelSize(R.dimen.spacing_refresh_end)
            )

            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right
            val left: Int
            val right: Int
            if (ViewCompat.getLayoutDirection(view) == ViewCompat.LAYOUT_DIRECTION_LTR) {
                left = leftInsets + spacingNavigationRail
                right = rightInsets
            } else {
                left = leftInsets
                right = rightInsets + spacingNavigationRail
            }
            binding.swipeRefresh.updateMargins(left = left, right = right)

            binding.noticeText.updatePadding(bottom = spacingNavigation)

            // Update button margins
            val buttonSpacing = resources.getDimensionPixelSize(R.dimen.spacing_fab)
            binding.btnViewToggle.updateMargins(
                left = leftInsets + buttonSpacing,
                right = rightInsets + buttonSpacing,
                bottom = barInsets.bottom + spacingNavigation + buttonSpacing
            )

            windowInsets
        }
}
