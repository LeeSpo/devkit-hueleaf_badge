package com.hueleaf.remote

import android.content.res.ColorStateList
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.color.MaterialColors
import com.hueleaf.remote.databinding.ItemGalleryBinding

class GalleryAdapter(private val onShow: (String) -> Unit) : ListAdapter<GalleryAdapter.Row, GalleryAdapter.Holder>(Diff) {
    data class Row(val item: Protocol.Item, val current: Boolean, val enabled: Boolean)
    class Holder(val binding: ItemGalleryBinding) : RecyclerView.ViewHolder(binding.root)

    private object Diff : DiffUtil.ItemCallback<Row>() {
        override fun areItemsTheSame(oldItem: Row, newItem: Row) = oldItem.item.name == newItem.item.name
        override fun areContentsTheSame(oldItem: Row, newItem: Row) = oldItem == newItem
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int) = Holder(
        ItemGalleryBinding.inflate(LayoutInflater.from(parent.context), parent, false)
    )

    override fun onBindViewHolder(holder: Holder, position: Int) {
        val row = getItem(position)
        val b = holder.binding
        val context = b.root.context
        val current = context.getString(if (row.current) R.string.current_badge else R.string.image_idle)
        val playlist = context.getString(if (row.item.selected) R.string.playlist_badge else R.string.not_playlist)
        b.itemName.text = row.item.displayName
        b.itemBadges.text = if (row.current) context.getString(R.string.badge_pair, current, playlist) else playlist
        b.root.contentDescription = context.getString(R.string.show_image, row.item.displayName, current, playlist)
        b.root.isEnabled = row.enabled
        b.root.alpha = if (row.enabled) 1f else 0.55f
        b.root.setCardBackgroundColor(MaterialColors.getColor(b.root,
            if (row.current) com.google.android.material.R.attr.colorSecondaryContainer else com.google.android.material.R.attr.colorSurface))
        b.root.strokeWidth = if (row.current) 0 else (context.resources.displayMetrics.density).toInt().coerceAtLeast(1)
        b.root.setStrokeColor(MaterialColors.getColor(b.root, com.google.android.material.R.attr.colorOutline).let { ColorStateList.valueOf(it) })
        b.itemIcon.setImageResource(if (row.current) R.drawable.ic_check else R.drawable.ic_frame)
        b.root.setOnClickListener { if (row.enabled) onShow(row.item.name) }
    }
}
