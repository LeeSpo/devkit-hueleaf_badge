package com.hueleaf.remote

import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

class ProtocolTest {
    @Test fun fourDigitPinMatchesUsbProvisioningVector() {
        val hex = Protocol.pinKey("0123").joinToString("") { "%02x".format(it.toInt() and 255) }
        assertEquals("444499cd627ce855496569777bea9ba3e96d9c4cdcd6001846343747b4d1db93", hex)
        listOf("123", "12345", "１２３４", "12a4").forEach { pin ->
            try { Protocol.pinKey(pin); fail("invalid PIN accepted") } catch (_: IllegalArgumentException) { }
        }
    }
    @Test fun navMatchesDocumentedWireExample() {
        val frame = Protocol.fragments(Protocol.request(4, 2, byteArrayOf(1)), 2, 23).single()
        assertArrayEquals(byteArrayOf(2, 0, 3, 0, 1, 4, 2, 0, 1), frame)
    }
    @Test fun fragmentedResponseAtMinimumMtuReassembles() {
        val response = byteArrayOf(1, 0x83.toByte(), 7, 0, 0, 0) + ByteArray(80) { it.toByte() }
        val assembler = Protocol.Assembler()
        val parts = Protocol.fragments(response, 19, 23)
        assertTrue(parts.size > 1)
        parts.dropLast(1).forEach { assertNull(assembler.feed(it)) }
        val reply = assembler.feed(parts.last())!!
        assertEquals(0x83, reply.op); assertEquals(7, reply.id); assertEquals(0, reply.status)
        assertArrayEquals(response.copyOfRange(6, response.size), reply.data)
    }
    @Test fun rejectsLostOrOutOfOrderFragments() {
        val parts = Protocol.fragments(ByteArray(80) { 1 }, 1, 23)
        val assembler = Protocol.Assembler()
        assembler.feed(parts[0])
        try { assembler.feed(parts[2]); fail("missing fragment must fail") } catch (_: IllegalArgumentException) { }
    }
    @Test fun parsesPagedInventoryAndUtf8() {
        val name = "花.bin".toByteArray(Charsets.UTF_8)
        val b = ByteBuffer.allocate(17 + name.size).order(ByteOrder.LITTLE_ENDIAN)
        b.putLong(0x123456789L).putShort(300.toShort()).putShort(64.toShort()).putShort(1.toShort()).put(1)
        b.put(1).put(name.size.toByte()).put(name)
        val page = Protocol.page(b.array())
        assertEquals(300, page.total); assertEquals(64, page.start); assertEquals(0x123456789L, page.token)
        assertEquals("花.bin", page.items.single().name); assertTrue(page.items.single().selected)
        assertArrayEquals(byteArrayOf(64, 0, 0x89.toByte(), 0x67, 0x45, 0x23, 1, 0, 0, 0), Protocol.pageRequest(64, page.token))
    }
    @Test fun rejectsTruncatedAndTrailingInventoryData() {
        val b = ByteBuffer.allocate(18).order(ByteOrder.LITTLE_ENDIAN)
        b.putLong(1).putShort(1.toShort()).putShort(0.toShort()).putShort(1.toShort()).put(0).put(1).put(2).put(65)
        try { Protocol.page(b.array()); fail("truncated name must fail") } catch (_: IllegalArgumentException) { }
        val empty = ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN).putLong(1).putShort(0.toShort()).putShort(0.toShort()).putShort(0.toShort()).put(0).put(0).array()
        try { Protocol.page(empty); fail("trailing byte must fail") } catch (_: IllegalArgumentException) { }
    }
}
