package io.github.abooodbah.leandoc

import android.app.Activity
import android.os.Bundle
import android.view.Gravity
import android.widget.TextView

class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(
            TextView(this).apply {
                text = getString(R.string.app_name)
                gravity = Gravity.CENTER
                setTextColor(getColor(R.color.ink))
            },
        )
    }
}
