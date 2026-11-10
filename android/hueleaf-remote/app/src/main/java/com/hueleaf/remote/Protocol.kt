package com.hueleaf.remote

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

object Protocol {
    fun pinKey(pin: String): ByteArray {
        require(pin.matches(Regex("[0-9]{4}"))) { "请输入4位数字 PIN" }
        return java.security.MessageDigest.getInstance("SHA-256")
            .digest(("HueLeaf remote PIN v1:" + pin).toByteArray(Charsets.US_ASCII))
    }
    fun request(op: Int, id: Int, payload: ByteArray = byteArrayOf()): ByteArray {
        require(op in 1..6 && id in 1..65535 && payload.size <= 508)
        return byteArrayOf(1, op.toByte(), id.toByte(), (id shr 8).toByte()) + payload
    }

    fun fragments(data: ByteArray, id: Int, mtu: Int): List<ByteArray> {
        require(data.size in 4..512 && mtu >= 23)
        val size = minOf(mtu - 7, 244)
        return data.asList().chunked(size).mapIndexed { seq, chunk ->
            val flags = (if (seq == 0) 1 else 0) or (if ((seq + 1) * size >= data.size) 2 else 0)
            byteArrayOf(id.toByte(), seq.toByte(), flags.toByte(), 0) + chunk.toByteArray()
        }
    }

    data class Reply(val op: Int, val id: Int, val status: Int, val data: ByteArray)
    class Assembler {
        private var stream = -1
        private var seq = 0
        private val data = ByteArrayOutputStream()
        fun feed(packet: ByteArray): Reply? {
            require(packet.size >= 5 && packet[3].toInt() == 0 && (packet[2].toInt() and 0xfc) == 0) { "无效分片" }
            val s = packet[0].toInt() and 255
            val n = packet[1].toInt() and 255
            val flags = packet[2].toInt() and 255
            if (flags and 1 != 0) {
                require(n == 0 && stream == -1) { "分片开始错误" }
                stream = s; seq = 0; data.reset()
            }
            require(stream == s && seq == n && data.size() + packet.size - 4 <= 512) { "分片丢失或乱序" }
            data.write(packet, 4, packet.size - 4); seq++
            if (flags and 2 == 0) return null
            val result = data.toByteArray(); stream = -1
            require(result.size >= 6 && result[0].toInt() == 1) { "无效应答" }
            val b = ByteBuffer.wrap(result).order(ByteOrder.LITTLE_ENDIAN)
            return Reply(result[1].toInt() and 255, b.getShort(2).toInt() and 65535, b.getShort(4).toInt(), result.copyOfRange(6, result.size))
        }
    }
    data class Item(val name: String, val selected: Boolean)
    data class Page(val token: Long, val total: Int, val start: Int, val flags: Int, val items: List<Item>)
    fun page(data: ByteArray): Page {
        require(data.size >= 15) { "图库数据不完整" }
        val b = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
        val token = b.long; val total = b.short.toInt() and 65535; val start = b.short.toInt() and 65535
        val count = b.short.toInt() and 65535; val flags = b.get().toInt() and 255
        require(start <= total && count <= total - start)
        val items = (0 until count).map {
            require(b.remaining() >= 2)
            val selected = b.get().toInt() != 0; val size = b.get().toInt() and 255
            require(size in 1..63 && b.remaining() >= size)
            val bytes = ByteArray(size); b.get(bytes)
            val decoder = Charsets.UTF_8.newDecoder()
            Item(decoder.decode(ByteBuffer.wrap(bytes)).toString(), selected)
        }
        require(!b.hasRemaining())
        return Page(token, total, start, flags, items)
    }
    fun pageRequest(start: Int, token: Long): ByteArray = ByteBuffer.allocate(10).order(ByteOrder.LITTLE_ENDIAN).putShort(start.toShort()).putLong(token).array()
}
