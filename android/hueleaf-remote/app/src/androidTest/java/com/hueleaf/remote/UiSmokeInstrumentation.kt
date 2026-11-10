package com.hueleaf.remote

import android.app.Activity
import android.app.Instrumentation
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.app.KeyguardManager
import android.content.Context
import androidx.appcompat.app.AppCompatDelegate
import android.os.Bundle
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.MotionEvent
import androidx.viewpager2.widget.ViewPager2
import android.widget.TextView
import com.google.android.material.button.MaterialButton
import com.google.android.material.tabs.TabLayout
import com.google.android.material.textfield.TextInputEditText
import com.google.android.material.textfield.TextInputLayout
import androidx.recyclerview.widget.RecyclerView
import java.io.File

/** Real Android rendering and interaction checks with presentation fixtures; never opens BLE. */
class UiSmokeInstrumentation : Instrumentation() {
    private var prefix = "review"
    private lateinit var activity: MainActivity
    private var checks = 0
    private var night = "system"
    private var viewRendering = false

    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments)
        prefix = arguments?.getString("prefix") ?: prefix
        night = arguments?.getString("night") ?: night
        start()
    }

    override fun onStart() {
        try {
            viewRendering = (targetContext.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager).isKeyguardLocked
            onMain {
                AppCompatDelegate.setDefaultNightMode(when (night) {
                    "yes" -> AppCompatDelegate.MODE_NIGHT_YES
                    "no" -> AppCompatDelegate.MODE_NIGHT_NO
                    else -> AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM
                })
            }
            activity = startActivitySync(Intent(targetContext, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
            waitForIdleSync()
            capture("remote-disconnected")
            swipe(toGallery = true)
            onMain {
                expect(activity.findViewById<ViewPager2>(R.id.pager).currentItem == 1, "Swipe left opens gallery")
                expect(activity.findViewById<TabLayout>(R.id.tabs).selectedTabPosition == 1, "Swipe synchronizes tab")
            }
            swipe(toGallery = false)
            onMain {
                expect(activity.findViewById<ViewPager2>(R.id.pager).currentItem == 0, "Swipe right opens remote")
                expect(activity.findViewById<TabLayout>(R.id.tabs).selectedTabPosition == 0, "Reverse swipe synchronizes tab")
            }
            onMain {
                expect(activity.findViewById<View>(R.id.connect).visibility == View.VISIBLE, "Disconnected CTA")
                activity.findViewById<TabLayout>(R.id.tabs).getTabAt(1)!!.select()
                expect(activity.findViewById<TextView>(R.id.empty_title).text == activity.getString(R.string.gallery_empty_title), "Disconnected gallery")
            }
            capture("gallery-disconnected")
            val sample = RemoteUiState(
                phase = ConnectionPhase.CONNECTED, deviceName = "HueLeaf", ready = true, battery = 86,
                count = 5, currentIndex = 0, currentName = "img-garden.bin", deviceState = "idle", inventoryLoaded = true,
                items = listOf(Protocol.Item("img-garden.bin", true, "午后花园"), Protocol.Item("Mountain.BIN", false),
                    Protocol.Item("海边的日落.bin", true), Protocol.Item("窗边的绿植与一本尚未读完的书.bin", false), Protocol.Item("周末散步.bin", true))
            )
            fixture(sample)
            onMain {
                expect(activity.findViewById<View>(R.id.previous).isEnabled, "Connected navigation enabled")
                expect(activity.findViewById<TextView>(R.id.current_name).text == "午后花园", "Current image uses label")
                var shownName = ""
                val adapter = GalleryAdapter { shownName = it }
                adapter.submitList(listOf(GalleryAdapter.Row(sample.items[0], current = true, enabled = true)))
                val parent = activity.findViewById<RecyclerView>(R.id.gallery_list)
                val holder = adapter.onCreateViewHolder(parent, 0)
                adapter.onBindViewHolder(holder, 0)
                expect(holder.binding.itemName.text == "午后花园", "Gallery row uses label")
                expect(holder.binding.root.contentDescription.contains("午后花园"), "Accessibility uses label")
                holder.binding.root.performClick()
                expect(shownName == "img-garden.bin", "Label click sends original filename")
                activity.findViewById<TabLayout>(R.id.tabs).getTabAt(0)!!.select()
            }
            capture("remote-connected-fixture")
            onMain { activity.findViewById<TabLayout>(R.id.tabs).getTabAt(1)!!.select() }
            capture("gallery-connected-fixture")
            onMain { activity.findViewById<TextInputEditText>(R.id.search).setText("mountain") }
            waitForIdleSync(); SystemClock.sleep(400)
            swipe(toGallery = false)
            swipe(toGallery = true)
            onMain {
                expect(activity.findViewById<TextInputEditText>(R.id.search).text.toString() == "mountain", "Swipe preserves search")
                expect(activity.findViewById<RecyclerView>(R.id.gallery_list).adapter!!.itemCount == 1, "Search case-insensitive")
                activity.findViewById<MaterialButton>(R.id.filter_playlist).performClick()
                expect(activity.findViewById<TextView>(R.id.empty_title).text == activity.getString(R.string.gallery_no_match), "Playlist intersects search")
                activity.findViewById<MaterialButton>(R.id.empty_action).performClick()
            }
            waitForIdleSync(); SystemClock.sleep(200)
            onMain {
                expect(activity.findViewById<TextInputEditText>(R.id.search).text.isNullOrEmpty(), "Clear search")
                expect(activity.findViewById<RecyclerView>(R.id.gallery_list).adapter!!.itemCount == 5, "Clear filter restores gallery")
            }
            fixture(sample.copy(task = UiTask.DISPLAYING, progress = "正在发送并等待墨水屏刷新完成，请勿重复操作。"))
            onMain {
                expect(!activity.findViewById<View>(R.id.previous).isEnabled, "Wait for physical refresh")
                expect(!activity.findViewById<View>(R.id.refresh).isEnabled, "No duplicate refresh")
                expect(activity.findViewById<TextView>(R.id.current_name).text == "午后花园", "Current image not changed early")
                activity.findViewById<TabLayout>(R.id.tabs).getTabAt(0)!!.select()
            }
            capture("refreshing-fixture")
            fixture(sample.copy(ready = false, inventoryLoaded = false, items = emptyList()))
            onMain {
                activity.findViewById<TabLayout>(R.id.tabs).getTabAt(1)!!.select()
                expect(activity.findViewById<TextView>(R.id.empty_title).text == activity.getString(R.string.gallery_not_ready), "Not-ready state")
                expect(activity.findViewById<View>(R.id.empty_action).isEnabled, "Can refresh initialization")
            }
            fixture(RemoteUiState(error = "相框已断开。请双击唤醒后重新连接。"))
            onMain {
                expect(activity.findViewById<View>(R.id.retry).visibility == View.VISIBLE, "Recoverable error")
                activity.findViewById<TabLayout>(R.id.tabs).getTabAt(0)!!.select()
            }
            capture("disconnected-error-fixture")
            fixture(RemoteUiState())
            onMain { activity.findViewById<MaterialButton>(R.id.connect).performClick() }
            capture("connection")
            onMain {
                val window = activity.javaClass.getDeclaredField("connectionSheet").apply { isAccessible = true }
                    .get(activity) as com.google.android.material.bottomsheet.BottomSheetDialog
                window.findViewById<TextInputEditText>(R.id.pin_input)!!.setText("12")
                window.findViewById<MaterialButton>(R.id.scan)!!.performClick()
                expect(!window.findViewById<TextInputLayout>(R.id.pin_box)!!.error.isNullOrEmpty(), "Inline invalid PIN")
                window.findViewById<TextInputEditText>(R.id.pin_input)!!.text!!.clear()
                window.findViewById<MaterialButton>(R.id.advanced_toggle)!!.performClick()
                expect(window.findViewById<View>(R.id.advanced_section)!!.visibility == View.VISIBLE, "Advanced credentials disclosure")
            }
            capture("connection-advanced")
            onMain {
                val window = activity.javaClass.getDeclaredField("connectionSheet").apply { isAccessible = true }
                    .get(activity) as com.google.android.material.bottomsheet.BottomSheetDialog
                window.dismiss()
            }
            val result = Bundle().apply { putString("stream", "Passed $checks UI checks (locked-view-rendering=$viewRendering); screenshots: ${targetContext.getExternalFilesDir("ui-review")}") }
            onMain { activity.finish() }
            finish(Activity.RESULT_OK, result)
        } catch (e: Throwable) {
            finish(Activity.RESULT_CANCELED, Bundle().apply { putString("stream", "UI failure: ${e.stackTraceToString()}") })
        }
    }

    private fun fixture(state: RemoteUiState) {
        onMain {
            activity.javaClass.getDeclaredField("ui").apply { isAccessible = true }.set(activity, state)
            activity.javaClass.getDeclaredMethod("render").apply { isAccessible = true }.invoke(activity)
        }
        waitForIdleSync(); SystemClock.sleep(200)
    }

    private fun expect(value: Boolean, message: String) { check(value) { message }; checks++ }

    private fun onMain(block: () -> Unit) {
        var outcome: Result<Unit>? = null
        runOnMainSync { outcome = runCatching(block) }
        outcome!!.getOrThrow()
    }

    private fun swipe(toGallery: Boolean) {
        val location = IntArray(2)
        var width = 0
        var height = 0
        onMain {
            val pager = activity.findViewById<ViewPager2>(R.id.pager)
            pager.getLocationOnScreen(location)
            width = pager.width; height = pager.height
        }
        val from = location[0] + width * if (toGallery) 0.82f else 0.18f
        val to = location[0] + width * if (toGallery) 0.18f else 0.82f
        val y = location[1] + height * 0.45f
        val down = SystemClock.uptimeMillis()
        fun event(action: Int, x: Float) {
            val e = MotionEvent.obtain(down, SystemClock.uptimeMillis(), action, x, y, 0)
            if (viewRendering) onMain { activity.window.decorView.dispatchTouchEvent(e) }
            else sendPointerSync(e)
            e.recycle()
        }
        event(MotionEvent.ACTION_DOWN, from)
        for (step in 1..16) {
            SystemClock.sleep(16)
            event(MotionEvent.ACTION_MOVE, from + (to - from) * step / 16)
        }
        event(MotionEvent.ACTION_UP, to)
        waitForIdleSync(); SystemClock.sleep(500)
    }

    private fun capture(name: String) {
        waitForIdleSync(); SystemClock.sleep(500)
        val output = File(targetContext.getExternalFilesDir("ui-review"), "$prefix-$name.png")
        output.parentFile!!.mkdirs()
        val bitmap = if (viewRendering) {
            var rendered: Bitmap? = null
            onMain {
                val dialog = activity.javaClass.getDeclaredField("connectionSheet").apply { isAccessible = true }
                    .get(activity) as? com.google.android.material.bottomsheet.BottomSheetDialog
                val view = dialog?.takeIf { it.isShowing }?.window?.decorView ?: activity.window.decorView
                fun forceLayout(node: View) {
                    node.forceLayout()
                    if (node is ViewGroup) for (i in 0 until node.childCount) forceLayout(node.getChildAt(i))
                }
                val width = view.width
                val height = view.height
                forceLayout(view)
                view.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY))
                view.layout(0, 0, width, height)
                if (dialog?.isShowing != true) {
                    val pager = activity.findViewById<ViewPager2>(R.id.pager)
                    (pager.getChildAt(0) as RecyclerView).scrollToPosition(pager.currentItem)
                    forceLayout(view)
                    view.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY))
                    view.layout(0, 0, width, height)
                }
                rendered = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888).also { view.draw(Canvas(it)) }
            }
            checkNotNull(rendered)
        } else checkNotNull(uiAutomation.takeScreenshot())
        output.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        bitmap.recycle()
    }
}
