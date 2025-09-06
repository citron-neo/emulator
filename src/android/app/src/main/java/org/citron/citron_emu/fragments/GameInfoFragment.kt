// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.fragments

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.findNavController
import androidx.navigation.fragment.navArgs
import com.google.android.material.transition.MaterialSharedAxis
import org.citron.citron_emu.NativeLibrary
import org.citron.citron_emu.R
import org.citron.citron_emu.databinding.FragmentGameInfoBinding
import org.citron.citron_emu.model.GameVerificationResult
import org.citron.citron_emu.model.HomeViewModel
import org.citron.citron_emu.utils.GameMetadata
import org.citron.citron_emu.utils.ViewUtils.setVisible
import org.citron.citron_emu.utils.ViewUtils.updateMargins

class GameInfoFragment : Fragment() {
    private var _binding: FragmentGameInfoBinding? = null
    private val binding get() = _binding!!

    private val homeViewModel: HomeViewModel by activityViewModels()

    private val args by navArgs<GameInfoFragmentArgs>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
        returnTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
        reenterTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)

        // Check for an up-to-date version string
        args.game.version = GameMetadata.getVersion(args.game.path, true)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentGameInfoBinding.inflate(inflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeViewModel.setNavigationVisibility(visible = false, animated = false)
        homeViewModel.setStatusBarShadeVisibility(false)

        binding.apply {
            toolbarInfo.title = args.game.title
            toolbarInfo.setNavigationOnClickListener {
                view.findNavController().popBackStack()
            }

            val pathString = Uri.parse(args.game.path).path ?: ""
            path.setHint(R.string.path)
            pathField.setText(pathString)
            pathField.setOnClickListener { copyToClipboard(getString(R.string.path), pathString) }

            programId.setHint(R.string.program_id)
            programIdField.setText(args.game.programIdHex)
            programIdField.setOnClickListener {
                copyToClipboard(getString(R.string.program_id), args.game.programIdHex)
            }

            if (args.game.developer.isNotEmpty()) {
                developer.setHint(R.string.developer)
                developerField.setText(args.game.developer)
                developerField.setOnClickListener {
                    copyToClipboard(getString(R.string.developer), args.game.developer)
                }
            } else {
                developer.setVisible(false)
            }

            version.setHint(R.string.version)
            versionField.setText(args.game.version)
            versionField.setOnClickListener {
                copyToClipboard(getString(R.string.version), args.game.version)
            }

            buttonCopy.setOnClickListener {
                val details = """
                    ${args.game.title}
                    ${getString(R.string.path)} - $pathString
                    ${getString(R.string.program_id)} - ${args.game.programIdHex}
                    ${getString(R.string.developer)} - ${args.game.developer}
                    ${getString(R.string.version)} - ${args.game.version}
                """.trimIndent()
                copyToClipboard(args.game.title, details)
            }

            buttonVerifyIntegrity.setOnClickListener {
                ProgressDialogFragment.newInstance(
                    requireActivity(),
                    R.string.verifying,
                    true
                ) { progressCallback, _ ->
                    val result = GameVerificationResult.from(
                        NativeLibrary.verifyGameContents(
                            args.game.path,
                            progressCallback
                        )
                    )
                    return@newInstance when (result) {
                        GameVerificationResult.Success ->
                            MessageDialogFragment.newInstance(
                                titleId = R.string.verify_success,
                                descriptionId = R.string.operation_completed_successfully
                            )

                        GameVerificationResult.Failed ->
                            MessageDialogFragment.newInstance(
                                titleId = R.string.verify_failure,
                                descriptionId = R.string.verify_failure_description
                            )

                        GameVerificationResult.NotImplemented ->
                            MessageDialogFragment.newInstance(
                                titleId = R.string.verify_no_result,
                                descriptionId = R.string.verify_no_result_description
                            )
                    }
                }.show(parentFragmentManager, ProgressDialogFragment.TAG)
            }
        }

        setInsets()
    }

    private fun copyToClipboard(label: String, body: String) {
        val clipBoard =
            requireContext().getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = ClipData.newPlainText(label, body)
        clipBoard.setPrimaryClip(clip)

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            Toast.makeText(
                requireContext(),
                R.string.copied_to_clipboard,
                Toast.LENGTH_SHORT
            ).show()
        }
    }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { _: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())

            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right

            binding.toolbarInfo.updateMargins(left = leftInsets, right = rightInsets)
            binding.scrollInfo.updateMargins(left = leftInsets, right = rightInsets)

            binding.contentInfo.updatePadding(bottom = barInsets.bottom)

            windowInsets
        }
}
