// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.fragments

import android.Manifest
import android.content.ActivityNotFoundException
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.provider.DocumentsContract
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.documentfile.provider.DocumentFile
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.findNavController
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.GridLayoutManager
import com.google.android.material.transition.MaterialSharedAxis
import org.citron.citron_emu.BuildConfig
import org.citron.citron_emu.HomeNavigationDirections
import org.citron.citron_emu.NativeLibrary
import org.citron.citron_emu.R
import org.citron.citron_emu.CitronApplication
import org.citron.citron_emu.adapters.HomeSettingAdapter
import org.citron.citron_emu.databinding.FragmentHomeSettingsBinding
import org.citron.citron_emu.features.DocumentProvider
import org.citron.citron_emu.features.settings.model.Settings
import org.citron.citron_emu.model.DriverViewModel
import org.citron.citron_emu.model.HomeSetting
import org.citron.citron_emu.model.HomeViewModel
import org.citron.citron_emu.ui.main.MainActivity
import org.citron.citron_emu.utils.FileUtil
import org.citron.citron_emu.utils.GpuDriverHelper
import org.citron.citron_emu.utils.Log
import org.citron.citron_emu.utils.ViewUtils.updateMargins

class HomeSettingsFragment : Fragment() {
    private var _binding: FragmentHomeSettingsBinding? = null
    private val binding get() = _binding!!

    private lateinit var mainActivity: MainActivity

    private val homeViewModel: HomeViewModel by activityViewModels()
    private val driverViewModel: DriverViewModel by activityViewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        reenterTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentHomeSettingsBinding.inflate(layoutInflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeViewModel.setNavigationVisibility(visible = true, animated = true)
        homeViewModel.setStatusBarShadeVisibility(visible = true)
        mainActivity = requireActivity() as MainActivity

        val optionsList: MutableList<HomeSetting> = mutableListOf<HomeSetting>().apply {
            add(
                HomeSetting(
                    R.string.advanced_settings,
                    R.string.settings_description,
                    R.drawable.ic_settings,
                    {
                        val action = HomeNavigationDirections.actionGlobalSettingsActivity(
                            null,
                            Settings.MenuTag.SECTION_ROOT
                        )
                        binding.root.findNavController().navigate(action)
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.preferences_controls,
                    R.string.preferences_controls_description,
                    R.drawable.ic_controller,
                    {
                        val action = HomeNavigationDirections.actionGlobalSettingsActivity(
                            null,
                            Settings.MenuTag.SECTION_INPUT
                        )
                        binding.root.findNavController().navigate(action)
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.gpu_driver_manager,
                    R.string.install_gpu_driver_description,
                    R.drawable.ic_build,
                    {
                        val action = HomeSettingsFragmentDirections
                            .actionHomeSettingsFragmentToDriverManagerFragment(null)
                        binding.root.findNavController().navigate(action)
                    },
                    { GpuDriverHelper.supportsCustomDriverLoading() },
                    R.string.custom_driver_not_supported,
                    R.string.custom_driver_not_supported_description,
                    driverViewModel.selectedDriverTitle
                )
            )
            add(
                HomeSetting(
                    R.string.applets,
                    R.string.applets_description,
                    R.drawable.ic_applet,
                    {
                        binding.root.findNavController()
                            .navigate(R.id.action_homeSettingsFragment_to_appletLauncherFragment)
                    },
                    { NativeLibrary.isFirmwareAvailable() },
                    R.string.applets_error_firmware,
                    R.string.applets_error_description
                )
            )
            add(
                HomeSetting(
                    R.string.manage_citron_data,
                    R.string.manage_citron_data_description,
                    R.drawable.ic_install,
                    {
                        binding.root.findNavController()
                            .navigate(R.id.action_homeSettingsFragment_to_installableFragment)
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.manage_game_folders,
                    R.string.select_games_folder_description,
                    R.drawable.ic_add,
                    {
                        binding.root.findNavController()
                            .navigate(R.id.action_homeSettingsFragment_to_gameFoldersFragment)
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.verify_installed_content,
                    R.string.verify_installed_content_description,
                    R.drawable.ic_check_circle,
                    {
                        ProgressDialogFragment.newInstance(
                            requireActivity(),
                            titleId = R.string.verifying,
                            cancellable = true
                        ) { progressCallback, _ ->
                            val result = NativeLibrary.verifyInstalledContents(progressCallback)
                            return@newInstance if (progressCallback.invoke(100, 100)) {
                                // Invoke the progress callback to check if the process was cancelled
                                MessageDialogFragment.newInstance(
                                    titleId = R.string.verify_no_result,
                                    descriptionId = R.string.verify_no_result_description
                                )
                            } else if (result.isEmpty()) {
                                MessageDialogFragment.newInstance(
                                    titleId = R.string.verify_success,
                                    descriptionId = R.string.operation_completed_successfully
                                )
                            } else {
                                val failedNames = result.joinToString("\n")
                                val errorMessage = CitronApplication.appContext.getString(
                                    R.string.verification_failed_for,
                                    failedNames
                                )
                                MessageDialogFragment.newInstance(
                                    titleId = R.string.verify_failure,
                                    descriptionString = errorMessage
                                )
                            }
                        }.show(parentFragmentManager, ProgressDialogFragment.TAG)
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.share_log,
                    R.string.share_log_description,
                    R.drawable.ic_log,
                    { shareLog() }
                )
            )
            add(
                HomeSetting(
                    R.string.open_user_folder,
                    R.string.open_user_folder_description,
                    R.drawable.ic_folder_open,
                    { openFileManager() }
                )
            )
            add(
                HomeSetting(
                    R.string.preferences_theme,
                    R.string.theme_and_color_description,
                    R.drawable.ic_palette,
                    {
                        val action = HomeNavigationDirections.actionGlobalSettingsActivity(
                            null,
                            Settings.MenuTag.SECTION_THEME
                        )
                        binding.root.findNavController().navigate(action)
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.about,
                    R.string.about_description,
                    R.drawable.ic_info_outline,
                    {
                        exitTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
                        parentFragmentManager.primaryNavigationFragment?.findNavController()
                            ?.navigate(R.id.action_homeSettingsFragment_to_aboutFragment)
                    }
                )
            )
        }

        if (!BuildConfig.PREMIUM) {
            optionsList.add(
                0,
                HomeSetting(
                    R.string.get_early_access,
                    R.string.get_early_access_description,
                    R.drawable.ic_diamond,
                    {
                        exitTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
                        parentFragmentManager.primaryNavigationFragment?.findNavController()
                            ?.navigate(R.id.action_homeSettingsFragment_to_earlyAccessFragment)
                    }
                )
            )
        }

        binding.homeSettingsList.apply {
            layoutManager =
                GridLayoutManager(requireContext(), resources.getInteger(R.integer.grid_columns))
            adapter = HomeSettingAdapter(
                requireActivity() as AppCompatActivity,
                viewLifecycleOwner,
                optionsList
            )
        }

        setInsets()
    }

    override fun onStart() {
        super.onStart()
        exitTransition = null
    }

    override fun onResume() {
        super.onResume()
        driverViewModel.updateDriverNameForGame(null)
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private fun openFileManager() {
        // First, try to open the user data folder directly
        try {
            startActivity(getFileManagerIntentOnDocumentProvider(Intent.ACTION_VIEW))
            return
        } catch (_: ActivityNotFoundException) {
        }

        try {
            startActivity(getFileManagerIntentOnDocumentProvider("android.provider.action.BROWSE"))
            return
        } catch (_: ActivityNotFoundException) {
        }

        // Just try to open the file manager, try the package name used on "normal" phones
        try {
            startActivity(getFileManagerIntent("com.google.android.documentsui"))
            showNoLinkNotification()
            return
        } catch (_: ActivityNotFoundException) {
        }

        try {
            // Next, try the AOSP package name
            startActivity(getFileManagerIntent("com.android.documentsui"))
            showNoLinkNotification()
            return
        } catch (_: ActivityNotFoundException) {
        }

        Toast.makeText(
            requireContext(),
            resources.getString(R.string.no_file_manager),
            Toast.LENGTH_LONG
        ).show()
    }

    private fun getFileManagerIntent(packageName: String): Intent {
        // Fragile, but some phones don't expose the system file manager in any better way
        val intent = Intent(Intent.ACTION_MAIN)
        intent.setClassName(packageName, "com.android.documentsui.files.FilesActivity")
        intent.flags = Intent.FLAG_ACTIVITY_NEW_TASK
        return intent
    }

    private fun getFileManagerIntentOnDocumentProvider(action: String): Intent {
        val authority = "${requireContext().packageName}.user"
        val intent = Intent(action)
        intent.addCategory(Intent.CATEGORY_DEFAULT)
        intent.data = DocumentsContract.buildRootUri(authority, DocumentProvider.ROOT_ID)
        intent.addFlags(
            Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION or
                Intent.FLAG_GRANT_PREFIX_URI_PERMISSION or
                Intent.FLAG_GRANT_WRITE_URI_PERMISSION
        )
        return intent
    }

    private fun showNoLinkNotification() {
        val builder = NotificationCompat.Builder(
            requireContext(),
            getString(R.string.notice_notification_channel_id)
        )
            .setSmallIcon(R.drawable.ic_stat_notification_logo)
            .setContentTitle(getString(R.string.notification_no_directory_link))
            .setContentText(getString(R.string.notification_no_directory_link_description))
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setAutoCancel(true)
        // TODO: Make the click action for this notification lead to a help article

        with(NotificationManagerCompat.from(requireContext())) {
            if (ActivityCompat.checkSelfPermission(
                    requireContext(),
                    Manifest.permission.POST_NOTIFICATIONS
                ) != PackageManager.PERMISSION_GRANTED
            ) {
                Toast.makeText(
                    requireContext(),
                    resources.getString(R.string.notification_permission_not_granted),
                    Toast.LENGTH_LONG
                ).show()
                return
            }
            notify(0, builder.build())
        }
    }

    // Share the current log if we just returned from a game but share the old log
    // if we just started the app and the old log exists.
    private fun shareLog() {
        val currentLog = DocumentFile.fromSingleUri(
            mainActivity,
            DocumentsContract.buildDocumentUri(
                DocumentProvider.AUTHORITY,
                "${DocumentProvider.ROOT_ID}/log/citron_log.txt"
            )
        )!!
        val oldLog = DocumentFile.fromSingleUri(
            mainActivity,
            DocumentsContract.buildDocumentUri(
                DocumentProvider.AUTHORITY,
                "${DocumentProvider.ROOT_ID}/log/citron_log.txt.old.txt"
            )
        )!!

        val intent = Intent(Intent.ACTION_SEND)
            .setDataAndType(currentLog.uri, FileUtil.TEXT_PLAIN)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        if (!Log.gameLaunched && oldLog.exists()) {
            intent.putExtra(Intent.EXTRA_STREAM, oldLog.uri)
            startActivity(Intent.createChooser(intent, getText(R.string.share_log)))
        } else if (currentLog.exists()) {
            intent.putExtra(Intent.EXTRA_STREAM, currentLog.uri)
            startActivity(Intent.createChooser(intent, getText(R.string.share_log)))
        } else {
            Toast.makeText(
                requireContext(),
                getText(R.string.share_log_missing),
                Toast.LENGTH_SHORT
            ).show()
        }
    }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { view: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            val spacingNavigation = resources.getDimensionPixelSize(R.dimen.spacing_navigation)
            val spacingNavigationRail =
                resources.getDimensionPixelSize(R.dimen.spacing_navigation_rail)

            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right

            binding.scrollViewSettings.updatePadding(
                top = barInsets.top,
                bottom = barInsets.bottom
            )

            binding.scrollViewSettings.updateMargins(left = leftInsets, right = rightInsets)

            binding.linearLayoutSettings.updatePadding(bottom = spacingNavigation)

            if (ViewCompat.getLayoutDirection(view) == ViewCompat.LAYOUT_DIRECTION_LTR) {
                binding.linearLayoutSettings.updatePadding(left = spacingNavigationRail)
            } else {
                binding.linearLayoutSettings.updatePadding(right = spacingNavigationRail)
            }

            windowInsets
        }
}
