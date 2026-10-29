package io.github.abooodbah.leandoc

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.ComponentName
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.ApplicationInfo
import android.content.res.Configuration
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.util.Log
import android.view.Gravity
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.webkit.ConsoleMessage
import android.webkit.RenderProcessGoneDetail
import android.webkit.WebChromeClient
import android.webkit.WebMessage
import android.webkit.WebMessagePort
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import android.widget.TextView
import java.io.ByteArrayInputStream
import java.io.IOException
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import kotlin.math.roundToInt

/**
 * Shows one Word document in the desktop reader page. Files arrive through
 * VIEW and SEND intents or the page's Open button; the app reads them with the
 * grant that came with them and never writes to them. The desktop's C++ core
 * checks and sanitizes every document before the page sees it.
 */
class MainActivity : Activity() {

    private sealed interface Shown {
        data object Empty : Shown

        /** [packageBytes] is served once to the page at [ReaderAssets.packageUrl]. */
        class Document(
            val name: String,
            val generation: Long,
            val report: String,
            @Volatile var packageBytes: ByteArray?,
        ) : Shown

        class Fallback(val name: String, val generation: Long, val report: String) : Shown
    }

    private lateinit var root: FrameLayout
    private lateinit var prefs: SharedPreferences
    private val main = Handler(Looper.getMainLooper())
    private val worker: ExecutorService = Executors.newSingleThreadExecutor()

    private var webView: WebView? = null
    private var port: WebMessagePort? = null
    private var portPosted = false
    private var readerReady = false
    private var source: Uri? = null

    @Volatile private var shown: Shown = Shown.Empty
    private var generation = 0L
    private var loads = 0
    private var loading = false
    private var awaitingFirstRender = true
    private var theme = "system"
    private var readerView = "page"

    /** Every request the WebView made that was refused. Read by the instrumented tests. */
    internal val blockedRequests = CopyOnWriteArrayList<String>()

    internal val webViewForTest: WebView? get() = webView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = getSharedPreferences(PREFERENCES, MODE_PRIVATE)
        theme = prefs.getString(KEY_THEME, "system") ?: "system"
        readerView = prefs.getString(KEY_VIEW, "page") ?: "page"

        root = FrameLayout(this)
        setContentView(root)
        layOutEdgeToEdge()
        applyBarColors()

        // Read and check the file while the WebView starts; creating the
        // WebView is the slowest part of a cold start.
        val restored = savedInstanceState?.getString(STATE_URI)?.let(Uri::parse)
        if (restored != null) open(restored) else openFrom(intent)

        val provider = WebView.getCurrentWebViewPackage()?.versionName
        if (!WebViewSupport.isSupported(provider)) {
            showUnsupported(WebViewSupport.message(provider))
            return
        }
        try {
            createWebView()
        } catch (error: RuntimeException) {
            // Thrown when the WebView provider is missing or being updated.
            showUnsupported(WebViewSupport.message(null))
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        openFrom(intent)
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        source?.let { outState.putString(STATE_URI, it.toString()) }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        applyBarColors()
    }

    override fun onDestroy() {
        worker.shutdownNow()
        port?.close()
        webView?.destroy()
        super.onDestroy()
    }

    @Deprecated("Activity result API from the platform; AndroidX is not used.")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data
        if (requestCode == REQUEST_OPEN && resultCode == RESULT_OK && uri != null) {
            open(uri)
        }
    }

    // Opening documents

    private fun openFrom(intent: Intent?) {
        val uri = when (intent?.action) {
            Intent.ACTION_VIEW -> intent.data
            Intent.ACTION_SEND -> sharedStream(intent)
            else -> null
        }
        if (uri != null) {
            open(uri)
        } else {
            source = null
            shown = Shown.Empty
            if (readerReady) sendCurrentView()
        }
    }

    private fun sharedStream(intent: Intent): Uri? =
        if (Build.VERSION.SDK_INT >= 33) {
            intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra(Intent.EXTRA_STREAM)
        }

    private fun open(uri: Uri) {
        source = uri
        val load = ++loads
        val next = ++generation
        loading = true
        worker.execute {
            val result = load(uri, next)
            main.post {
                if (load != loads || isDestroyed) return@post
                loading = false
                shown = result
                if (readerReady) sendCurrentView()
            }
        }
    }

    private fun load(uri: Uri, generation: Long): Shown {
        val name = displayName(uri)
        val bytes = try {
            // Only content URIs carry a grant from the sending app. Paths and
            // other schemes would read with this app's own access instead.
            if (uri.scheme != "content") throw SecurityException("not a content URI")
            val size = queryLong(uri, OpenableColumns.SIZE) ?: -1
            val input = contentResolver.openInputStream(uri) ?: throw IOException("no stream")
            input.use { DocumentReader.readLimited(it, DocumentReader.MAX_BYTES, size) }
        } catch (error: DocumentReader.TooLargeException) {
            val size = queryLong(uri, OpenableColumns.SIZE) ?: 0
            return Shown.Fallback(name, generation, HostMessages.failureReport("too-large", DocumentReader.TOO_LARGE, size))
        } catch (error: OutOfMemoryError) {
            return Shown.Fallback(name, generation, HostMessages.failureReport("read-failed", OUT_OF_MEMORY, 0))
        } catch (error: Exception) {
            // FileNotFoundException, SecurityException and other provider failures.
            return Shown.Fallback(name, generation, HostMessages.failureReport("read-failed", CANNOT_READ, 0))
        }
        val opened = try {
            DocCore.open(bytes, DocumentReader.MAX_BYTES)
        } catch (error: OutOfMemoryError) {
            null
        } ?: return Shown.Fallback(name, generation, HostMessages.failureReport("read-failed", OUT_OF_MEMORY, bytes.size.toLong()))
        val report = String(opened.report, Charsets.UTF_8)
        val packageBytes = opened.packageBytes
        return if (packageBytes != null) {
            Shown.Document(name, generation, report, packageBytes)
        } else {
            Shown.Fallback(name, generation, report)
        }
    }

    private fun displayName(uri: Uri): String {
        val queried = try {
            contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
                val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (index >= 0 && cursor.moveToFirst() && !cursor.isNull(index)) cursor.getString(index) else null
            }
        } catch (error: Exception) {
            null
        }
        return queried?.takeIf { it.isNotBlank() }
            ?: uri.lastPathSegment?.substringAfterLast('/')?.takeIf { it.isNotBlank() }
            ?: "Untitled.docx"
    }

    private fun queryLong(uri: Uri, column: String): Long? = try {
        contentResolver.query(uri, arrayOf(column), null, null, null)?.use { cursor ->
            val index = cursor.getColumnIndex(column)
            if (index >= 0 && cursor.moveToFirst() && !cursor.isNull(index)) cursor.getLong(index) else null
        }
    } catch (error: Exception) {
        null
    }

    // The reader page

    private fun createWebView() {
        if (isDebuggable()) WebView.setWebContentsDebuggingEnabled(true)
        val view = WebView(this)
        view.setBackgroundColor(Color.TRANSPARENT)
        with(view.settings) {
            javaScriptEnabled = true
            allowFileAccess = false
            allowContentAccess = false
            domStorageEnabled = false
            setGeolocationEnabled(false)
            setSupportMultipleWindows(false)
            javaScriptCanOpenWindowsAutomatically = false
            mixedContentMode = WebSettings.MIXED_CONTENT_NEVER_ALLOW
            safeBrowsingEnabled = false
            mediaPlaybackRequiresUserGesture = true
            builtInZoomControls = true
            displayZoomControls = false
            // The system font size applies to the reader's own text.
            textZoom = (resources.configuration.fontScale * 100).roundToInt()
        }
        view.webViewClient = ReaderClient()
        if (isDebuggable()) view.webChromeClient = ConsoleLog
        root.addView(view, FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT))
        webView = view
        portPosted = false
        readerReady = false
        view.loadUrl(ReaderAssets.PAGE_URL)
    }

    private inner class ReaderClient : WebViewClient() {
        override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? {
            val url = request.url.toString()
            val route = if (request.method == "GET") ReaderAssets.route(url) else null
            return when (route) {
                is ReaderAssets.Route.Asset -> try {
                    val encoding = if (route.mimeType.startsWith("text/")) "utf-8" else null
                    WebResourceResponse(route.mimeType, encoding, assets.open(route.assetPath))
                } catch (error: IOException) {
                    refuse(url)
                }
                is ReaderAssets.Route.Package -> servePackage(route.generation) ?: refuse(url)
                null -> refuse(url)
            }
        }

        // Links are handled by the page, which posts them. Anything that
        // still tries to navigate is refused, except moving to an #anchor.
        override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
            val url = request.url.toString()
            return !(url.substringBefore('#') == ReaderAssets.PAGE_URL && url.contains('#'))
        }

        override fun onPageStarted(view: WebView, url: String, favicon: android.graphics.Bitmap?) {
            portPosted = false
        }

        override fun onPageFinished(view: WebView, url: String) {
            if (view === webView && !portPosted && url.startsWith(ReaderAssets.PAGE_URL)) {
                portPosted = true
                connect(view)
            }
        }

        override fun onRenderProcessGone(view: WebView, detail: RenderProcessGoneDetail): Boolean {
            // Without this the whole app would close. Start a new page and show
            // the document again, read fresh from its source.
            if (view === webView) {
                root.removeView(view)
                webView = null
                port = null
                readerReady = false
                view.destroy()
                createWebView()
            }
            return true
        }
    }

    private fun refuse(url: String): WebResourceResponse {
        blockedRequests += url
        return WebResourceResponse("text/plain", "utf-8", 403, "Blocked", emptyMap(), ByteArrayInputStream(ByteArray(0)))
    }

    /** Runs on a WebView thread. Only the package of the document on screen is served. */
    private fun servePackage(requested: Long): WebResourceResponse? {
        val current = shown as? Shown.Document ?: return null
        if (current.generation != requested) return null
        val bytes = current.packageBytes ?: return null
        return WebResourceResponse(
            ReaderAssets.PACKAGE_TYPE,
            null,
            200,
            "OK",
            mapOf("Cache-Control" to "no-store"),
            ByteArrayInputStream(bytes),
        )
    }

    private fun connect(view: WebView) {
        val (mine, theirs) = view.createWebMessageChannel()
        mine.setWebMessageCallback(
            object : WebMessagePort.WebMessageCallback() {
                override fun onMessage(from: WebMessagePort, message: WebMessage) {
                    if (from === port) message.data?.let(::handleReaderMessage)
                }
            },
            main,
        )
        port = mine
        view.postWebMessage(WebMessage("leandoc-port", arrayOf(theirs)), Uri.parse(ReaderAssets.ORIGIN))
    }

    private fun send(message: String) {
        if (readerReady) port?.postMessage(WebMessage(message))
    }

    private fun handleReaderMessage(message: String) {
        when (val command = ReaderCommand.parse(message) ?: return) {
            ReaderCommand.Ready -> {
                readerReady = true
                send(HostMessages.host(versionName()))
                send(HostMessages.theme(theme, readerView))
                sendCurrentView()
            }
            ReaderCommand.Open -> pickDocument()
            ReaderCommand.Reload -> source?.let(::open)
            ReaderCommand.OpenInEditor -> openInAnotherApp()
            is ReaderCommand.SetTheme -> {
                theme = command.theme
                prefs.edit().putString(KEY_THEME, theme).apply()
                send(HostMessages.theme(theme, readerView))
                applyBarColors()
            }
            is ReaderCommand.SetView -> {
                readerView = command.view
                prefs.edit().putString(KEY_VIEW, readerView).apply()
            }
            is ReaderCommand.Link -> followLink(command.href)
            is ReaderCommand.Rendered -> if (awaitingFirstRender && command.generation == generation) reportDrawn()
            is ReaderCommand.RenderFailed -> if (awaitingFirstRender) reportDrawn()
        }
    }

    private fun sendCurrentView() {
        when (val current = shown) {
            Shown.Empty -> if (!loading) {
                send(HostMessages.empty())
                if (awaitingFirstRender) reportDrawn()
            }
            is Shown.Document -> if (current.packageBytes == null) {
                // The page was replaced after the package was dropped.
                source?.let(::open)
            } else {
                send(HostMessages.document(current.generation, current.name, ReaderAssets.packageUrl(current.generation), current.report))
            }
            is Shown.Fallback -> {
                send(HostMessages.fallback(current.generation, current.name, current.report))
                if (awaitingFirstRender) reportDrawn()
            }
        }
    }

    private fun reportDrawn() {
        val view = webView ?: return
        awaitingFirstRender = false
        // Fires once the page's new content is ready to be drawn on screen.
        view.postVisualStateCallback(
            0,
            object : WebView.VisualStateCallback() {
                override fun onComplete(requestId: Long) {
                    try {
                        reportFullyDrawn()
                    } catch (error: SecurityException) {
                        // Some older builds required a permission for this call.
                    }
                }
            },
        )
    }

    private fun showUnsupported(message: String) {
        root.removeAllViews()
        val padding = (24 * resources.displayMetrics.density).roundToInt()
        root.addView(
            TextView(this).apply {
                text = message
                gravity = Gravity.CENTER
                textSize = 18f
                setTextColor(getColor(R.color.ink))
                setPadding(padding, padding, padding, padding)
            },
            FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT),
        )
        awaitingFirstRender = false
        root.post {
            try {
                reportFullyDrawn()
            } catch (error: SecurityException) {
                // As above.
            }
        }
    }

    // Commands from the page

    private fun pickDocument() {
        val pick = Intent(Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType("*/*")
            .putExtra(Intent.EXTRA_MIME_TYPES, PICKER_TYPES)
        try {
            @Suppress("DEPRECATION")
            startActivityForResult(pick, REQUEST_OPEN)
        } catch (error: ActivityNotFoundException) {
            send(HostMessages.status("This device has no file picker.", "error"))
        }
    }

    /** Offers the same file to every other app that opens it, never to LeanDoc itself. */
    private fun openInAnotherApp() {
        val uri = source ?: return
        val type = try {
            contentResolver.getType(uri)
        } catch (error: Exception) {
            null
        } ?: ReaderAssets.PACKAGE_TYPE
        val view = Intent(Intent.ACTION_VIEW)
            .setDataAndType(uri, type)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        val chooser = Intent.createChooser(view, null)
            .putExtra(Intent.EXTRA_EXCLUDE_COMPONENTS, arrayOf(ComponentName(this, MainActivity::class.java)))
        try {
            startActivity(chooser)
        } catch (error: RuntimeException) {
            // ActivityNotFoundException, or a SecurityException when this app
            // may not pass the grant on.
            send(HostMessages.status("No other app on this device can open this file.", "warning"))
        }
    }

    private fun followLink(href: String) {
        when (val action = LinkAction.forHref(href)) {
            is LinkAction.External -> try {
                startActivity(
                    Intent(Intent.ACTION_VIEW, Uri.parse(action.uri)).addCategory(Intent.CATEGORY_BROWSABLE),
                )
            } catch (error: ActivityNotFoundException) {
                send(HostMessages.status("No app on this device can open that link.", "warning"))
            }
            is LinkAction.Refuse -> send(HostMessages.status(action.message, "warning"))
            LinkAction.Ignore -> Unit
        }
    }

    // Window

    @Suppress("DEPRECATION")
    private fun layOutEdgeToEdge() {
        if (Build.VERSION.SDK_INT < 30) return
        // Android 15 and later always draw behind the bars; doing the same from
        // Android 11 keeps one code path for insets and the keyboard.
        window.setDecorFitsSystemWindows(false)
        root.setOnApplyWindowInsetsListener { view, insets ->
            val bars = insets.getInsets(
                WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout() or WindowInsets.Type.ime(),
            )
            view.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            WindowInsets.CONSUMED
        }
    }

    private fun isNight(): Boolean =
        resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK == Configuration.UI_MODE_NIGHT_YES

    @Suppress("DEPRECATION")
    private fun applyBarColors() {
        val dark = theme == "dark" || (theme == "system" && isNight())
        val surface = if (dark) SURFACE_DARK else SURFACE_LIGHT
        root.setBackgroundColor(surface)
        window.statusBarColor = surface
        window.navigationBarColor = surface
        if (Build.VERSION.SDK_INT >= 30) {
            val light = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or
                WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS
            window.insetsController?.setSystemBarsAppearance(if (dark) 0 else light, light)
        } else {
            val light = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR or View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR
            val flags = window.decorView.systemUiVisibility
            window.decorView.systemUiVisibility = if (dark) flags and light.inv() else flags or light
        }
    }

    private fun versionName(): String = try {
        packageManager.getPackageInfo(packageName, 0).versionName ?: ""
    } catch (error: Exception) {
        ""
    }

    private fun isDebuggable() = applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0

    private object ConsoleLog : WebChromeClient() {
        override fun onConsoleMessage(message: ConsoleMessage): Boolean {
            Log.d("LeanDocPage", "${message.messageLevel()} ${message.message()} (${message.lineNumber()})")
            return true
        }
    }

    private companion object {
        const val PREFERENCES = "reader"
        const val KEY_THEME = "theme"
        const val KEY_VIEW = "view"
        const val STATE_URI = "document"
        const val REQUEST_OPEN = 1

        // reader.css --surface in its light and dark themes.
        const val SURFACE_LIGHT = 0xFFFFFFFF.toInt()
        const val SURFACE_DARK = 0xFF1A1917.toInt()

        val PICKER_TYPES = arrayOf(
            "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
            "application/vnd.openxmlformats-officedocument.wordprocessingml.template",
            "application/vnd.ms-word.document.macroenabled.12",
            "application/vnd.ms-word.template.macroenabled.12",
            "application/msword",
            "application/octet-stream",
        )

        const val CANNOT_READ =
            "LeanDoc could not read this file. The app that shared it may have " +
                "withdrawn access. Open it again from that app."
        const val OUT_OF_MEMORY = "LeanDoc ran out of memory while reading this file."
    }
}
