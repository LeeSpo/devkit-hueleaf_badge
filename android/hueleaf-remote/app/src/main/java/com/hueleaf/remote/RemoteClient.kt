package com.hueleaf.remote

import android.annotation.SuppressLint
import android.bluetooth.*
import android.content.Context
import android.content.BroadcastReceiver
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.util.Log
import java.util.UUID
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec
import org.json.JSONObject

@SuppressLint("MissingPermission")
class RemoteClient(
    private val context: Context,
    private val device: BluetoothDevice,
    private val onProgress: (String) -> Unit = {},
    private val onDisconnected: () -> Unit = {}
) {
    companion object {
        fun uuid(n: Int): UUID = UUID.fromString("8f51000$n-6c8f-4d84-9af8-6e1f9000a001")
        val SERVICE: UUID = uuid(1)
        private val CCC = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
    private data class Event(val type: String, val status: Int = 0, val bytes: ByteArray = byteArrayOf(), val mtu: Int = 23)
    private val events = LinkedBlockingQueue<Event>()
    private val replies = LinkedBlockingQueue<Any>()
    private val bonds = LinkedBlockingQueue<Int>()
    private fun progress(text: String) { Log.i("HueLeafBLE", text); onProgress(text) }
    private val bondReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            @Suppress("DEPRECATION") val peer = intent.getParcelableExtra<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE)
            if (peer?.address != device.address) return
            val state = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.ERROR)
            Log.i("HueLeafBLE", "bond state=$state")
            if (state == BluetoothDevice.BOND_BONDING) progress("请确认系统蓝牙配对请求（无需输入 4 位 PIN）……")
            bonds.offer(state)
        }
    }
    private val assembler = Protocol.Assembler()
    private var gatt: BluetoothGatt? = null
    private lateinit var rx: BluetoothGattCharacteristic
    private var mtu = 23
    private var requestId = 0
    @Volatile private var closed = false
    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, state: Int) {
            Log.i("HueLeafBLE", "connection state=$state status=$status")
            if (state == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) events.offer(Event("connected"))
            else if (state == BluetoothProfile.STATE_DISCONNECTED || status != BluetoothGatt.GATT_SUCCESS) {
                events.offer(Event("disconnected", status)); replies.offer(IllegalStateException("设备已断开（$status）")); onDisconnected()
            }
        }
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) { events.offer(Event("services", status)) }
        override fun onMtuChanged(g: BluetoothGatt, value: Int, status: Int) { events.offer(Event("mtu", status, mtu = value)) }
        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) { events.offer(Event("descriptor", status)) }
        @Deprecated("Android callback") override fun onCharacteristicRead(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            events.offer(Event("read", status, c.value?.clone() ?: byteArrayOf()))
        }
        override fun onCharacteristicRead(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray, status: Int) {
            events.offer(Event("read", status, value.clone()))
        }
        @Deprecated("Android callback") override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) { receive(c.value?.clone() ?: byteArrayOf()) }
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) { receive(value.clone()) }
        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) { events.offer(Event("write", status)) }
    }
    private fun receive(bytes: ByteArray) {
        try { assembler.feed(bytes)?.let { replies.offer(it) } } catch (e: Exception) { replies.offer(e) }
    }
    private fun await(type: String, seconds: Long = 30): Event {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(seconds)
        while (true) {
            val remaining = deadline - System.nanoTime()
            val event = if (remaining > 0) events.poll(remaining, TimeUnit.NANOSECONDS) else null
            if (event == null && type == "mtu") return Event("mtu", mtu = mtu)
            checkNotNull(event) { "蓝牙操作超时：$type；请靠近相框并重新连接" }
            if (event.type == "disconnected") error("相框已断开（${event.status}）。请双击唤醒后重连；首次连接请确认系统配对。")
            // Android may send unsolicited MTU changes between GATT operations.
            if (event.type == "mtu") {
                if (event.status == BluetoothGatt.GATT_SUCCESS) mtu = event.mtu
                if (type == "mtu") return Event("mtu", mtu = mtu)
                continue
            }
            check(event.type == type && event.status == BluetoothGatt.GATT_SUCCESS) { "蓝牙操作失败：$type（${event.status}）" }
            return event
        }
    }
    private fun ensureBond() {
        if (device.bondState == BluetoothDevice.BOND_BONDED) return
        progress("首次连接：请确认系统蓝牙配对请求……")
        if (device.bondState != BluetoothDevice.BOND_BONDING) check(device.createBond()) { "无法发起系统配对，请在系统蓝牙中取消旧配对后重试" }
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(60)
        while (!closed && device.bondState != BluetoothDevice.BOND_BONDED) {
            val remaining = deadline - System.nanoTime()
            check(remaining > 0) { "系统配对超时，请确认配对弹窗后重试" }
            val state = bonds.poll(minOf(remaining, TimeUnit.SECONDS.toNanos(1)), TimeUnit.NANOSECONDS)
            check(state != BluetoothDevice.BOND_NONE) { "系统配对未完成，请重新连接并确认配对请求" }
        }
        check(!closed) { "连接已取消" }
    }
    fun connect(key: ByteArray) {
        check(key.size == 32)
        if (Build.VERSION.SDK_INT >= 33) context.registerReceiver(bondReceiver, IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED), Context.RECEIVER_EXPORTED)
        else context.registerReceiver(bondReceiver, IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED))
        try {
        progress("正在连接相框……")
        val g = synchronized(this) {
            check(!closed) { "连接已关闭" }
            (device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE) ?: error("无法连接设备")).also { gatt = it }
        }
        await("connected")
        ensureBond()
        progress("正在发现遥控服务……")
        check(g.discoverServices()); await("services")
        if (g.requestMtu(64)) mtu = await("mtu", 8).mtu
        val service = g.getService(SERVICE) ?: error("未发现 HueLeaf 服务")
        rx = service.getCharacteristic(uuid(2)) ?: error("缺少 RX")
        val tx = service.getCharacteristic(uuid(3)) ?: error("缺少 TX")
        check(g.setCharacteristicNotification(tx, true))
        val descriptor = tx.getDescriptor(CCC) ?: error("缺少 indication 描述符")
        @Suppress("DEPRECATION") descriptor.value = BluetoothGattDescriptor.ENABLE_INDICATION_VALUE
        @Suppress("DEPRECATION") check(g.writeDescriptor(descriptor)); await("descriptor")
        val challenge = service.getCharacteristic(uuid(4)) ?: error("缺少认证信息")
        check(g.readCharacteristic(challenge))
        val nonce = await("read").bytes
        check(nonce.size == 33 && nonce[0].toInt() == 1) { "相框尚未配置遥控 PIN，请在电脑设置后重启相框" }
        progress("正在验证 PIN……")
        val mac = Mac.getInstance("HmacSHA256"); mac.init(SecretKeySpec(key, "HmacSHA256"))
        call(1, mac.doFinal(nonce.copyOfRange(1, 33)))
        progress("认证成功，正在读取图库……")
        } finally { context.unregisterReceiver(bondReceiver) }
    }
    class RemoteError(val code: Int) : Exception(when (code) {
        -10 -> "PIN 不匹配或认证已超时，请检查 PIN，并确认电脑设置后已重启相框"
        -9 -> "相框正在刷新，请稍后重试"
        else -> "设备返回错误 $code"
    })
    private fun nextReply(seconds: Long): Protocol.Reply {
        val value = replies.poll(seconds, TimeUnit.SECONDS) ?: error("等待设备应答超时")
        if (value is Exception) throw value
        return value as Protocol.Reply
    }
    private fun call(op: Int, payload: ByteArray = byteArrayOf()): Pair<Int, ByteArray> {
        requestId = requestId % 65535 + 1
        val id = requestId
        val g = gatt ?: error("设备未连接")
        for (part in Protocol.fragments(Protocol.request(op, id, payload), id, mtu)) {
            @Suppress("DEPRECATION") rx.value = part
            rx.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            @Suppress("DEPRECATION") check(g.writeCharacteristic(rx)) { "无法发送蓝牙数据" }
            await("write")
        }
        val reply = nextReply(30)
        check(reply.op == (op or 0x80) && reply.id == id) { "收到不匹配的应答" }
        if (reply.status != 0) throw RemoteError(reply.status)
        return id to reply.data
    }
    fun status(): JSONObject = JSONObject(call(2).second.toString(Charsets.UTF_8))
    fun inventory(): Pair<Int, List<Protocol.Item>> {
        repeat(3) {
            try {
                var start = 0; var token = 0L; var flags = 0
                val all = mutableListOf<Protocol.Item>()
                do {
                    val page = Protocol.page(call(3, Protocol.pageRequest(start, token)).second)
                    check(page.start == start && (start == 0 || page.token == token)) { "图库分页不一致" }
                    token = page.token; flags = page.flags; all.addAll(page.items); start += page.items.size
                    check(start >= page.total || page.items.isNotEmpty()) { "图库分页未前进" }
                } while (start < page.total)
                return flags to all
            } catch (e: RemoteError) { if (e.code != -11 || it == 2) throw e }
        }
        error("图库持续变化")
    }
    fun display(op: Int, payload: ByteArray) {
        val id = call(op, payload).first
        val event = nextReply(120)
        check(event.op == 0xc0 && event.id == id) { "收到不匹配的刷新事件" }
        check(event.status == 0) { "屏幕刷新失败（${event.status}）" }
    }
    fun end() { call(6) }
    @Synchronized fun close() {
        if (!closed) {
            closed = true
            events.offer(Event("disconnected")); replies.offer(IllegalStateException("连接已关闭"))
            gatt?.disconnect(); gatt?.close(); gatt = null
        }
    }
}
