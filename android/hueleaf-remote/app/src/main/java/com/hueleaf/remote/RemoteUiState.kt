package com.hueleaf.remote

enum class ConnectionPhase { DISCONNECTED, SCANNING, CONNECTING, CONNECTED }
enum class UiTask { NONE, CONNECTING, READING, DISPLAYING, ENDING }
enum class GalleryEmptyReason { DISCONNECTED, LOADING, NOT_READY, EMPTY, NO_MATCH, NONE }

/** Presentation state only. Credentials and Bluetooth objects never belong here. */
data class RemoteUiState(
    val phase: ConnectionPhase = ConnectionPhase.DISCONNECTED,
    val task: UiTask = UiTask.NONE,
    val deviceName: String = "",
    val ready: Boolean = false,
    val battery: Int? = null,
    val count: Int = 0,
    val currentIndex: Int = -1,
    val currentName: String = "",
    val deviceState: String = "",
    val items: List<Protocol.Item> = emptyList(),
    val inventoryLoaded: Boolean = false,
    val inventoryFlags: Int = 0,
    val progress: String = "",
    val error: String? = null,
    val notice: String? = null
) {
    val connected: Boolean get() = phase == ConnectionPhase.CONNECTED
    val busy: Boolean get() = task != UiTask.NONE
    val canRefresh: Boolean get() = connected && !busy
    val canDisplay: Boolean get() = canRefresh && ready && count > 0
    val canEditCredentials: Boolean get() = phase == ConnectionPhase.DISCONNECTED && !busy
    val currentDisplayName: String get() = items.firstOrNull { it.name == currentName }?.displayName ?: currentName

    fun visibleItems(query: String, playlistOnly: Boolean): List<Protocol.Item> {
        val term = query.trim()
        return items.filter { (!playlistOnly || it.selected) &&
            (it.displayName.contains(term, ignoreCase = true) || it.name.contains(term, ignoreCase = true)) }
    }

    fun galleryEmptyReason(query: String, playlistOnly: Boolean): GalleryEmptyReason = when {
        phase == ConnectionPhase.CONNECTING -> GalleryEmptyReason.LOADING
        !connected -> GalleryEmptyReason.DISCONNECTED
        task == UiTask.READING && !inventoryLoaded -> GalleryEmptyReason.LOADING
        !ready -> GalleryEmptyReason.NOT_READY
        !inventoryLoaded -> GalleryEmptyReason.LOADING
        items.isEmpty() -> GalleryEmptyReason.EMPTY
        visibleItems(query, playlistOnly).isEmpty() -> GalleryEmptyReason.NO_MATCH
        else -> GalleryEmptyReason.NONE
    }
}
