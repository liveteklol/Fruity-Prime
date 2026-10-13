package fr.livetek.fruityprime;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.res.AssetManager;
import android.content.res.Configuration;
import android.hardware.display.DisplayManager;
import android.hardware.input.InputManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.system.ErrnoException;
import android.system.Os;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.TextView;
import android.widget.Toast;

import android.app.Activity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

public final class MainActivity extends Activity
        implements DisplayManager.DisplayListener, InputManager.InputDeviceListener {

    private long nativeHandle;
    private FrameLayout root;
    private LauncherView launcher;
    private DisplayManager displayManager;
    private InputManager inputManager;
    private boolean destroyed;
    private boolean resumed;
    private boolean startupFailed;
    private boolean qmlReadyHandled;
    private static final long QML_STATUS_POLL_MS = 250L;
    private final Handler startupHandler = new Handler(Looper.getMainLooper());

    // Startup is a sequence of observable states, never a black window:
    // Qt QML loads, the native side is created, the front screen is published
    // and Qt presents its first frame. Any step that fails or never arrives
    // ends in a persistent panel saying so, with the reason in logcat.
    private static final String STARTUP_TAG = "FruityStartup";
    private static final long STARTUP_TIMEOUT_MS = 20000;
    // Bits of MphRead::Droid::StartupFlag.
    private static final int STARTUP_NATIVE_CREATED = 1;
    private static final int STARTUP_FRONT_PUBLISHED = 2;
    private static final int STARTUP_FIRST_FRAME = 4;
    private static final int STARTUP_FAILED = 8;

    static void startupPhase(String phase) {
        Log.i(STARTUP_TAG, "[android-startup] " + phase);
    }

    // Fault injection for the startup gate: a debuggable build, or a release
    // build whose external files directory holds the marker, which only adb
    // (or a cable) can put there. CI pushes it before launching the APK.
    private boolean startupTestHooks() {
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0) return true;
        final File external = getExternalFilesDir(null);
        if (external != null && new File(external, "fruity-startup-test").isFile()) return true;
        // debug.* properties are settable from adb shell and nothing else.
        try {
            final Class<?> properties = Class.forName("android.os.SystemProperties");
            final Object value = properties.getMethod("get", String.class, String.class)
                    .invoke(null, "debug.fruityprime.startuptest", "0");
            return "1".equals(value);
        } catch (ReflectiveOperationException | RuntimeException unavailable) {
            return false;
        }
    }

    private static void setEnv(String name, String value) {
        try {
            Os.setenv(name, value, true);
        } catch (ErrnoException error) {
            Log.w(STARTUP_TAG, "could not set " + name, error);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        final DisplayMetrics metrics = getResources().getDisplayMetrics();
        startupPhase("activity_on_create sdk=" + Build.VERSION.SDK_INT
                + " device=" + Build.MANUFACTURER + "/" + Build.MODEL
                + " abi=" + String.join(",", Build.SUPPORTED_ABIS)
                + " screen=" + metrics.widthPixels + "x" + metrics.heightPixels
                + " orientation=" + getResources().getConfiguration().orientation);

        // The menus are told the display density directly: Qt sizes their
        // view in physical pixels whatever its own pixel ratio then is.
        setEnv("FRUITY_DENSITY", Float.toString(metrics.density));

        // Read before Qt starts: its main() reads the environment once.
        // FRUITY_SURFACE_CONTAINER is an A/B switch for a device whose
        // launcher stays black, so it is honoured in a release build too.
        final Intent intent = getIntent();
        final String container = intent == null ? null : intent.getStringExtra("fruity.surfaceContainer");
        if ("default".equals(container)) setEnv("FRUITY_SURFACE_CONTAINER", "default");
        String qmlSource = null;
        if (intent != null && startupTestHooks()) {
            qmlSource = intent.getStringExtra("fruity.testQmlSource");
            if (intent.getBooleanExtra("fruity.testNativeCreateFail", false)) {
                setEnv("FRUITY_TEST_NATIVE_CREATE_FAIL", "1");
            }
        }

        root = new FrameLayout(this);
        launcher = qmlSource == null ? new LauncherView(this) : new LauncherView(this, qmlSource);
        startupPhase("launcher_view_constructed container=" + (container == null ? "textureview" : container)
                + (qmlSource == null ? "" : " qml=" + qmlSource));
        root.addView(
                launcher,
                new FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.MATCH_PARENT));
        launcher.setStatusChangeListener(status -> onQmlStatusChanged(status, savedInstanceState));
        setContentView(root);
        // QtQuickView starts loading inside its constructor. It can become
        // READY before the listener is installed, so observe current status
        // independently rather than relying on an edge-triggered callback.
        startupHandler.post(() -> pollQmlStatus(savedInstanceState));
        startupHandler.postDelayed(this::checkStartup, STARTUP_TIMEOUT_MS);
    }

    private void onQmlStatusChanged(org.qtproject.qt.android.QtQmlStatus status,
            Bundle savedInstanceState) {
        if (destroyed || startupFailed) {
            return;
        }
        if (status == org.qtproject.qt.android.QtQmlStatus.LOADING) {
            startupPhase("qml_status_loading");
        } else if (status == org.qtproject.qt.android.QtQmlStatus.READY && !qmlReadyHandled) {
            qmlReadyHandled = true;
            startupPhase("qml_status_ready");
            runOnUiThread(() -> createNative(savedInstanceState));
        } else if (status == org.qtproject.qt.android.QtQmlStatus.ERROR) {
            startupPhase("qml_status_error native_handle=" + nativeHandle);
            runOnUiThread(() -> failStartup(
                    "The menus could not be loaded (QML error). See logcat tags FruityQt and FruityStartup."));
        }
    }

    private void pollQmlStatus(Bundle savedInstanceState) {
        if (destroyed || startupFailed || qmlReadyHandled || launcher == null) {
            return;
        }
        final org.qtproject.qt.android.QtQmlStatus status = launcher.getStatus();
        if (status == org.qtproject.qt.android.QtQmlStatus.READY
                || status == org.qtproject.qt.android.QtQmlStatus.ERROR) {
            startupPhase("qml_status_poll_recovered status=" + status);
            onQmlStatusChanged(status, savedInstanceState);
        } else {
            startupHandler.postDelayed(() -> pollQmlStatus(savedInstanceState), QML_STATUS_POLL_MS);
        }
    }

    private void createNative(Bundle savedInstanceState) {
        if (destroyed || startupFailed || nativeHandle != 0) {
            return;
        }
        startupPhase("native_create_begin");
        long handle = 0;
        String error = null;
        try {
            InstallResultReceiver.ensureBound();
            handle = nativeCreate(savedInstanceState, root, launcher);
        } catch (Throwable failure) {
            // A LinkageError is not a RuntimeException, and one escaping
            // here is swallowed by Qt's status listener: catch everything.
            Log.e(STARTUP_TAG, "[android-startup] native_create_threw", failure);
            error = failure.getClass().getSimpleName() + ": " + failure.getMessage();
        }
        if (handle == 0) {
            startupPhase("native_create_failed " + (error == null ? "handle=0" : error));
            failStartup("Native startup failed" + (error == null ? "." : ": " + error));
            return;
        }
        nativeHandle = handle;
        startupPhase("native_create_ok handle=" + handle);
        if (resumed) nativeOnResume(nativeHandle);
        nativeOnWindowFocusChanged(nativeHandle, hasWindowFocus());
    }

    // The watchdog: by now Qt has presented a frame and the front screen is
    // published, or startup is reported as failed rather than left black.
    private void checkStartup() {
        if (destroyed || startupFailed) {
            return;
        }
        if (!resumed) {
            // A hidden Activity draws nothing; judge it once it is back.
            startupHandler.postDelayed(this::checkStartup, STARTUP_TIMEOUT_MS);
            return;
        }
        int flags;
        String nativeError = "";
        try {
            flags = nativeStartupFlags();
            nativeError = nativeStartupError();
        } catch (UnsatisfiedLinkError notLoaded) {
            failStartup("The game library did not load within " + (STARTUP_TIMEOUT_MS / 1000) + " seconds.");
            return;
        }
        final boolean done = (flags & STARTUP_FIRST_FRAME) != 0 && (flags & STARTUP_FRONT_PUBLISHED) != 0;
        if (done) {
            startupPhase("startup_complete");
            return;
        }
        if (nativeHandle == 0 && launcher != null) {
            startupPhase("qml_status_at_timeout status=" + launcher.getStatus());
        }
        String missing = (flags & STARTUP_FAILED) != 0 ? nativeError
                : nativeHandle == 0 ? "the menus never became ready"
                : (flags & STARTUP_FRONT_PUBLISHED) == 0 ? "the front screen was never published"
                : "Qt never presented a frame";
        startupPhase("startup_timeout flags=" + flags + " reason=" + missing);
        failStartup("Startup did not finish: " + missing + ".");
    }

    // A failure is a state with a screen of its own: an opaque native panel
    // over everything, which needs neither Qt nor the game to draw.
    private void failStartup(String message) {
        if (destroyed || startupFailed) {
            return;
        }
        startupFailed = true;
        Log.e(STARTUP_TAG, "[android-startup] failure_panel " + message);
        final TextView panel = fruityCreateNoticeTextView(
                "Fruity Prime could not start\n\n" + message
                        + "\n\nAndroid " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + "), "
                        + Build.MANUFACTURER + " " + Build.MODEL,
                0xFFE8ECF4, 0xFF10141C);
        panel.setPadding(48, 48, 48, 48);
        panel.setClickable(true);
        if (launcher != null) launcher.setVisibility(View.GONE);
        root.addView(panel, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        panel.bringToFront();
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        if (nativeHandle != 0) {
            nativeOnConfigurationChanged(nativeHandle, newConfig);
        }
    }

    @Override
    protected void onPause() {
        resumed = false;
        if (nativeHandle != 0) {
            nativeOnPause(nativeHandle);
        }
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        if (nativeHandle != 0) {
            nativeOnResume(nativeHandle);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (nativeHandle != 0) {
            nativeOnWindowFocusChanged(nativeHandle, hasFocus);
        }
    }

    @Override
    protected void onDestroy() {
        destroyed = true;
        startupHandler.removeCallbacksAndMessages(null);
        final long handle = nativeHandle;
        nativeHandle = 0;
        if (handle != 0) {
            nativeDestroy(handle);
        }
        super.onDestroy();
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        if (nativeHandle != 0) {
            nativeOnBackPressed(nativeHandle);
        } else {
            super.onBackPressed();
        }
    }

    @SuppressWarnings("deprecation")
    public void fruitySuperOnBackPressed() {
        super.onBackPressed();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (nativeHandle != 0 && nativeDispatchKeyEvent(nativeHandle, event)) {
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (nativeHandle != 0 && nativeDispatchTouchEvent(nativeHandle, event)) {
            return true;
        }
        return super.dispatchTouchEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (nativeHandle != 0 && nativeDispatchGenericMotionEvent(nativeHandle, event)) {
            return true;
        }
        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    public void onDisplayAdded(int displayId) {
        if (nativeHandle != 0) {
            nativeOnDisplayAdded(nativeHandle, displayId);
        }
    }

    @Override
    public void onDisplayRemoved(int displayId) {
        if (nativeHandle != 0) {
            nativeOnDisplayRemoved(nativeHandle, displayId);
        }
    }

    @Override
    public void onDisplayChanged(int displayId) {
        if (nativeHandle != 0) {
            nativeOnDisplayChanged(nativeHandle, displayId);
        }
    }

    @Override
    public void onInputDeviceAdded(int deviceId) {
        if (nativeHandle != 0) {
            nativeOnInputDeviceAdded(nativeHandle, deviceId);
        }
    }

    @Override
    public void onInputDeviceChanged(int deviceId) {
        if (nativeHandle != 0) {
            nativeOnInputDeviceChanged(nativeHandle, deviceId);
        }
    }

    @Override
    public void onInputDeviceRemoved(int deviceId) {
        if (nativeHandle != 0) {
            nativeOnInputDeviceRemoved(nativeHandle, deviceId);
        }
    }

    public String fruityExternalFilesPath() {
        final File directory = getExternalFilesDir(null);
        return directory == null ? null : directory.getAbsolutePath();
    }

    public String fruityInternalFilesPath() {
        final File directory = getFilesDir();
        return directory == null ? null : directory.getAbsolutePath();
    }

    public GameSurfaceView fruityCreateGameSurfaceView() {
        return new GameSurfaceView(this);
    }

    public TouchOverlayView fruityCreateTouchOverlayView() {
        return new TouchOverlayView(this);
    }

    public TextView fruityCreateNoticeTextView(
            String text,
            int textArgb,
            int backgroundArgb) {
        final TextView view = new TextView(this);
        view.setText(text);
        view.setTextColor(textArgb);
        view.setBackgroundColor(backgroundArgb);
        view.setGravity(Gravity.CENTER);
        view.setTextAlignment(View.TEXT_ALIGNMENT_CENTER);
        return view;
    }

    public void fruityShowToast(String text) {
        Toast.makeText(this, text, Toast.LENGTH_LONG).show();
    }

    public void fruityPostNativeTask(long token) {
        runOnUiThread(() -> nativeRunTask(token));
    }

    public void fruityPostNativeTaskDelayed(View view, long token, long milliseconds) {
        view.postDelayed(() -> nativeRunTask(token), Math.max(0L, milliseconds));
    }

    public void fruitySetDisplayListenerEnabled(boolean enabled) {
        if (displayManager == null) {
            displayManager = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        }
        if (displayManager == null) {
            return;
        }
        if (enabled) {
            displayManager.registerDisplayListener(this, new Handler(Looper.getMainLooper()));
        } else {
            displayManager.unregisterDisplayListener(this);
        }
    }

    public void fruitySetInputListenerEnabled(boolean enabled) {
        if (inputManager == null) {
            inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        }
        if (inputManager == null) {
            return;
        }
        if (enabled) {
            inputManager.registerInputDeviceListener(this, new Handler(Looper.getMainLooper()));
        } else {
            inputManager.unregisterInputDeviceListener(this);
        }
    }

    public void fruityPrepareAssets(String rootPath) throws IOException {
        final File rootDirectory = new File(rootPath);
        copyAssetTree(getAssets(), "Assets", new File(rootDirectory, "Assets"));
    }

    private static void copyAssetTree(
            AssetManager assets,
            String assetPath,
            File destination) throws IOException {
        final String[] children = assets.list(assetPath);
        if (children != null && children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs() && !destination.isDirectory()) {
                throw new IOException("Could not create " + destination);
            }
            for (String child : children) {
                copyAssetTree(
                        assets,
                        assetPath + "/" + child,
                        new File(destination, child));
            }
            return;
        }

        final File parent = destination.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs() && !parent.isDirectory()) {
            throw new IOException("Could not create " + parent);
        }

        try (InputStream input = assets.open(assetPath);
             FileOutputStream output = new FileOutputStream(destination, false)) {
            final byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = input.read(buffer)) >= 0) {
                if (read != 0) {
                    output.write(buffer, 0, read);
                }
            }
        }
    }

    private static final int REQUEST_PICK_ROM = 4201;

    /**
     * "Choose your .nds file": Android's own document picker. The result is a
     * content:// document with no path behind it, so it is copied into the
     * app's cache for the extractor, which reads a real file. Called by the
     * native side (on the UI thread) and answered through nativeOnRomPicked,
     * with null when the player cancelled or the copy failed.
     */
    public void requestRomPick() {
        try {
            final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            // .nds has no MIME type of its own; anything else would grey the file out.
            intent.setType("*/*");
            startActivityForResult(intent, REQUEST_PICK_ROM);
        } catch (RuntimeException failure) {
            Log.w(STARTUP_TAG, "document picker unavailable", failure);
            nativeOnRomPicked(null);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_PICK_ROM) {
            return;
        }
        final Uri uri = resultCode == RESULT_OK && data != null ? data.getData() : null;
        if (uri == null) {
            nativeOnRomPicked(null);
            return;
        }
        // A cartridge dump is 64-128 MB: not on the UI thread.
        new Thread(() -> {
            String result = null;
            final File destination = new File(getCacheDir(), "picked-rom.nds");
            try (InputStream input = getContentResolver().openInputStream(uri);
                 FileOutputStream output = new FileOutputStream(destination, false)) {
                if (input != null) {
                    final byte[] buffer = new byte[256 * 1024];
                    int read;
                    while ((read = input.read(buffer)) >= 0) {
                        output.write(buffer, 0, read);
                    }
                    result = destination.getAbsolutePath();
                }
            } catch (IOException | RuntimeException failure) {
                Log.w(STARTUP_TAG, "could not copy the picked file", failure);
            }
            nativeOnRomPicked(result);
        }, "rom-pick-copy").start();
    }

    private static native void nativeOnRomPicked(String path);

    private native long nativeCreate(
            Bundle savedInstanceState,
            FrameLayout root,
            LauncherView launcher);
    private native void nativeDestroy(long handle);
    private native void nativeOnConfigurationChanged(long handle, Configuration configuration);
    private native void nativeOnPause(long handle);
    private native void nativeOnResume(long handle);
    private native void nativeOnWindowFocusChanged(long handle, boolean hasFocus);
    private native void nativeOnBackPressed(long handle);
    private native boolean nativeDispatchKeyEvent(long handle, KeyEvent event);
    private native boolean nativeDispatchTouchEvent(long handle, MotionEvent event);
    private native boolean nativeDispatchGenericMotionEvent(long handle, MotionEvent event);
    private native void nativeOnDisplayAdded(long handle, int displayId);
    private native void nativeOnDisplayRemoved(long handle, int displayId);
    private native void nativeOnDisplayChanged(long handle, int displayId);
    private native void nativeOnInputDeviceAdded(long handle, int deviceId);
    private native void nativeOnInputDeviceChanged(long handle, int deviceId);
    private native void nativeOnInputDeviceRemoved(long handle, int deviceId);

    private static native void nativeRunTask(long token);
    private static native int nativeStartupFlags();
    private static native String nativeStartupError();
}
