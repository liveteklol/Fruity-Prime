#!/usr/bin/env python3
"""Android startup gate: install the APK on a running emulator/device, launch
it, and prove that what the player sees is never a black window.

    python3 tools/ci/android-startup-smoke.py APK [--out DIR] [--case NAME ...]

Cases:
  normal        Qt READY -> native_create_ok -> front_published ->
                first_qt_frame_presented, and a screenshot that is not blank.
  qml-error     an invalid QML source is injected: qml_status_error, the
                persistent failure panel, a non-blank screenshot, no ANR.
  native-fail   nativeCreate is made to throw: native_create_failed, the
                failure panel, a non-blank screenshot.

Injection needs a debuggable APK or the marker file this script pushes into
the app's external files directory (see MainActivity.startupTestHooks).
Exit code 0 = every case passed. Needs only adb on PATH (or ANDROID_SDK_ROOT).
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import time

PACKAGE = "fr.livetek.fruityprime"
ACTIVITY = PACKAGE + "/.MainActivity"
MARKER = "/sdcard/Android/data/%s/files/fruity-startup-test" % PACKAGE
PROPERTY = "debug.fruityprime.startuptest"
FATAL = ("FATAL EXCEPTION", "Fatal signal", "ANR in " + PACKAGE)


def adb_path():
    found = shutil.which("adb")
    if found:
        return found
    for root in (os.environ.get("ANDROID_SDK_ROOT"), os.environ.get("ANDROID_HOME"),
                 os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk")):
        if root:
            candidate = os.path.join(root, "platform-tools", "adb.exe" if os.name == "nt" else "adb")
            if os.path.isfile(candidate):
                return candidate
    sys.exit("adb not found")


ADB = adb_path()


def adb(*args, check=True, binary=False, timeout=120):
    result = subprocess.run([ADB, *args], capture_output=True, timeout=timeout)
    if check and result.returncode != 0:
        raise RuntimeError("adb %s failed: %s" % (" ".join(args), result.stderr.decode(errors="replace")))
    return result.stdout if binary else result.stdout.decode(errors="replace")


def reconnect(timeout=60):
    """Re-establish a dropped adb transport and wait until the device is back."""
    adb("reconnect", "offline", check=False, timeout=30)
    try:
        adb("wait-for-device", check=False, timeout=timeout)
    except subprocess.TimeoutExpired:
        print("  device still missing after %ds" % timeout)
        return
    time.sleep(2)


def logcat():
    return adb("logcat", "-d", "-v", "brief", "FruityStartup:V", "FruityQt:V", "AndroidRuntime:E",
               "ActivityManager:E", "DEBUG:V", "libc:F", "*:S")


def screenshot_variance(path):
    """Raw screencap: header (w, h, format[, colorspace]) then RGBA rows.

    Some emulator/gfxstream combinations occasionally return a valid header
    followed by a truncated pixel stream. Treat that as a transient capture
    failure and retry; never index past the bytes that adb actually returned.
    """
    last_error = "screencap returned no data"
    for attempt in range(1, 6):
        result = subprocess.run([ADB, "exec-out", "screencap"], capture_output=True, timeout=120)
        if result.returncode != 0:
            # The API 35 emulator sometimes drops its adb transport right after
            # an empty capture ("device 'emulator-5554' not found") while the
            # emulator itself stays up: reconnect and wait instead of failing.
            last_error = "adb exec-out screencap failed: " + result.stderr.decode(errors="replace").strip()
            if attempt < 5:
                print("  screencap attempt %d failed: %s; reconnecting" % (attempt, last_error))
                reconnect()
            continue
        raw = result.stdout
        with open(path, "wb") as out:
            out.write(raw)

        if len(raw) < 12:
            last_error = "screencap returned only %d bytes (header needs at least 12)" % len(raw)
        else:
            width, height, pixel_format = struct.unpack_from("<III", raw, 0)
            if width <= 0 or height <= 0 or width > 16384 or height > 16384:
                last_error = "invalid screencap dimensions %dx%d" % (width, height)
            else:
                pixel_bytes = width * height * 4
                if len(raw) >= 16 + pixel_bytes:
                    header = 16
                elif len(raw) >= 12 + pixel_bytes:
                    header = 12
                else:
                    header = 0
                    last_error = (
                        "truncated screencap: got %d bytes for %dx%d format=%d; "
                        "need at least %d"
                        % (len(raw), width, height, pixel_format, 12 + pixel_bytes)
                    )

                if header:
                    pixels = raw[header:header + pixel_bytes]
                    # Sample a grid of luminances; a blank window is one value everywhere.
                    step = max(1, (width * height) // 20000)
                    values = []
                    for index in range(0, width * height, step):
                        r, g, b = pixels[index * 4], pixels[index * 4 + 1], pixels[index * 4 + 2]
                        values.append((r * 299 + g * 587 + b * 114) // 1000)
                    mean = sum(values) / len(values)
                    variance = sum((v - mean) ** 2 for v in values) / len(values)
                    return mean, variance, len(set(values))

        if attempt < 5:
            print("  screencap attempt %d incomplete: %s; retrying" % (attempt, last_error))
            time.sleep(0.5)

    raise RuntimeError("screencap failed after 5 attempts: " + last_error)


def lifecycle(out_dir, pid):
    """Background/foreground, focus loss and two configuration changes on a
    started launcher: the same process must survive each one and keep
    drawing, and none may end on the failure panel."""
    failures = []

    def check(step):
        time.sleep(4)
        now = adb("shell", "pidof", PACKAGE, check=False).strip()
        if now != pid:
            failures.append("%s: process changed (%s -> %s)" % (step, pid, now or "dead"))
        mean, variance, levels = screenshot_variance(os.path.join(out_dir, "lifecycle-%s.raw" % step))
        print("  %s: screen mean=%.1f variance=%.1f levels=%d" % (step, mean, variance, levels))
        if levels < 3 or variance < 1.0:
            failures.append("%s: screenshot is blank" % step)

    adb("shell", "input", "keyevent", "3")             # HOME: onPause/onStop
    time.sleep(3)
    adb("shell", "am", "start", "-W", "-n", ACTIVITY)   # back to the same task
    check("foreground")
    adb("shell", "cmd", "statusbar", "expand-notifications", check=False)  # focus lost
    time.sleep(2)
    adb("shell", "cmd", "statusbar", "collapse", check=False)              # regained
    check("focus")
    adb("shell", "settings", "put", "system", "accelerometer_rotation", "0", check=False)
    # The Activity is sensorLandscape and handles its own configuration
    # changes: the two landscapes and a uiMode flip all reach
    # onConfigurationChanged without recreating it.
    adb("shell", "settings", "put", "system", "user_rotation", "3", check=False)
    check("reverse-landscape")
    adb("shell", "settings", "put", "system", "user_rotation", "1", check=False)
    check("landscape")
    adb("shell", "cmd", "uimode", "night", "yes", check=False)
    check("night-mode")
    adb("shell", "cmd", "uimode", "night", "no", check=False)
    check("day-mode")
    log = logcat()
    for bad in ("failure_panel", "startup_timeout") + FATAL:
        if bad in log:
            failures.append("lifecycle: %s" % bad)
    return failures


def run_case(name, out_dir, timeout, allow_first_frame_retry=True):
    print("== case %s" % name)
    adb("shell", "am", "force-stop", PACKAGE, check=False)
    adb("logcat", "-c", check=False)
    extras = []
    if name == "qml-error":
        extras = ["--es", "fruity.testQmlSource", "qrc:/qt/qml/FruityPrime/Ui/DoesNotExist.qml"]
    elif name == "native-fail":
        extras = ["--ez", "fruity.testNativeCreateFail", "true"]
    # Two ways to arm the hooks, since scoped storage refuses the shell some
    # writes into Android/data: the marker file, and a debug.* property.
    if extras:
        local = os.path.join(out_dir, "fruity-startup-test")
        open(local, "w").close()
        adb("push", local, MARKER, check=False)
        adb("shell", "setprop", PROPERTY, "1")
    else:
        adb("shell", "rm", "-f", MARKER, check=False)
        adb("shell", "setprop", PROPERTY, "0", check=False)
    print(adb("shell", "am", "start", "-W", "-n", ACTIVITY, *extras).strip())

    expected = {
        "normal": ["qml_status_ready", "native_create_ok", "front_published", "first_qt_frame_presented"],
        "lifecycle": ["qml_status_ready", "native_create_ok", "front_published", "first_qt_frame_presented"],
        "qml-error": ["qml_status_error", "failure_panel"],
        "native-fail": ["native_create_failed", "failure_panel"],
    }[name]
    deadline = time.time() + timeout
    log = ""
    while time.time() < deadline:
        log = logcat()
        if all(marker in log for marker in expected) or any(f in log for f in FATAL):
            break
        time.sleep(1)
    # Let the frame settle, then look at it.
    time.sleep(3)
    log = logcat()
    with open(os.path.join(out_dir, "%s-logcat.txt" % name), "w", encoding="utf-8") as out:
        out.write(log)
    failures = ["missing marker %s" % marker for marker in expected if marker not in log]
    failures += ["fatal: %s" % f for f in FATAL if f in log]
    pid = adb("shell", "pidof", PACKAGE, check=False).strip()
    if not pid:
        failures.append("process is not alive")
    mean, variance, levels = screenshot_variance(os.path.join(out_dir, "%s-screen.raw" % name))
    print("screen mean=%.1f variance=%.1f levels=%d" % (mean, variance, levels))
    if levels < 3 or variance < 1.0:
        failures.append("screenshot is blank (mean=%.1f variance=%.2f levels=%d)" % (mean, variance, levels))
    if name == "lifecycle" and not failures:
        failures += lifecycle(out_dir, pid)
        log = logcat()
        with open(os.path.join(out_dir, "%s-logcat.txt" % name), "w", encoding="utf-8") as out:
            out.write(log)
    for line in log.splitlines():
        if "[android-startup]" in line:
            print("  " + line.strip())

    # API 35 emulator graphics initialization is intermittently unable to
    # present the very first Qt/TextureView frame after a fresh AVD boot even
    # though QML, nativeCreate and front publication all completed. This is
    # distinct from an application startup failure: retry only this exact
    # infrastructure signature once, and preserve attempt 1 as evidence.
    retryable_first_frame = (
        name == "normal"
        and allow_first_frame_retry
        and failures == ["missing marker first_qt_frame_presented"]
        and "startup_timeout flags=3 reason=Qt never presented a frame" in log
        and bool(pid)
    )
    if retryable_first_frame:
        for suffix in ("logcat.txt", "screen.raw"):
            source = os.path.join(out_dir, "%s-%s" % (name, suffix))
            preserved = os.path.join(out_dir, "%s-attempt1-%s" % (name, suffix))
            if os.path.exists(source):
                os.replace(source, preserved)
        print("RETRY normal: first Qt frame was not presented after successful native/front startup")
        adb("shell", "rm", "-f", MARKER, check=False)
        adb("shell", "setprop", PROPERTY, "0", check=False)
        return run_case(name, out_dir, timeout, allow_first_frame_retry=False)

    for failure in failures:
        print("FAIL %s: %s" % (name, failure))
    if not failures:
        print("PASS %s" % name)
    adb("shell", "rm", "-f", MARKER, check=False)
    adb("shell", "setprop", PROPERTY, "0", check=False)
    return not failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("apk")
    parser.add_argument("--out", default="android-startup-smoke")
    parser.add_argument("--case", action="append", choices=["normal", "lifecycle", "qml-error", "native-fail"])
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    adb("wait-for-device", timeout=600)
    print(adb("install", "-r", "-g", args.apk, timeout=600).strip())
    results = [run_case(case, args.out, args.timeout) for case in (args.case or ["normal", "lifecycle", "qml-error", "native-fail"])]
    adb("shell", "am", "force-stop", PACKAGE, check=False)
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
