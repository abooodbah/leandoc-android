package io.github.abooodbah.leandoc

import android.app.Activity
import android.app.ActivityManager
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.InputDevice
import android.view.MotionEvent
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.fail

/**
 * Android delivers injected touches and keys to the window with input focus,
 * refuses them when that window belongs to another app, and lets only the
 * focused app read the clipboard. On a slow emulator a system dialog, such as
 * "System UI isn't responding", can take focus in the middle of a test run,
 * and the launcher can come up over the app while the emulator settles.
 * Tests call [ensure] before they press keys or read the clipboard, and
 * send touches with [touch].
 */
object Focus {
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private var dialogsHidden = false

    /** Waits until one of this app's windows has input focus. */
    fun ensure(timeoutMs: Long = 15_000) {
        if (!dialogsHidden) {
            // Crash and not-responding dialogs from other apps stay hidden.
            shell("settings put global hide_error_dialogs 1")
            dialogsHidden = true
        }
        val app = instrumentation.targetContext.packageName
        val end = SystemClock.uptimeMillis() + timeoutMs
        var focus = currentFocus()
        while (app !in focus) {
            if (SystemClock.uptimeMillis() > end) {
                fail("another window has input focus, so Android would refuse the test's input: $focus")
            }
            shell("am broadcast -a android.intent.action.CLOSE_SYSTEM_DIALOGS")
            // Brings the app's own task back in front without a new intent,
            // so the activity under test keeps its document.
            instrumentation.targetContext.getSystemService(ActivityManager::class.java)
                .appTasks.forEach { it.moveToFront() }
            SystemClock.sleep(250)
            focus = currentFocus()
        }
    }


    /**
     * Delivers a touch, given in screen pixels, straight to [activity] on the
     * main thread. It runs through the activity's own touch handling as a
     * finger would, but does not depend on which window has input focus: on
     * the API 34 emulator the launcher sometimes keeps focus while the app is
     * resumed in front, and injected touches are then refused.
     */
    fun touch(activity: Activity, action: Int, downTime: Long, eventTime: Long, x: Float, y: Float) {
        instrumentation.runOnMainSync {
            val origin = IntArray(2).also { activity.window.decorView.getLocationOnScreen(it) }
            val event = MotionEvent.obtain(downTime, eventTime, action, x - origin[0], y - origin[1], 0)
            event.source = InputDevice.SOURCE_TOUCHSCREEN
            activity.dispatchTouchEvent(event)
            event.recycle()
        }
        if (action == MotionEvent.ACTION_UP) instrumentation.waitForIdleSync()
    }

    private fun currentFocus(): String =
        shell("dumpsys window").lineSequence().map { it.trim() }
            .firstOrNull { it.startsWith("mCurrentFocus=") }.orEmpty()

    private fun shell(command: String): String {
        val output = instrumentation.uiAutomation.executeShellCommand(command)
        return ParcelFileDescriptor.AutoCloseInputStream(output).bufferedReader().use { it.readText() }
    }
}
