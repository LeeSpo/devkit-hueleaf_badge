package com.hueleaf.remote

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.view.WindowManager
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.widget.doAfterTextChanged
import androidx.core.content.edit
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import androidx.viewpager2.widget.ViewPager2
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.button.MaterialButton
import com.google.android.material.color.MaterialColors
import com.google.android.material.snackbar.Snackbar
import com.google.android.material.tabs.TabLayoutMediator
import com.hueleaf.remote.databinding.ActivityMainBinding
import com.hueleaf.remote.databinding.SheetConnectionBinding
import com.hueleaf.remote.databinding.PageRemoteBinding
import com.hueleaf.remote.databinding.PageGalleryBinding
import org.json.JSONObject
import java.util.concurrent.Executors

@SuppressLint("MissingPermission")
class MainActivity : AppCompatActivity() {
    private val worker = Executors.newSingleThreadExecutor()
    private val main = Handler(Looper.getMainLooper())
    private val devices = linkedMapOf<String, BluetoothDevice>()
    private val preferences by lazy { getSharedPreferences("connection", MODE_PRIVATE) }
    private var client: RemoteClient? = null
    @Volatile private var pending: RemoteClient? = null
    @Volatile private var foreground = false
    private var scanCallback: ScanCallback? = null
    private var scanGeneration = 0
    private var operationGeneration = 0
    private var deferredCompletion: (() -> Unit)? = null
    private var connectionSheet: BottomSheetDialog? = null
    private var sheet: SheetConnectionBinding? = null
    private var hasCredential = false
    private var ui = RemoteUiState()
    private var query = ""
    private var playlistOnly = false
    private lateinit var keys: KeyStore
    private lateinit var binding: ActivityMainBinding
    private lateinit var remotePage: PageRemoteBinding
    private lateinit var galleryPage: PageGalleryBinding
    private var tabMediator: TabLayoutMediator? = null
    private val galleryAdapter = GalleryAdapter { name -> show(5, name.toByteArray(Charsets.UTF_8)) }
    private val adapter: BluetoothAdapter? get() = (getSystemService(BLUETOOTH_SERVICE) as BluetoothManager).adapter

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        keys = KeyStore(this)
        hasCredential = credentialAvailable()
        binding = ActivityMainBinding.inflate(layoutInflater)
        remotePage = PageRemoteBinding.inflate(layoutInflater)
        galleryPage = PageGalleryBinding.inflate(layoutInflater)
        setContentView(binding.root)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val light = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK != Configuration.UI_MODE_NIGHT_YES
        WindowCompat.getInsetsController(window, binding.root).apply {
            isAppearanceLightStatusBars = light
            isAppearanceLightNavigationBars = light
        }
        ViewCompat.setOnApplyWindowInsetsListener(binding.contentRoot) { view, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
            val ime = insets.getInsets(WindowInsetsCompat.Type.ime())
            view.setPadding(bars.left, bars.top, bars.right, maxOf(bars.bottom, ime.bottom))
            insets
        }
        binding.toolbar.setOnMenuItemClickListener { showConnection(); true }
        val pages = listOf(remotePage.root, galleryPage.root)
        binding.pager.adapter = object : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
            override fun getItemCount() = pages.size
            override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder =
                object : RecyclerView.ViewHolder(FrameLayout(parent.context).apply {
                    layoutParams = ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
                }) {}
            override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
                val container = holder.itemView as FrameLayout
                val page = pages[position]
                (page.parent as? ViewGroup)?.removeView(page)
                container.removeAllViews()
                container.addView(page)
            }
        }
        binding.pager.offscreenPageLimit = 1
        tabMediator = TabLayoutMediator(binding.tabs, binding.pager) { tab, position ->
            tab.setText(if (position == 0) R.string.tab_remote else R.string.tab_gallery)
        }.also { it.attach() }
        binding.pager.registerOnPageChangeCallback(object : ViewPager2.OnPageChangeCallback() {
            override fun onPageSelected(position: Int) { hideKeyboard() }
        })
        remotePage.connect.setOnClickListener { showConnection() }
        remotePage.previous.setOnClickListener { show(4, byteArrayOf(-1)) }
        remotePage.next.setOnClickListener { show(4, byteArrayOf(1)) }
        remotePage.refresh.setOnClickListener { refresh() }
        remotePage.endSession.setOnClickListener { endSession() }
        binding.retry.setOnClickListener { if (ui.connected) refresh() else showConnection() }
        galleryPage.emptyAction.setOnClickListener {
            when (ui.galleryEmptyReason(query, playlistOnly)) {
                GalleryEmptyReason.DISCONNECTED -> showConnection()
                GalleryEmptyReason.NO_MATCH -> {
                    galleryPage.search.text?.clear()
                    galleryPage.galleryFilter.check(R.id.filter_all)
                }
                else -> refresh()
            }
        }
        galleryPage.galleryList.layoutManager = LinearLayoutManager(this)
        galleryPage.galleryList.adapter = galleryAdapter
        galleryPage.galleryList.itemAnimator = null
        galleryPage.search.doAfterTextChanged { query = it?.toString().orEmpty(); renderGallery() }
        galleryPage.galleryFilter.addOnButtonCheckedListener { _, checkedId, checked ->
            if (checked) { playlistOnly = checkedId == R.id.filter_playlist; renderGallery() }
        }
        galleryPage.search.setText(savedInstanceState?.getString("query").orEmpty())
        galleryPage.galleryFilter.check(if (savedInstanceState?.getBoolean("playlist") == true) R.id.filter_playlist else R.id.filter_all)
        binding.pager.setCurrentItem(savedInstanceState?.getInt("tab") ?: 0, false)
        render()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putInt("tab", binding.tabs.selectedTabPosition)
        outState.putString("query", query)
        outState.putBoolean("playlist", playlistOnly)
        super.onSaveInstanceState(outState)
    }

    private fun View.visible(value: Boolean) { visibility = if (value) View.VISIBLE else View.GONE }
    private fun hideKeyboard() { WindowCompat.getInsetsController(window, binding.root).hide(WindowInsetsCompat.Type.ime()); currentFocus?.clearFocus() }
    private fun credentialAvailable(): Boolean = try { keys.load()?.let { it.fill(0); true } ?: false } catch (_: Exception) { false }

    private fun render() {
        binding.toolbar.subtitle = if (ui.connected) getString(R.string.connected_device, ui.deviceName) else getString(R.string.app_subtitle)
        remotePage.deviceTitle.setText(when (ui.phase) {
            ConnectionPhase.DISCONNECTED -> R.string.device_disconnected
            ConnectionPhase.SCANNING -> R.string.device_scanning
            ConnectionPhase.CONNECTING -> R.string.device_connecting
            ConnectionPhase.CONNECTED -> R.string.device_connected
        })
        remotePage.deviceDetail.text = when {
            ui.connected -> ui.deviceName.ifBlank { getString(R.string.connected_detail) }
            ui.phase == ConnectionPhase.SCANNING -> getString(R.string.devices_scanning)
            ui.phase == ConnectionPhase.CONNECTING -> getString(R.string.connection_detail)
            else -> getString(R.string.device_hint)
        }
        remotePage.deviceStats.visible(ui.connected)
        val battery = ui.battery?.takeIf { it in 0..100 }?.let { getString(R.string.battery_value, it) } ?: getString(R.string.battery_unknown)
        remotePage.deviceStats.text = getString(R.string.device_stats, battery, getString(R.string.image_count, ui.count), deviceStateLabel())
        remotePage.currentName.text = when {
            !ui.connected -> getString(R.string.await_connection)
            !ui.ready -> getString(R.string.gallery_not_ready)
            ui.currentName.isBlank() -> getString(R.string.no_current_image)
            else -> ui.currentDisplayName
        }
        remotePage.currentPosition.text = when {
            !ui.connected -> getString(R.string.await_connection_detail)
            ui.currentIndex in 0 until ui.count -> getString(R.string.image_position, ui.currentIndex + 1, ui.count)
            else -> getString(R.string.image_count, ui.count)
        }
        remotePage.connect.visible(!ui.connected)
        remotePage.connect.isEnabled = !ui.busy || pending != null
        remotePage.connect.setText(if (ui.phase == ConnectionPhase.SCANNING || ui.phase == ConnectionPhase.CONNECTING) R.string.connection_progress else R.string.connect)
        remotePage.navigationControls.visible(ui.connected)
        remotePage.previous.isEnabled = ui.canDisplay
        remotePage.next.isEnabled = ui.canDisplay
        remotePage.refresh.visible(ui.connected)
        remotePage.refresh.isEnabled = ui.canRefresh
        remotePage.endSession.visible(ui.connected)
        remotePage.endSession.isEnabled = ui.canRefresh
        remotePage.controlHint.setText(when {
            !ui.connected -> R.string.wake_hint
            !ui.ready -> R.string.not_ready_detail
            ui.count == 0 -> R.string.no_images_detail
            else -> R.string.ready_detail
        })
        val working = ui.busy || ui.phase == ConnectionPhase.SCANNING
        binding.feedbackCard.visible(working || ui.error != null || ui.notice != null)
        binding.feedbackTitle.setText(when {
            ui.error != null -> R.string.operation_failed
            ui.task == UiTask.DISPLAYING -> R.string.displaying_title
            ui.task == UiTask.READING -> R.string.reading_title
            ui.task == UiTask.ENDING -> R.string.ending_title
            ui.phase == ConnectionPhase.CONNECTING -> R.string.device_connecting
            ui.phase == ConnectionPhase.SCANNING -> R.string.device_scanning
            else -> R.string.notice_title
        })
        binding.feedbackDetail.text = ui.error ?: ui.notice ?: ui.progress
        val background = if (ui.error != null) com.google.android.material.R.attr.colorErrorContainer else com.google.android.material.R.attr.colorSecondaryContainer
        val foregroundColor = if (ui.error != null) com.google.android.material.R.attr.colorOnErrorContainer else com.google.android.material.R.attr.colorOnSecondaryContainer
        binding.feedbackCard.setCardBackgroundColor(MaterialColors.getColor(binding.root, background))
        binding.feedbackTitle.setTextColor(MaterialColors.getColor(binding.root, foregroundColor))
        binding.feedbackDetail.setTextColor(MaterialColors.getColor(binding.root, foregroundColor))
        binding.taskProgress.visible(working)
        binding.retry.visible(ui.error != null && !working)
        renderGallery()
        renderSheet()
    }

    private fun deviceStateLabel(): String = getString(when (ui.deviceState) {
        "starting" -> R.string.state_starting
        "fault" -> R.string.state_fault
        "sleeping" -> R.string.state_sleeping
        "refreshing" -> R.string.state_refreshing
        "uploading" -> R.string.state_uploading
        "maintenance" -> R.string.state_maintenance
        else -> R.string.state_idle
    })

    private fun renderGallery() {
        val visibleItems = ui.visibleItems(query, playlistOnly)
        val reason = ui.galleryEmptyReason(query, playlistOnly)
        galleryPage.galleryTools.visible(ui.connected && ui.inventoryLoaded)
        galleryPage.galleryWarning.visible(ui.connected && ui.inventoryFlags and 2 != 0)
        galleryPage.galleryList.visible(reason == GalleryEmptyReason.NONE)
        galleryPage.galleryEmpty.visible(reason != GalleryEmptyReason.NONE)
        galleryPage.gallerySummary.text = getString(R.string.gallery_results_hint, visibleItems.size, ui.items.size)
        galleryAdapter.submitList(visibleItems.map { GalleryAdapter.Row(it, it.name == ui.currentName, ui.canDisplay) })
        galleryPage.emptyTitle.setText(when (reason) {
            GalleryEmptyReason.DISCONNECTED -> R.string.gallery_empty_title
            GalleryEmptyReason.LOADING -> R.string.gallery_loading
            GalleryEmptyReason.NOT_READY -> R.string.gallery_not_ready
            GalleryEmptyReason.EMPTY -> R.string.gallery_empty
            else -> R.string.gallery_no_match
        })
        galleryPage.emptyDetail.setText(when (reason) {
            GalleryEmptyReason.DISCONNECTED -> R.string.gallery_empty_detail
            GalleryEmptyReason.LOADING -> R.string.gallery_loading_detail
            GalleryEmptyReason.NOT_READY -> R.string.gallery_not_ready_detail
            GalleryEmptyReason.EMPTY -> R.string.gallery_empty_body
            else -> R.string.gallery_no_match_body
        })
        galleryPage.emptyAction.visible(reason != GalleryEmptyReason.NONE && reason != GalleryEmptyReason.LOADING)
        galleryPage.emptyAction.isEnabled = !ui.busy
        galleryPage.emptyAction.setText(when (reason) {
            GalleryEmptyReason.DISCONNECTED -> R.string.gallery_connect
            GalleryEmptyReason.NO_MATCH -> R.string.gallery_clear
            else -> R.string.refresh
        })
    }

    private fun showConnection() {
        if (connectionSheet?.isShowing == true) return
        val b = SheetConnectionBinding.inflate(layoutInflater)
        val dialog = BottomSheetDialog(this)
        sheet = b; connectionSheet = dialog
        dialog.setContentView(b.root)
        @Suppress("DEPRECATION")
        dialog.window?.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
        dialog.setOnShowListener {
            dialog.behavior.state = BottomSheetBehavior.STATE_EXPANDED
            dialog.behavior.skipCollapsed = true
            // Keep the sheet scrollable above both the keyboard and navigation gestures.
            ViewCompat.setOnApplyWindowInsetsListener(b.sheetContent) { view, insets ->
                val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
                val ime = insets.getInsets(WindowInsetsCompat.Type.ime())
                val pad = (24 * resources.displayMetrics.density).toInt()
                view.setPadding(pad + bars.left, pad, pad + bars.right, pad + maxOf(bars.bottom, ime.bottom))
                insets
            }
            ViewCompat.requestApplyInsets(b.sheetContent)
        }
        dialog.setOnDismissListener {
            sheet = null; connectionSheet = null
            if (ui.phase == ConnectionPhase.SCANNING) { stopScan(); ui = ui.copy(notice = getString(R.string.scan_cancelled)); render() }
            if (pending != null) cancelConnection()
        }
        b.sheetClose.setOnClickListener { dialog.dismiss() }
        b.scan.setOnClickListener {
            when {
                ui.phase == ConnectionPhase.SCANNING -> { stopScan(); ui = ui.copy(notice = getString(R.string.scan_cancelled)); render() }
                pending != null -> cancelConnection()
                else -> scanWithPermission()
            }
        }
        b.advancedToggle.setOnClickListener {
            val expanded = b.advancedSection.visibility != View.VISIBLE
            b.advancedSection.visible(expanded)
            b.advancedToggle.setIconResource(if (expanded) R.drawable.ic_close else R.drawable.ic_expand)
        }
        b.importKey.setOnClickListener { dialog.dismiss(); importKey() }
        b.pinInput.doAfterTextChanged { b.pinBox.error = null }
        renderSheet()
        dialog.show()
    }

    private fun renderSheet() {
        val b = sheet ?: return
        b.credentialHint.setText(if (hasCredential) R.string.pin_saved else R.string.pin_first)
        b.connectionIntro.setText(if (ui.connected) R.string.connection_active else R.string.connection_intro)
        b.pinBox.isEnabled = ui.canEditCredentials
        b.importKey.isEnabled = ui.canEditCredentials
        b.scan.isEnabled = !ui.connected && (!ui.busy || pending != null)
        b.scan.setText(when {
            ui.phase == ConnectionPhase.SCANNING -> R.string.cancel_scan
            pending != null -> R.string.cancel_connection
            else -> R.string.scan
        })
        b.scanProgress.visible(ui.phase == ConnectionPhase.SCANNING || pending != null)
        b.sheetMessage.visible(ui.error != null || ui.notice != null || pending != null)
        b.sheetMessage.text = ui.error ?: ui.notice ?: ui.progress
        b.sheetMessage.setTextColor(MaterialColors.getColor(b.root,
            if (ui.error != null) com.google.android.material.R.attr.colorError else com.google.android.material.R.attr.colorOnSurfaceVariant))
        b.devicesHint.setText(when {
            devices.isNotEmpty() -> R.string.devices_found
            ui.phase == ConnectionPhase.SCANNING -> R.string.devices_scanning
            else -> R.string.devices_hint
        })
        // Scan lists are small. Rebuild only when their addresses change, not on every progress update.
        val signature = devices.keys.toList()
        if (b.devices.tag != signature) {
            b.devices.tag = signature
            b.devices.removeAllViews()
            devices.values.forEach { device ->
                val button = MaterialButton(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
                    text = getString(R.string.device_list_label, device.name ?: "HueLeaf", device.address)
                    isAllCaps = false
                    setIconResource(R.drawable.ic_bluetooth)
                    setOnClickListener { if (!ui.busy && !ui.connected) { stopScan(); connectDevice(device) } }
                }
                b.devices.addView(button)
            }
        }
        for (i in 0 until b.devices.childCount) b.devices.getChildAt(i).isEnabled = !ui.busy && !ui.connected
    }

    private fun importKey() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            type = "application/json"; addCategory(Intent.CATEGORY_OPENABLE)
            putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/json", "text/plain", "application/octet-stream"))
        }
        @Suppress("DEPRECATION") startActivityForResult(intent, 1)
    }

    @Deprecated("Activity result") override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == 1 && resultCode == RESULT_OK) {
            try {
                val uri = data?.data ?: return
                val bytes = contentResolver.openInputStream(uri)?.use { it.readNBytesCompat(4097) } ?: error("无法读取文件")
                try {
                    require(bytes.size <= 4096) { "密钥文件过大" }
                    keys.import(bytes.toString(Charsets.UTF_8))
                } finally { bytes.fill(0) }
                hasCredential = true
                ui = ui.copy(error = null, notice = getString(R.string.import_success))
            } catch (e: Exception) { ui = ui.copy(error = getString(R.string.import_failure, e.message), notice = null) }
            render()
            showConnection()
        }
    }

    private fun java.io.InputStream.readNBytesCompat(limit: Int): ByteArray {
        val out = java.io.ByteArrayOutputStream(); val buf = ByteArray(512)
        while (out.size() < limit) {
            val n = read(buf, 0, minOf(buf.size, limit - out.size()))
            if (n < 0) break
            out.write(buf, 0, n)
        }
        return out.toByteArray()
    }

    private fun permissions(): Array<String> = if (Build.VERSION.SDK_INT >= 31)
        arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)

    private fun scanWithPermission() {
        val b = sheet ?: return
        try {
            val pin = b.pinInput.text?.toString().orEmpty()
            if (pin.isNotEmpty()) {
                if (!pin.matches(Regex("[0-9]{4}"))) { b.pinBox.error = getString(R.string.pin_invalid); return }
                keys.setPin(pin); b.pinInput.text?.clear()
            }
            hasCredential = credentialAvailable()
            if (!hasCredential) { b.pinBox.error = getString(R.string.pin_required); return }
        } catch (e: Exception) { b.pinBox.error = e.message; return }
        hideKeyboard()
        connectionSheet?.window?.let { WindowCompat.getInsetsController(it, b.root).hide(WindowInsetsCompat.Type.ime()) }
        if (permissions().any { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }) {
            requestPermissions(permissions(), 2); return
        }
        startScan()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, results: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, results)
        if (requestCode == 2 && connectionSheet?.isShowing == true) {
            if (results.isNotEmpty() && results.all { it == PackageManager.PERMISSION_GRANTED }) startScan()
            else { ui = ui.copy(error = getString(R.string.permission_required), notice = null); render() }
        }
    }

    private fun startScan() {
        try {
            check(credentialAvailable()) { getString(R.string.pin_required) }
            check(adapter?.isEnabled == true) { getString(R.string.bluetooth_off) }
            devices.clear()
            val generation = ++scanGeneration
            ui = RemoteUiState(phase = ConnectionPhase.SCANNING, progress = getString(R.string.devices_scanning))
            val callback = object : ScanCallback() {
                override fun onScanResult(callbackType: Int, result: ScanResult) {
                    main.post {
                        if (ui.phase != ConnectionPhase.SCANNING || generation != scanGeneration || devices.containsKey(result.device.address)) return@post
                        devices[result.device.address] = result.device
                        if (result.device.address == preferences.getString("device", null)) {
                            stopScan(); connectDevice(result.device)
                        } else renderSheet()
                    }
                }
                override fun onScanFailed(errorCode: Int) {
                    main.post {
                        if (generation != scanGeneration) return@post
                        stopScan(); ui = ui.copy(error = getString(R.string.scan_error, errorCode)); render()
                    }
                }
            }
            scanCallback = callback
            val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(RemoteClient.SERVICE)).build()
            val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
            checkNotNull(adapter?.bluetoothLeScanner).startScan(listOf(filter), settings, callback)
            render()
            main.postDelayed({
                if (ui.phase == ConnectionPhase.SCANNING && generation == scanGeneration) {
                    stopScan()
                    ui = ui.copy(error = if (devices.isEmpty()) getString(R.string.scan_timeout) else null)
                    render()
                }
            }, 20000)
        } catch (e: Exception) {
            stopScan(); ui = RemoteUiState(error = e.message ?: getString(R.string.operation_failed)); render()
        }
    }

    private fun stopScan() {
        scanCallback?.let { try { adapter?.bluetoothLeScanner?.stopScan(it) } catch (_: Exception) {} }
        scanCallback = null; scanGeneration++
        if (ui.phase == ConnectionPhase.SCANNING) ui = ui.copy(phase = ConnectionPhase.DISCONNECTED, progress = "")
    }

    private fun cancelConnection() {
        operationGeneration++
        val c = pending; pending = null; c?.close()
        deferredCompletion = null
        ui = RemoteUiState(notice = getString(R.string.connection_cancelled))
        render()
    }

    private fun connectDevice(device: BluetoothDevice) {
        if (ui.busy || ui.connected) return
        lateinit var c: RemoteClient
        c = RemoteClient(applicationContext, device, onProgress = { progress ->
            main.post { if (pending === c) { ui = ui.copy(progress = progress); render() } }
        }) {
            main.post {
                if (client === c) {
                    operationGeneration++; client = null; c.close()
                    ui = RemoteUiState(error = getString(R.string.disconnected)); render()
                }
            }
        }
        pending = c
        ui = RemoteUiState(phase = ConnectionPhase.CONNECTING, deviceName = device.name ?: "HueLeaf", progress = getString(R.string.connection_detail))
        runTask(UiTask.CONNECTING, discard = { _: Pair<JSONObject, Pair<Int, List<Protocol.Item>>?> -> c.close() }, action = {
            try {
                check(foreground) { "应用已进入后台" }
                val key = keys.load() ?: error(getString(R.string.pin_required))
                try { c.connect(key) } finally { key.fill(0) }
                val until = System.currentTimeMillis() + 30000
                var status = c.status()
                while (!status.optBoolean("ready") && System.currentTimeMillis() < until) {
                    check(pending === c) { getString(R.string.connection_cancelled) }
                    Thread.sleep(500); status = c.status()
                }
                val inventory = if (status.optBoolean("ready")) c.inventory() else null
                status to inventory
            } catch (e: Exception) { c.close(); throw e }
        }) { (status, inventory) ->
            pending = null; client = c
            preferences.edit { putString("device", device.address) }
            ui = ui.copy(phase = ConnectionPhase.CONNECTED)
            updateStatus(status, inventory)
            connectionSheet?.dismiss()
        }
    }

    private fun updateStatus(status: JSONObject, inventory: Pair<Int, List<Protocol.Item>>?) {
        val ready = status.optBoolean("ready")
        ui = ui.copy(
            ready = ready,
            battery = if (status.has("battery")) status.optInt("battery", -1).takeIf { it in 0..100 } else null,
            count = status.optInt("n").coerceAtLeast(0), currentIndex = status.optInt("cur", -1),
            currentName = status.optString("name"), deviceState = status.optString("state"),
            items = inventory?.second ?: emptyList(), inventoryLoaded = inventory != null,
            inventoryFlags = inventory?.first ?: 0, progress = "",
            notice = when {
                !ready -> getString(R.string.gallery_not_ready_detail)
                client?.labelsSupported == false -> getString(R.string.labels_upgrade_notice)
                else -> null
            }
        )
    }

    private fun refresh() {
        val c = client ?: return
        if (!ui.canRefresh) return
        ui = ui.copy(progress = getString(R.string.refresh_detail))
        runTask(UiTask.READING, action = { val status = c.status(); status to if (status.optBoolean("ready")) c.inventory() else null }) { (status, inventory) ->
            updateStatus(status, inventory)
        }
    }

    private fun show(op: Int, payload: ByteArray) {
        val c = client ?: return
        if (!ui.canDisplay) return
        hideKeyboard()
        ui = ui.copy(progress = getString(R.string.displaying_detail))
        runTask(UiTask.DISPLAYING, action = {
            c.display(op, payload)
            val status = c.status()
            status to if (status.optBoolean("ready")) c.inventory() else null
        }) { (status, inventory) ->
            updateStatus(status, inventory)
            message(getString(R.string.display_complete))
        }
    }

    private fun endSession() {
        val c = client ?: return
        if (!ui.canRefresh) return
        client = null
        ui = ui.copy(progress = getString(R.string.end_detail))
        runTask(UiTask.ENDING, action = { try { c.end() } finally { c.close() } }) {
            ui = RemoteUiState(notice = getString(R.string.session_ended))
        }
    }

    private fun <T> runTask(task: UiTask, discard: (T) -> Unit = {}, action: () -> T, success: (T) -> Unit) {
        val generation = ++operationGeneration
        ui = ui.copy(task = task, error = null, notice = null)
        render()
        worker.execute {
            try {
                val result = action()
                main.post {
                    if (generation != operationGeneration || isFinishing || isDestroyed) { discard(result); return@post }
                    val deliver = {
                        if (generation == operationGeneration) { ui = ui.copy(task = UiTask.NONE); success(result); render() }
                        else discard(result)
                    }
                    if (foreground) deliver() else if (pending != null) deferredCompletion = deliver else discard(result)
                }
            } catch (e: Exception) {
                main.post {
                    if (generation != operationGeneration || isFinishing || isDestroyed) return@post
                    val deliver = {
                        if (generation == operationGeneration) {
                            pending?.close(); pending = null
                            if (e !is RemoteClient.RemoteError || e.code == -10) {
                                client?.close(); client = null
                                ui = RemoteUiState(error = e.message ?: getString(R.string.operation_failed))
                            } else {
                                ui = ui.copy(task = UiTask.NONE, error = e.message, notice = null)
                                if (client == null) ui = RemoteUiState(error = e.message)
                            }
                            render()
                        }
                    }
                    if (foreground) deliver() else deferredCompletion = deliver
                }
            }
        }
    }

    private fun message(value: String) { Snackbar.make(binding.root, value, Snackbar.LENGTH_LONG).show() }

    override fun onStart() {
        super.onStart(); foreground = true
        val completion = deferredCompletion; deferredCompletion = null; completion?.invoke()
    }

    override fun onStop() {
        foreground = false
        val hadScan = ui.phase == ConnectionPhase.SCANNING
        stopScan()
        // A system pairing window can stop the Activity. Preserve the bounded attempt.
        if (pending == null) {
            val hadSession = client != null || ui.busy || hadScan
            operationGeneration++
            client?.close(); client = null
            if (hadSession) ui = RemoteUiState(notice = getString(R.string.background_disconnect))
            render()
        }
        super.onStop()
    }

    override fun onDestroy() {
        operationGeneration++
        pending?.close(); pending = null; client?.close(); client = null
        connectionSheet?.setOnDismissListener(null); connectionSheet?.dismiss()
        main.removeCallbacksAndMessages(null)
        tabMediator?.detach()
        worker.shutdownNow()
        super.onDestroy()
    }
}
