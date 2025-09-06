// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.fragments

import android.app.Dialog
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.FragmentActivity
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.ViewModelProvider
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citron.citron_emu.R
import org.citron.citron_emu.databinding.DialogProgressBarBinding
import org.citron.citron_emu.model.TaskViewModel
import org.citron.citron_emu.utils.ViewUtils.setVisible
import org.citron.citron_emu.utils.collect

class ProgressDialogFragment : DialogFragment() {
    private val taskViewModel: TaskViewModel by activityViewModels()

    private lateinit var binding: DialogProgressBarBinding

    private val PROGRESS_BAR_RESOLUTION = 1000

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val titleId = requireArguments().getInt(TITLE)
        val cancellable = requireArguments().getBoolean(CANCELLABLE)

        binding = DialogProgressBarBinding.inflate(layoutInflater)
        binding.progressBar.isIndeterminate = true
        val dialog = MaterialAlertDialogBuilder(requireContext())
            .setTitle(titleId)
            .setView(binding.root)

        if (cancellable) {
            dialog.setNegativeButton(android.R.string.cancel, null)
        }

        val alertDialog = dialog.create()
        alertDialog.setCanceledOnTouchOutside(false)

        if (!taskViewModel.isRunning.value) {
            taskViewModel.runTask()
        }
        return alertDialog
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        binding.message.isSelected = true
        taskViewModel.isComplete.collect(viewLifecycleOwner) {
            if (it) {
                dismiss()
                when (val result = taskViewModel.result.value) {
                    is String -> Toast.makeText(
                        requireContext(),
                        result,
                        Toast.LENGTH_LONG
                    ).show()

                    is MessageDialogFragment -> result.show(
                        requireActivity().supportFragmentManager,
                        MessageDialogFragment.TAG
                    )

                    else -> {
                        // Do nothing
                    }
                }
                taskViewModel.clear()
            }
        }
        taskViewModel.cancelled.collect(viewLifecycleOwner) {
            if (it) {
                dialog?.setTitle(R.string.cancelling)
            }
        }
        taskViewModel.progress.collect(viewLifecycleOwner) {
            if (it != 0.0) {
                binding.progressBar.apply {
                    isIndeterminate = false
                    progress = (
                        (it / taskViewModel.maxProgress.value) *
                            PROGRESS_BAR_RESOLUTION
                        ).toInt()
                    min = 0
                    max = PROGRESS_BAR_RESOLUTION
                }
            }
        }
        taskViewModel.message.collect(viewLifecycleOwner) {
            binding.message.setVisible(it.isNotEmpty())
            binding.message.text = it
        }
    }

    // By default, the ProgressDialog will immediately dismiss itself upon a button being pressed.
    // Setting the OnClickListener again after the dialog is shown overrides this behavior.
    override fun onResume() {
        super.onResume()
        val alertDialog = dialog as AlertDialog
        val negativeButton = alertDialog.getButton(Dialog.BUTTON_NEGATIVE)
        negativeButton.setOnClickListener {
            alertDialog.setTitle(getString(R.string.cancelling))
            binding.progressBar.isIndeterminate = true
            taskViewModel.setCancelled(true)
        }
    }

    companion object {
        const val TAG = "IndeterminateProgressDialogFragment"

        private const val TITLE = "Title"
        private const val CANCELLABLE = "Cancellable"

        fun newInstance(
            activity: FragmentActivity,
            titleId: Int,
            cancellable: Boolean = false,
            task: suspend (
                progressCallback: (max: Long, progress: Long) -> Boolean,
                messageCallback: (message: String) -> Unit
            ) -> Any
        ): ProgressDialogFragment {
            val dialog = ProgressDialogFragment()
            val args = Bundle()
            ViewModelProvider(activity)[TaskViewModel::class.java].task = task
            args.putInt(TITLE, titleId)
            args.putBoolean(CANCELLABLE, cancellable)
            dialog.arguments = args
            return dialog
        }
    }
}
