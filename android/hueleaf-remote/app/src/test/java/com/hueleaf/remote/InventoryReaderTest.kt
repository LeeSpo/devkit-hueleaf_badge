package com.hueleaf.remote

import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

class InventoryReaderTest {
    private fun page(start: Int = 0, total: Int = 1, token: Long = 99, labels: Boolean = true,
                     items: List<Protocol.Item> = listOf(Protocol.Item("img-123.bin", true, "花园 Garden 🌿"))): ByteArray {
        val b = ByteBuffer.allocate(506).order(ByteOrder.LITTLE_ENDIAN)
        b.putLong(token).putShort(total.toShort()).putShort(start.toShort()).putShort(items.size.toShort()).put(1)
        for (item in items) {
            val name = item.name.toByteArray(); val label = item.label.toByteArray()
            b.put(if (item.selected) 1.toByte() else 0.toByte()).put(name.size.toByte()).put(name)
            if (labels) b.put(label.size.toByte()).put(label)
        }
        return b.array().copyOf(b.position())
    }

    @Test fun labeledPageSurvivesMinimumMtuAndKeepsIdentity() {
        val item = Protocol.Item("img-123.bin", true, "花".repeat(85))
        val data = page(items = listOf(item))
        val response = byteArrayOf(1, 0x87.toByte(), 1, 0, 0, 0) + data
        val assembler = Protocol.Assembler()
        val reply = Protocol.fragments(response, 1, 23).mapNotNull { assembler.feed(it) }.single()
        assertEquals(item, Protocol.page(reply.data, labels = true).items.single())
        assertEquals("花".repeat(85), Protocol.page(reply.data, true).items.single().displayName)
        assertEquals(7, Protocol.request(7, 1)[1].toInt())
        assertArrayEquals("img-123.bin".toByteArray(), Protocol.request(5, 2, item.name.toByteArray()).copyOfRange(4, 15))
    }

    @Test fun absentBlankAndMalformedOptionalLabelsFallBack() {
        listOf("", " \t\n", "\u3000", "bad\u0000name").forEach { label ->
            val parsed = Protocol.page(page(items = listOf(Protocol.Item("a.bin", false, label))), true).items.single()
            assertEquals("a.bin", parsed.displayName)
        }
        val malformed = page(items = listOf(Protocol.Item("a.bin", false, "xx")))
        malformed[malformed.lastIndex - 1] = 0xc0.toByte(); malformed[malformed.lastIndex] = 0xaf.toByte()
        assertEquals("a.bin", Protocol.page(malformed, true).items.single().displayName)
    }

    @Test fun malformedLabelFramingIsRejected() {
        val full = page()
        for (data in listOf(full.copyOf(full.size - 1), full + byteArrayOf(0), full.copyOf(17 + "img-123.bin".length))) {
            assertThrows(IllegalArgumentException::class.java) { Protocol.page(data, true) }
        }
    }

    @Test fun oldFirmwareFallsBackOnlyOncePerSession() {
        val ops = mutableListOf<Int>()
        val reader = InventoryReader { op, payload ->
            ops.add(op)
            assertArrayEquals(Protocol.pageRequest(0, 0), payload)
            if (op == 7) throw InventoryReader.DeviceError(-3)
            page(labels = false)
        }
        assertEquals("img-123.bin", reader.read().second.single().displayName)
        assertEquals(false, reader.labelsSupported)
        reader.read()
        assertEquals(listOf(7, 3, 3), ops)
    }

    @Test fun otherErrorsNeverSilentlyDowngrade() {
        for (code in listOf(-9, -10, -7, -5)) {
            val ops = mutableListOf<Int>()
            val reader = InventoryReader { op, _ -> ops.add(op); throw InventoryReader.DeviceError(code) }
            assertEquals(code, assertThrows(InventoryReader.DeviceError::class.java) { reader.read() }.code)
            assertEquals(listOf(7), ops)
        }
        val reader = InventoryReader { _, _ -> error("transport failed") }
        assertThrows(IllegalStateException::class.java) { reader.read() }
        assertNull(reader.labelsSupported)
    }

    @Test fun expiredSnapshotRestartsAndDiscardsPartialResults() {
        var calls = 0
        val reader = InventoryReader { op, payload ->
            assertEquals(7, op)
            when (++calls) {
                1 -> page(total = 2)
                2 -> {
                    assertArrayEquals(Protocol.pageRequest(1, 99), payload)
                    throw InventoryReader.DeviceError(-11)
                }
                else -> {
                    assertArrayEquals(Protocol.pageRequest(0, 0), payload)
                    page(token = 100, items = listOf(Protocol.Item("new.bin", false, "新标签")))
                }
            }
        }
        assertEquals(listOf(Protocol.Item("new.bin", false, "新标签")), reader.read().second)
        assertEquals(true, reader.labelsSupported)
    }

    @Test fun repeatedSnapshotChangesStopAfterThreeAttempts() {
        var calls = 0
        val reader = InventoryReader { _, _ -> calls++; throw InventoryReader.DeviceError(-11) }
        assertThrows(InventoryReader.DeviceError::class.java) { reader.read() }
        assertEquals(3, calls)
    }

    @Test fun pagesKeepLabelsAndSelectionForThreeHundredImages() {
        val expected = (0 until 300).map { Protocol.Item("img-$it.bin", it % 2 == 0, "花园 $it") }
        val reader = InventoryReader { op, payload ->
            assertEquals(7, op)
            val request = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
            val start = request.short.toInt() and 65535
            assertEquals(if (start == 0) 0L else 99L, request.long)
            page(start, 300, items = expected.drop(start).take(2))
        }
        assertEquals(expected, reader.read().second)
    }

    @Test fun nonadvancingOrInconsistentPagesFail() {
        val stalled = InventoryReader { _, _ -> page(total = 1, items = emptyList()) }
        assertThrows(IllegalStateException::class.java) { stalled.read() }
        var calls = 0
        val inconsistent = InventoryReader { _, _ ->
            if (++calls == 1) page(total = 2) else page(start = 1, total = 2, token = 100)
        }
        assertThrows(IllegalStateException::class.java) { inconsistent.read() }
    }
}
