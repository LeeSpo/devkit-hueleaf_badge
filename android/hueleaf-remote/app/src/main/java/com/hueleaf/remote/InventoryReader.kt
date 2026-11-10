package com.hueleaf.remote

/** One authenticated session; keeps capability fallback separate from GATT. */
class InventoryReader(private val call: (Int, ByteArray) -> ByteArray) {
    open class DeviceError(val code: Int, message: String = "设备返回错误 $code") : Exception(message)

    var labelsSupported: Boolean? = null
        private set

    fun read(): Pair<Int, List<Protocol.Item>> {
        var retries = 0
        while (true) {
            val labels = labelsSupported != false
            try {
                var start = 0; var token = 0L; var flags = 0; var total: Int? = null
                val all = mutableListOf<Protocol.Item>()
                do {
                    val page = Protocol.page(call(if (labels) 7 else 3, Protocol.pageRequest(start, token)), labels)
                    check(page.start == start && (start == 0 || (page.token == token && page.total == total && page.flags == flags))) {
                        "图库分页不一致"
                    }
                    if (labels) labelsSupported = true
                    token = page.token; flags = page.flags; total = page.total
                    all.addAll(page.items); start += page.items.size
                    check(start >= page.total || page.items.isNotEmpty()) { "图库分页未前进" }
                } while (start < page.total)
                return flags to all
            } catch (e: DeviceError) {
                if (labels && e.code == -3) {
                    labelsSupported = false
                    continue
                }
                if (e.code != -11 || retries++ >= 2) throw e
            }
        }
    }
}
