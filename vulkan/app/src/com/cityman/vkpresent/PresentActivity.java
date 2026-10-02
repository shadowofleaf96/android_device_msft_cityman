package com.cityman.vkpresent;

import android.app.Activity;
import android.graphics.PixelFormat;
import android.os.Bundle;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.Toast;

/**
 * Own SurfaceView Surface (not SystemUI ViewRootImpl BLAST).
 * Native side uses ANativeWindow_fromSurface + Vulkan 1.0 WSI.
 */
public class PresentActivity extends Activity implements SurfaceHolder.Callback {
    static {
        System.loadLibrary("cityman-vk-present");
    }

    private native void nativeOnSurfaceCreated(Surface surface);
    private native void nativeOnSurfaceDestroyed();
    private native String nativeAwaitInit();

    private boolean mNativeSurface;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        SurfaceView sv = new SurfaceView(this);
        sv.getHolder().setFormat(PixelFormat.RGBA_8888);
        sv.getHolder().addCallback(this);
        setContentView(sv);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        nativeOnSurfaceCreated(holder.getSurface());
        mNativeSurface = true;
        new Thread(new Runnable() {
            @Override
            public void run() {
                final String line = nativeAwaitInit();
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        Toast.makeText(PresentActivity.this, line, Toast.LENGTH_LONG).show();
                        if (line.indexOf("enumerate==0") >= 0 || line.startsWith("FAIL")) {
                            finish();
                        }
                    }
                });
            }
        }, "vk-await").start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        if (mNativeSurface) {
            nativeOnSurfaceDestroyed();
            mNativeSurface = false;
        }
    }

    @Override
    protected void onDestroy() {
        if (mNativeSurface) {
            nativeOnSurfaceDestroyed();
            mNativeSurface = false;
        }
        super.onDestroy();
    }
}
