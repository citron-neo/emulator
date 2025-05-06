// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import org.citron.citron_emu.databinding.DialogLicenseBinding
import org.citron.citron_emu.model.License
import org.citron.citron_emu.utils.SerializableHelper.parcelable

class LicenseBottomSheetDialogFragment : BottomSheetDialogFragment() {
    private var _binding: DialogLicenseBinding? = null
    private val binding get() = _binding!!

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = DialogLicenseBinding.inflate(layoutInflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        BottomSheetBehavior.from<View>(view.parent as View).state =
            BottomSheetBehavior.STATE_HALF_EXPANDED

        val license = requireArguments().parcelable<License>(LICENSE)!!

        binding.apply {
            textTitle.setText(license.titleId)
            textLink.setText(license.linkId)
            textCopyright.setText(license.copyrightId)
            textLicense.setText(license.licenseId)
        }
    }

    companion object {
        const val TAG = "LicenseBottomSheetDialogFragment"

        const val LICENSE = "License"

        fun newInstance(
            license: License
        ): LicenseBottomSheetDialogFragment {
            val dialog = LicenseBottomSheetDialogFragment()
            val bundle = Bundle()
            bundle.putParcelable(LICENSE, license)
            dialog.arguments = bundle
            return dialog
        }
    }
}
