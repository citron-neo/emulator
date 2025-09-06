// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.adapters

import android.net.Uri
import android.view.LayoutInflater
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.pm.ShortcutInfoCompat
import androidx.core.content.pm.ShortcutManagerCompat
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.lifecycleScope
import androidx.navigation.findNavController
import androidx.preference.PreferenceManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.citron.citron_emu.HomeNavigationDirections
import org.citron.citron_emu.R
import org.citron.citron_emu.CitronApplication
import org.citron.citron_emu.databinding.CardGameBinding
import org.citron.citron_emu.databinding.CardGameListBinding
import org.citron.citron_emu.model.Game
import org.citron.citron_emu.model.GamesViewModel
import org.citron.citron_emu.utils.GameIconUtils
import org.citron.citron_emu.utils.ViewUtils.marquee
import org.citron.citron_emu.viewholder.AbstractViewHolder

class GameAdapter(private val activity: AppCompatActivity) :
    AbstractDiffAdapter<Game, AbstractViewHolder<Game>>(exact = false) {

    companion object {
        const val VIEW_TYPE_GRID = 0
        const val VIEW_TYPE_LIST = 1
    }

    private var isListView = false

    fun setListView(listView: Boolean) {
        if (isListView != listView) {
            isListView = listView
            notifyDataSetChanged()
        }
    }

    override fun getItemViewType(position: Int): Int {
        return if (isListView) VIEW_TYPE_LIST else VIEW_TYPE_GRID
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): AbstractViewHolder<Game> {
        return when (viewType) {
            VIEW_TYPE_GRID -> {
                val binding = CardGameBinding.inflate(LayoutInflater.from(parent.context), parent, false)
                GameGridViewHolder(binding)
            }
            VIEW_TYPE_LIST -> {
                val binding = CardGameListBinding.inflate(LayoutInflater.from(parent.context), parent, false)
                GameListViewHolder(binding)
            }
            else -> throw IllegalArgumentException("Invalid view type")
        }
    }

    inner class GameGridViewHolder(val binding: CardGameBinding) :
        AbstractViewHolder<Game>(binding) {
        override fun bind(model: Game) {
            binding.imageGameScreen.scaleType = ImageView.ScaleType.CENTER_CROP
            GameIconUtils.loadGameIcon(model, binding.imageGameScreen)

            binding.textGameTitle.text = model.title.replace("[\\t\\n\\r]+".toRegex(), " ")

            binding.textGameTitle.marquee()
            binding.cardGame.setOnClickListener { onClick(model) }
            binding.cardGame.setOnLongClickListener { onLongClick(model) }
        }

        fun onClick(game: Game) {
            handleGameClick(game)
        }

        fun onLongClick(game: Game): Boolean {
            return handleGameLongClick(game)
        }
    }

    inner class GameListViewHolder(val binding: CardGameListBinding) :
        AbstractViewHolder<Game>(binding) {
        override fun bind(model: Game) {
            binding.imageGameScreen.scaleType = ImageView.ScaleType.CENTER_CROP
            GameIconUtils.loadGameIcon(model, binding.imageGameScreen)

            binding.textGameTitle.text = model.title.replace("[\\t\\n\\r]+".toRegex(), " ")

            binding.cardGame.setOnClickListener { onClick(model) }
            binding.cardGame.setOnLongClickListener { onLongClick(model) }
        }

        fun onClick(game: Game) {
            handleGameClick(game)
        }

        fun onLongClick(game: Game): Boolean {
            return handleGameLongClick(game)
        }
    }

    private fun handleGameClick(game: Game) {
        val gameExists = DocumentFile.fromSingleUri(
            CitronApplication.appContext,
            Uri.parse(game.path)
        )?.exists() == true
        if (!gameExists) {
            Toast.makeText(
                CitronApplication.appContext,
                R.string.loader_error_file_not_found,
                Toast.LENGTH_LONG
            ).show()

            ViewModelProvider(activity)[GamesViewModel::class.java].reloadGames(true)
            return
        }

        val preferences =
            PreferenceManager.getDefaultSharedPreferences(CitronApplication.appContext)
        preferences.edit()
            .putLong(
                game.keyLastPlayedTime,
                System.currentTimeMillis()
            )
            .apply()

        activity.lifecycleScope.launch {
            withContext(Dispatchers.IO) {
                val shortcut =
                    ShortcutInfoCompat.Builder(CitronApplication.appContext, game.path)
                        .setShortLabel(game.title)
                        .setIcon(GameIconUtils.getShortcutIcon(activity, game))
                        .setIntent(game.launchIntent)
                        .build()
                ShortcutManagerCompat.pushDynamicShortcut(CitronApplication.appContext, shortcut)
            }
        }

        val action = HomeNavigationDirections.actionGlobalEmulationActivity(game, true)
        activity.findNavController(R.id.fragment_container).navigate(action)
    }

    private fun handleGameLongClick(game: Game): Boolean {
        val action = HomeNavigationDirections.actionGlobalPerGamePropertiesFragment(game)
        activity.findNavController(R.id.fragment_container).navigate(action)
        return true
    }
}
