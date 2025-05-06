// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.fragments

import android.app.Dialog
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.text.Html
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.FragmentActivity
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.ViewModelProvider
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citron.citron_emu.R
import org.citron.citron_emu.model.MessageDialogViewModel
import org.citron.citron_emu.utils.Log

class MessageDialogFragment : DialogFragment() {
    private val messageDialogViewModel: MessageDialogViewModel by activityViewModels()

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val titleId = requireArguments().getInt(TITLE_ID)
        val title = if (titleId != 0) {
            getString(titleId)
        } else {
            requireArguments().getString(TITLE_STRING)!!
        }

        val descriptionId = requireArguments().getInt(DESCRIPTION_ID)
        val description = if (descriptionId != 0) {
            getString(descriptionId)
        } else {
            requireArguments().getString(DESCRIPTION_STRING)!!
        }

        val positiveButtonId = requireArguments().getInt(POSITIVE_BUTTON_TITLE_ID)
        val positiveButtonString = requireArguments().getString(POSITIVE_BUTTON_TITLE_STRING)!!
        val positiveButton = if (positiveButtonId != 0) {
            getString(positiveButtonId)
        } else if (positiveButtonString.isNotEmpty()) {
            positiveButtonString
        } else if (messageDialogViewModel.positiveAction != null) {
            getString(android.R.string.ok)
        } else {
            getString(R.string.close)
        }

        val negativeButtonId = requireArguments().getInt(NEGATIVE_BUTTON_TITLE_ID)
        val negativeButtonString = requireArguments().getString(NEGATIVE_BUTTON_TITLE_STRING)!!
        val negativeButton = if (negativeButtonId != 0) {
            getString(negativeButtonId)
        } else if (negativeButtonString.isNotEmpty()) {
            negativeButtonString
        } else {
            getString(android.R.string.cancel)
        }

        val helpLinkId = requireArguments().getInt(HELP_LINK)
        val dismissible = requireArguments().getBoolean(DISMISSIBLE)
        val clearPositiveAction = requireArguments().getBoolean(CLEAR_ACTIONS)
        val showNegativeButton = requireArguments().getBoolean(SHOW_NEGATIVE_BUTTON)

        val builder = MaterialAlertDialogBuilder(requireContext())

        if (clearPositiveAction) {
            messageDialogViewModel.positiveAction = null
        }

        builder.setPositiveButton(positiveButton) { _, _ ->
            messageDialogViewModel.positiveAction?.invoke()
        }
        if (messageDialogViewModel.negativeAction != null || showNegativeButton) {
            builder.setNegativeButton(negativeButton) { _, _ ->
                messageDialogViewModel.negativeAction?.invoke()
            }
        }

        if (title.isNotEmpty()) builder.setTitle(title)
        if (description.isNotEmpty()) {
            builder.setMessage(Html.fromHtml(description, Html.FROM_HTML_MODE_LEGACY))
        }

        if (helpLinkId != 0) {
            builder.setNeutralButton(R.string.learn_more) { _, _ ->
                openLink(getString(helpLinkId))
            }
        }

        isCancelable = dismissible

        return builder.show()
    }

    private fun openLink(link: String) {
        val intent = Intent(Intent.ACTION_VIEW, Uri.parse(link))
        startActivity(intent)
    }

    companion object {
        const val TAG = "MessageDialogFragment"

        private const val TITLE_ID = "Title"
        private const val TITLE_STRING = "TitleString"
        private const val DESCRIPTION_ID = "DescriptionId"
        private const val DESCRIPTION_STRING = "DescriptionString"
        private const val HELP_LINK = "Link"
        private const val DISMISSIBLE = "Dismissible"
        private const val CLEAR_ACTIONS = "ClearActions"
        private const val POSITIVE_BUTTON_TITLE_ID = "PositiveButtonTitleId"
        private const val POSITIVE_BUTTON_TITLE_STRING = "PositiveButtonTitleString"
        private const val SHOW_NEGATIVE_BUTTON = "ShowNegativeButton"
        private const val NEGATIVE_BUTTON_TITLE_ID = "NegativeButtonTitleId"
        private const val NEGATIVE_BUTTON_TITLE_STRING = "NegativeButtonTitleString"

        /**
         * Creates a new [MessageDialogFragment] instance.
         * @param activity Activity that will hold a [MessageDialogViewModel] instance if using
         * [positiveAction] or [negativeAction].
         * @param titleId String resource ID that will be used for the title. [titleString] used if 0.
         * @param titleString String that will be used for the title. No title is set if empty.
         * @param descriptionId String resource ID that will be used for the description.
         * [descriptionString] used if 0.
         * @param descriptionString String that will be used for the description.
         * No description is set if empty.
         * @param helpLinkId String resource ID that contains a help link. Will be added as a neutral
         * button with the title R.string.help.
         * @param dismissible Whether the dialog is dismissible or not. Typically used to ensure that
         * the user clicks on one of the dialog buttons before closing.
         * @param positiveButtonTitleId String resource ID that will be used for the positive button.
         * [positiveButtonTitleString] used if 0.
         * @param positiveButtonTitleString String that will be used for the positive button.
         * android.R.string.close used if empty. android.R.string.ok will be used if [positiveAction]
         * is not null.
         * @param positiveAction Lambda to run when the positive button is clicked.
         * @param showNegativeButton Normally the negative button isn't shown if there is no
         * [negativeAction] set. This can override that behavior to always show a button.
         * @param negativeButtonTitleId String resource ID that will be used for the negative button.
         * [negativeButtonTitleString] used if 0.
         * @param negativeButtonTitleString String that will be used for the negative button.
         * android.R.string.cancel used if empty.
         * @param negativeAction Lambda to run when the negative button is clicked
         */
        fun newInstance(
            activity: FragmentActivity? = null,
            titleId: Int = 0,
            titleString: String = "",
            descriptionId: Int = 0,
            descriptionString: String = "",
            helpLinkId: Int = 0,
            dismissible: Boolean = true,
            positiveButtonTitleId: Int = 0,
            positiveButtonTitleString: String = "",
            positiveAction: (() -> Unit)? = null,
            showNegativeButton: Boolean = false,
            negativeButtonTitleId: Int = 0,
            negativeButtonTitleString: String = "",
            negativeAction: (() -> Unit)? = null
        ): MessageDialogFragment {
            var clearActions = false
            if (activity != null) {
                ViewModelProvider(activity)[MessageDialogViewModel::class.java].apply {
                    clear()
                    this.positiveAction = positiveAction
                    this.negativeAction = negativeAction
                }
            } else {
                clearActions = true
            }

            if (activity == null && (positiveAction == null || negativeAction == null)) {
                Log.warning("[$TAG] Tried to set action with no activity!")
            }

            val dialog = MessageDialogFragment()
            val bundle = Bundle().apply {
                putInt(TITLE_ID, titleId)
                putString(TITLE_STRING, titleString)
                putInt(DESCRIPTION_ID, descriptionId)
                putString(DESCRIPTION_STRING, descriptionString)
                putInt(HELP_LINK, helpLinkId)
                putBoolean(DISMISSIBLE, dismissible)
                putBoolean(CLEAR_ACTIONS, clearActions)
                putInt(POSITIVE_BUTTON_TITLE_ID, positiveButtonTitleId)
                putString(POSITIVE_BUTTON_TITLE_STRING, positiveButtonTitleString)
                putBoolean(SHOW_NEGATIVE_BUTTON, showNegativeButton)
                putInt(NEGATIVE_BUTTON_TITLE_ID, negativeButtonTitleId)
                putString(NEGATIVE_BUTTON_TITLE_STRING, negativeButtonTitleString)
            }
            dialog.arguments = bundle
            return dialog
        }
    }
}
