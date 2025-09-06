// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.fragments

import android.Manifest
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.RequiresApi
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.isVisible
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.findNavController
import androidx.preference.PreferenceManager
import androidx.viewpager2.widget.ViewPager2.OnPageChangeCallback
import com.google.android.material.transition.MaterialFadeThrough
import kotlinx.coroutines.launch
import org.citron.citron_emu.NativeLibrary
import java.io.File
import org.citron.citron_emu.R
import org.citron.citron_emu.CitronApplication
import org.citron.citron_emu.adapters.SetupAdapter
import org.citron.citron_emu.databinding.FragmentSetupBinding
import org.citron.citron_emu.features.settings.model.Settings
import org.citron.citron_emu.model.HomeViewModel
import org.citron.citron_emu.model.SetupCallback
import org.citron.citron_emu.model.SetupPage
import org.citron.citron_emu.model.StepState
import org.citron.citron_emu.ui.main.MainActivity
import org.citron.citron_emu.utils.DirectoryInitialization
import org.citron.citron_emu.utils.NativeConfig
import org.citron.citron_emu.utils.ViewUtils
import org.citron.citron_emu.utils.ViewUtils.setVisible
import org.citron.citron_emu.utils.collect

class SetupFragment : Fragment() {
    private var _binding: FragmentSetupBinding? = null
    private val binding get() = _binding!!

    private val homeViewModel: HomeViewModel by activityViewModels()

    private lateinit var mainActivity: MainActivity

    private lateinit var hasBeenWarned: BooleanArray

    companion object {
        const val KEY_NEXT_VISIBILITY = "NextButtonVisibility"
        const val KEY_BACK_VISIBILITY = "BackButtonVisibility"
        const val KEY_HAS_BEEN_WARNED = "HasBeenWarned"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        exitTransition = MaterialFadeThrough()
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentSetupBinding.inflate(layoutInflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        mainActivity = requireActivity() as MainActivity

        homeViewModel.setNavigationVisibility(visible = false, animated = false)

        requireActivity().onBackPressedDispatcher.addCallback(
            viewLifecycleOwner,
            object : OnBackPressedCallback(true) {
                override fun handleOnBackPressed() {
                    if (binding.viewPager2.currentItem > 0) {
                        pageBackward()
                    } else {
                        requireActivity().finish()
                    }
                }
            }
        )

        requireActivity().window.navigationBarColor =
            ContextCompat.getColor(requireContext(), android.R.color.transparent)

        val pages = mutableListOf<SetupPage>()
        pages.apply {
            add(
                SetupPage(
                    R.drawable.ic_citron_title,
                    R.string.welcome,
                    R.string.welcome_description,
                    0,
                    true,
                    R.string.get_started,
                    { pageForward() },
                    false
                )
            )

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                add(
                    SetupPage(
                        R.drawable.ic_notification,
                        R.string.notifications,
                        R.string.notifications_description,
                        0,
                        false,
                        R.string.give_permission,
                        {
                            notificationCallback = it
                            permissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
                        },
                        true,
                        R.string.notification_warning,
                        R.string.notification_warning_description,
                        0,
                        {
                            if (NotificationManagerCompat.from(requireContext())
                                .areNotificationsEnabled()
                            ) {
                                StepState.COMPLETE
                            } else {
                                StepState.INCOMPLETE
                            }
                        }
                    )
                )
            }

            add(
                SetupPage(
                    R.drawable.ic_key,
                    R.string.keys,
                    R.string.keys_description,
                    R.drawable.ic_add,
                    true,
                    R.string.select_keys,
                    {
                        keyCallback = it
                        getProdKey.launch(arrayOf("*/*"))
                    },
                    true,
                    R.string.install_prod_keys_warning,
                    R.string.install_prod_keys_warning_description,
                    R.string.install_prod_keys_warning_help,
                    {
                        val file = File(DirectoryInitialization.userDirectory + "/keys/prod.keys")
                        if (file.exists() && NativeLibrary.areKeysPresent()) {
                            StepState.COMPLETE
                        } else {
                            StepState.INCOMPLETE
                        }
                    }
                )
            )
            add(
                SetupPage(
                    R.drawable.ic_controller,
                    R.string.games,
                    R.string.games_description,
                    R.drawable.ic_add,
                    true,
                    R.string.add_games,
                    {
                        gamesDirCallback = it
                        getGamesDirectory.launch(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).data)
                    },
                    true,
                    R.string.add_games_warning,
                    R.string.add_games_warning_description,
                    R.string.add_games_warning_help,
                    {
                        if (NativeConfig.getGameDirs().isNotEmpty()) {
                            StepState.COMPLETE
                        } else {
                            StepState.INCOMPLETE
                        }
                    }
                )
            )
            add(
                SetupPage(
                    R.drawable.ic_check,
                    R.string.done,
                    R.string.done_description,
                    R.drawable.ic_arrow_forward,
                    false,
                    R.string.text_continue,
                    { finishSetup() },
                    false
                )
            )
        }

        homeViewModel.shouldPageForward.collect(
            viewLifecycleOwner,
            resetState = { homeViewModel.setShouldPageForward(false) }
        ) { if (it) pageForward() }
        homeViewModel.gamesDirSelected.collect(
            viewLifecycleOwner,
            resetState = { homeViewModel.setGamesDirSelected(false) }
        ) { if (it) gamesDirCallback.onStepCompleted() }

        binding.viewPager2.apply {
            adapter = SetupAdapter(requireActivity() as AppCompatActivity, pages)
            offscreenPageLimit = 2
            isUserInputEnabled = false
        }

        binding.viewPager2.registerOnPageChangeCallback(object : OnPageChangeCallback() {
            var previousPosition: Int = 0

            override fun onPageSelected(position: Int) {
                super.onPageSelected(position)

                if (position == 1 && previousPosition == 0) {
                    ViewUtils.showView(binding.buttonNext)
                    ViewUtils.showView(binding.buttonBack)
                } else if (position == 0 && previousPosition == 1) {
                    ViewUtils.hideView(binding.buttonBack)
                    ViewUtils.hideView(binding.buttonNext)
                } else if (position == pages.size - 1 && previousPosition == pages.size - 2) {
                    ViewUtils.hideView(binding.buttonNext)
                } else if (position == pages.size - 2 && previousPosition == pages.size - 1) {
                    ViewUtils.showView(binding.buttonNext)
                }

                previousPosition = position
            }
        })

        binding.buttonNext.setOnClickListener {
            val index = binding.viewPager2.currentItem
            val currentPage = pages[index]

            // Checks if the user has completed the task on the current page
            if (currentPage.hasWarning) {
                val stepState = currentPage.stepCompleted.invoke()
                if (stepState != StepState.INCOMPLETE) {
                    pageForward()
                    return@setOnClickListener
                }

                if (!hasBeenWarned[index]) {
                    SetupWarningDialogFragment.newInstance(
                        currentPage.warningTitleId,
                        currentPage.warningDescriptionId,
                        currentPage.warningHelpLinkId,
                        index
                    ).show(childFragmentManager, SetupWarningDialogFragment.TAG)
                    return@setOnClickListener
                }
            }
            pageForward()
        }
        binding.buttonBack.setOnClickListener { pageBackward() }

        if (savedInstanceState != null) {
            val nextIsVisible = savedInstanceState.getBoolean(KEY_NEXT_VISIBILITY)
            val backIsVisible = savedInstanceState.getBoolean(KEY_BACK_VISIBILITY)
            hasBeenWarned = savedInstanceState.getBooleanArray(KEY_HAS_BEEN_WARNED)!!

            binding.buttonNext.setVisible(nextIsVisible)
            binding.buttonBack.setVisible(backIsVisible)
        } else {
            hasBeenWarned = BooleanArray(pages.size)
        }

        setInsets()
    }

    override fun onStop() {
        super.onStop()
        NativeConfig.saveGlobalConfig()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        if (_binding != null) {
            outState.putBoolean(KEY_NEXT_VISIBILITY, binding.buttonNext.isVisible)
            outState.putBoolean(KEY_BACK_VISIBILITY, binding.buttonBack.isVisible)
        }
        outState.putBooleanArray(KEY_HAS_BEEN_WARNED, hasBeenWarned)
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private lateinit var notificationCallback: SetupCallback

    @RequiresApi(Build.VERSION_CODES.TIRAMISU)
    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) {
            if (it) {
                notificationCallback.onStepCompleted()
            }

            if (!it &&
                !shouldShowRequestPermissionRationale(Manifest.permission.POST_NOTIFICATIONS)
            ) {
                PermissionDeniedDialogFragment().show(
                    childFragmentManager,
                    PermissionDeniedDialogFragment.TAG
                )
            }
        }

    private lateinit var keyCallback: SetupCallback

    val getProdKey =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { result ->
            if (result != null) {
                mainActivity.processKey(result)
                if (NativeLibrary.areKeysPresent()) {
                    keyCallback.onStepCompleted()
                }
            }
        }

    private lateinit var gamesDirCallback: SetupCallback

    val getGamesDirectory =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { result ->
            if (result != null) {
                mainActivity.processGamesDir(result)
            }
        }

    private fun finishSetup() {
        PreferenceManager.getDefaultSharedPreferences(CitronApplication.appContext).edit()
            .putBoolean(Settings.PREF_FIRST_APP_LAUNCH, false)
            .apply()
        mainActivity.finishSetup(binding.root.findNavController())
    }

    fun pageForward() {
        if (_binding != null) {
            binding.viewPager2.currentItem += 1
        }
    }

    fun pageBackward() {
        if (_binding != null) {
            binding.viewPager2.currentItem -= 1
        }
    }

    fun setPageWarned(page: Int) {
        hasBeenWarned[page] = true
    }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { _: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())

            val leftPadding = barInsets.left + cutoutInsets.left
            val topPadding = barInsets.top + cutoutInsets.top
            val rightPadding = barInsets.right + cutoutInsets.right
            val bottomPadding = barInsets.bottom + cutoutInsets.bottom

            if (resources.getBoolean(R.bool.small_layout)) {
                binding.viewPager2
                    .updatePadding(left = leftPadding, top = topPadding, right = rightPadding)
                binding.constraintButtons
                    .updatePadding(left = leftPadding, right = rightPadding, bottom = bottomPadding)
            } else {
                binding.viewPager2.updatePadding(top = topPadding, bottom = bottomPadding)
                binding.constraintButtons
                    .updatePadding(
                        left = leftPadding,
                        right = rightPadding,
                        bottom = bottomPadding
                    )
            }
            windowInsets
        }
}
