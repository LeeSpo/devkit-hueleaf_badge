package com.hueleaf.remote

import org.junit.Assert.*
import org.junit.Test

class RemoteUiStateTest {
    @Test fun labelsDriveSearchAndCurrentTitleButNeverReplaceFilenameIdentity() {
        val sameLabel = listOf(Protocol.Item("img-a.bin", true, "花园"), Protocol.Item("img-b.bin", false, "花园"))
        val state = RemoteUiState(items = sameLabel, currentName = "img-b.bin")
        assertEquals("花园", state.currentDisplayName)
        assertEquals(sameLabel, state.visibleItems("花园", false))
        assertEquals(listOf(sameLabel[0]), state.visibleItems("花园", true))
        assertEquals(listOf(sameLabel[1]), state.visibleItems("IMG-B", false))
        assertEquals("unknown.bin", state.copy(currentName = "unknown.bin").currentDisplayName)
        assertEquals("img-b.bin", sameLabel[1].name)
    }
    private val items = listOf(
        Protocol.Item("夏日花园.bin", true),
        Protocol.Item("Mountain.BIN", false),
        Protocol.Item("花园夜色.bin", false)
    )
    private val connected = RemoteUiState(
        phase = ConnectionPhase.CONNECTED, ready = true, count = 3,
        currentIndex = 0, currentName = items[0].name, items = items, inventoryLoaded = true
    )

    @Test fun controlsRequireAuthenticatedReadySessionWithImages() {
        assertFalse(RemoteUiState().canDisplay)
        assertFalse(connected.copy(phase = ConnectionPhase.CONNECTING).canDisplay)
        assertFalse(connected.copy(ready = false).canDisplay)
        assertFalse(connected.copy(count = 0).canDisplay)
        assertTrue(connected.canDisplay)
        assertTrue(connected.copy(ready = false).canRefresh)
        assertTrue(connected.copy(count = 0).canRefresh)
    }

    @Test fun everyInFlightTaskBlocksDuplicateDisplayRefreshAndCredentialChanges() {
        UiTask.values().filter { it != UiTask.NONE }.forEach { task ->
            val state = connected.copy(task = task)
            assertFalse(task.name, state.canDisplay)
            assertFalse(task.name, state.canRefresh)
            assertFalse(task.name, state.canEditCredentials)
        }
    }

    @Test fun scanAndConnectionNeverAllowChangingCredentials() {
        assertTrue(RemoteUiState().canEditCredentials)
        listOf(ConnectionPhase.SCANNING, ConnectionPhase.CONNECTING, ConnectionPhase.CONNECTED).forEach {
            assertFalse(RemoteUiState(phase = it).canEditCredentials)
        }
    }

    @Test fun searchMatchesChineseAndIsCaseInsensitiveWithWhitespaceTrimmed() {
        assertEquals(listOf(items[0], items[2]), connected.visibleItems(" 花园 ", false))
        assertEquals(listOf(items[1]), connected.visibleItems("mountain.bin", false))
        assertEquals(items, connected.visibleItems(" ", false))
    }

    @Test fun playlistFilterIntersectsSearchWithoutChangingDeviceOrderOrMembership() {
        assertEquals(listOf(items[0]), connected.visibleItems("花园", true))
        assertTrue(connected.visibleItems("mountain", true).isEmpty())
        assertEquals(items, connected.items)
        assertFalse(connected.items[1].selected)
    }

    @Test fun disconnectedLoadingNotReadyEmptyAndNoMatchAreDifferentStates() {
        assertEquals(GalleryEmptyReason.DISCONNECTED, RemoteUiState().galleryEmptyReason("", false))
        assertEquals(GalleryEmptyReason.LOADING, RemoteUiState(phase = ConnectionPhase.CONNECTING).galleryEmptyReason("", false))
        assertEquals(GalleryEmptyReason.LOADING, connected.copy(task = UiTask.READING, inventoryLoaded = false).galleryEmptyReason("", false))
        assertEquals(GalleryEmptyReason.NOT_READY, connected.copy(ready = false).galleryEmptyReason("", false))
        assertEquals(GalleryEmptyReason.EMPTY, connected.copy(items = emptyList(), count = 0).galleryEmptyReason("", false))
        assertEquals(GalleryEmptyReason.NO_MATCH, connected.galleryEmptyReason("not-found", false))
        assertEquals(GalleryEmptyReason.NONE, connected.galleryEmptyReason("", false))
    }

    @Test fun refreshKeepsExistingListAndCurrentImageUntilNewDeviceResultsArrive() {
        val waiting = connected.copy(task = UiTask.DISPLAYING)
        assertEquals(items[0].name, waiting.currentName)
        assertEquals(GalleryEmptyReason.NONE, waiting.galleryEmptyReason("", false))
        assertFalse(waiting.canDisplay)
        val complete = waiting.copy(task = UiTask.NONE, currentIndex = 1, currentName = items[1].name)
        assertTrue(complete.canDisplay)
        assertEquals(items[1].name, complete.currentName)
    }
}
