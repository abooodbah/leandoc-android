package io.github.abooodbah.leandoc.baseline;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;

/** Draws one empty view: the least an app can do before its first frame. */
public final class EmptyActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(new View(this));
    }
}
