package io.github.abooodbah.leandoc

import android.app.Activity
import android.app.ActivityManager
import android.os.Build
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
        var nextRecovery = 0L
        while (app !in focus) {
            val now = SystemClock.uptimeMillis()
            if (now > end) {
                fail("no window of the app has input focus, so Android would refuse the test's input: " + windowState())
            }
            // Bringing the task forward starts a window transition, so it is
            // repeated only every few seconds; focus is checked in between.
            if (now >= nextRecovery) {
                shell("am broadcast -a android.intent.action.CLOSE_SYSTEM_DIALOGS")
                bringAppToFront()
                nextRecovery = now + 3_000
            }
            SystemClock.sleep(250)
            focus = currentFocus()
        }
    }

    /** The focus lines of dumpsys window and the top resumed activity, for failure messages. */
    private fun windowState(): String {
        val window = shell("dumpsys window").lineSequence().map { it.trim() }
            .filter { it.startsWith("mCurrentFocus=") || it.startsWith("mFocusedApp=") }.distinct().joinToString("; ")
        val activity = shell("dumpsys activity activities").lineSequence().map { it.trim() }
            .filter { it.startsWith("topResumedActivity=") || it.startsWith("ResumedActivity:") }.distinct().joinToString("; ")
        return "$window; $activity"
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


    /**
     * Moves the app's own task back in front without a new intent, so the
     * activity under test keeps its document. An app in the background may
     * not do that on Android 14, so from Android 10 on the call is made with
     * the shell's permissions, which allow it.
     */
    private fun bringAppToFront() {
        val manager = instrumentation.targetContext.getSystemService(ActivityManager::class.java)
        val automation = instrumentation.uiAutomation
        val adopted = Build.VERSION.SDK_INT >= 29
        if (adopted) automation.adoptShellPermissionIdentity()
        try {
            for (task in manager.appTasks) {
                @Suppress("DEPRECATION")
                val id = task.taskInfo.id
                manager.moveTaskToFront(id, 0)
            }
        } catch (error: SecurityException) {
            // Below Android 10 the app has no shell permissions to borrow;
            // the focus check then reports which window has focus.
        } finally {
            if (adopted) automation.dropShellPermissionIdentity()
        }
    }

    private fun currentFocus(): String =
        shell("dumpsys window").lineSequence().map { it.trim() }
            .firstOrNull { it.startsWith("mCurrentFocus=") }.orEmpty()

    private fun shell(command: String): String {
        val output = instrumentation.uiAutomation.executeShellCommand(command)
        return ParcelFileDescriptor.AutoCloseInputStream(output).bufferedReader().use { it.readText() }
    }
}
