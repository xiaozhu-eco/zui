package zui;

import android.app.Activity;
import android.app.AlarmManager;
import android.app.NotificationManager;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.PowerManager;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.view.View;
import android.view.WindowInsets;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Android side of IPlatform. The app calls {@link #attach} from its Activity
 * and forwards {@link #onActivityResult}. C++ presents pickers through the
 * static methods and receives paths via nativePickFinished.
 * Plugin document picks use {@link #REQ_PLUGIN_OPEN} and nativePluginPickFinished.
 */
public final class ZuiHost {
    public static final int REQ_OPEN = 0x7A01;
    public static final int REQ_SAVE = 0x7A02;
    public static final int REQ_FOLDER = 0x7A03;
    public static final int REQ_PLUGIN_OPEN = 0x7A21;
    public static final int REQ_PLUGIN_SAVE = 0x7A22;
    public static final int REQ_PLUGIN_FOLDER = 0x7A23;
    public static final int REQ_PERMISSION = 0x7A10;
    public static final int REQ_PERMISSION_SETTINGS = 0x7A11;

    // The states zui::PermissionState reports. Kept in step with host_types.h.
    public static final int PERMISSION_UNSUPPORTED = 0;
    public static final int PERMISSION_NOT_REQUESTED = 1;
    public static final int PERMISSION_GRANTED = 2;
    public static final int PERMISSION_DENIED = 3;
    public static final int PERMISSION_BLOCKED = 4;

    private static Activity activity;
    private static volatile boolean pluginPickWaiting;
    private static volatile boolean pluginPickNested;
    /** The stable name of the permission whose dialog is up. */
    private static String pendingPermission;
    /** True when the pending request opened a settings page rather than a dialog. */
    private static boolean pendingSettings;
    /** Names the user has been asked for; without this, "not asked yet" and
     *  "refused for good" look the same. */
    private static final java.util.Set<String> asked =
            new java.util.HashSet<String>();

    private ZuiHost() {}

    public static void attach(Activity host) {
        activity = host;
        nativeSetActivity(host);
    }

    public static Activity currentActivity() {
        return activity;
    }

    public static void beginPluginPick() {
        pluginPickWaiting = true;
    }

    public static void cancelPluginPick() {
        synchronized (ZuiHost.class) {
            pluginPickWaiting = false;
            ZuiHost.class.notifyAll();
        }
    }

    /**
     * Blocks until {@link #endPluginPickWait()}. On the main thread this pumps a nested
     * looper so {@link #onActivityResult} can deliver the document the plugin started.
     */
    public static void awaitPluginPick() {
        if (!pluginPickWaiting) {
            return;
        }
        Looper looper = Looper.myLooper();
        if (looper != null && looper == Looper.getMainLooper()) {
            pluginPickNested = true;
            try {
                Looper.loop();
            } catch (PluginPickDone done) {
                // The plugin result was stored before this left the nested looper.
            } finally {
                pluginPickNested = false;
                pluginPickWaiting = false;
            }
            return;
        }
        synchronized (ZuiHost.class) {
            while (pluginPickWaiting) {
                try {
                    ZuiHost.class.wait();
                } catch (InterruptedException stopped) {
                    return;
                }
            }
        }
    }

    private static void endPluginPickWait() {
        synchronized (ZuiHost.class) {
            pluginPickWaiting = false;
            ZuiHost.class.notifyAll();
        }
        if (pluginPickNested && Looper.myLooper() == Looper.getMainLooper()) {
            throw new PluginPickDone();
        }
    }

    private static final class PluginPickDone extends RuntimeException {
        PluginPickDone() {}
    }

    /** The stable name (`"camera"`, …) as the Android permission, or null. */
    private static String androidName(String name) {
        try {
            return com.zui.runtime.ZuiPermissions.android(name);
        } catch (Throwable missing) {
            // A package built without the generated table: a caller may pass the
            // Android name itself.
            return name != null && name.startsWith("android.permission.") ? name : null;
        }
    }

    private static boolean catalogRuntime(String name) {
        try {
            return com.zui.runtime.ZuiPermissions.runtime(name);
        } catch (Throwable missing) {
            return true;
        }
    }

    private static boolean catalogSettings(String name) {
        try {
            return com.zui.runtime.ZuiPermissions.settings(name);
        } catch (Throwable missing) {
            return false;
        }
    }

    private static boolean declared(String androidName) {
        if (activity == null) {
            return false;
        }
        try {
            android.content.pm.PackageInfo info = activity.getPackageManager().getPackageInfo(
                    activity.getPackageName(), android.content.pm.PackageManager.GET_PERMISSIONS);
            String[] requested = info.requestedPermissions;
            if (requested == null) {
                return false;
            }
            for (String one : requested) {
                if (androidName.equals(one)) {
                    return true;
                }
            }
        } catch (Throwable ignored) {
        }
        return false;
    }

    private static boolean rememberAsked(String name) {
        if (asked.contains(name)) {
            return true;
        }
        asked.add(name);
        if (activity != null) {
            activity.getPreferences(0).edit().putBoolean(name, true).apply();
        }
        return false;
    }

    private static boolean wasAsked(String name) {
        if (asked.contains(name)) {
            return true;
        }
        if (activity == null) {
            return false;
        }
        final boolean seen = activity.getPreferences(0).getBoolean(name, false);
        if (seen) {
            asked.add(name);
        }
        return seen;
    }

    private static int askedOrBlocked(String name) {
        return wasAsked(name) ? PERMISSION_BLOCKED : PERMISSION_NOT_REQUESTED;
    }

    /** App-op / settings permissions that `requestPermissions` cannot grant. */
    private static int specialState(String androidName, String name) {
        try {
            if ("android.permission.MANAGE_EXTERNAL_STORAGE".equals(androidName)) {
                if (Build.VERSION.SDK_INT < 30) {
                    return PERMISSION_UNSUPPORTED;
                }
                return Environment.isExternalStorageManager() ? PERMISSION_GRANTED
                                                             : askedOrBlocked(name);
            }
            if ("android.permission.SYSTEM_ALERT_WINDOW".equals(androidName)) {
                return Settings.canDrawOverlays(activity) ? PERMISSION_GRANTED
                                                         : askedOrBlocked(name);
            }
            if ("android.permission.REQUEST_INSTALL_PACKAGES".equals(androidName)) {
                if (Build.VERSION.SDK_INT < 26) {
                    return PERMISSION_GRANTED;
                }
                return activity.getPackageManager().canRequestPackageInstalls()
                        ? PERMISSION_GRANTED : askedOrBlocked(name);
            }
            if ("android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS".equals(androidName)) {
                PowerManager power =
                        (PowerManager) activity.getSystemService(Context.POWER_SERVICE);
                if (power == null) {
                    return PERMISSION_UNSUPPORTED;
                }
                return power.isIgnoringBatteryOptimizations(activity.getPackageName())
                        ? PERMISSION_GRANTED : askedOrBlocked(name);
            }
            if ("android.permission.SCHEDULE_EXACT_ALARM".equals(androidName)) {
                if (Build.VERSION.SDK_INT < 31) {
                    return PERMISSION_GRANTED;
                }
                AlarmManager alarms =
                        (AlarmManager) activity.getSystemService(Context.ALARM_SERVICE);
                if (alarms == null) {
                    return PERMISSION_UNSUPPORTED;
                }
                return alarms.canScheduleExactAlarms() ? PERMISSION_GRANTED : askedOrBlocked(name);
            }
            if ("android.permission.WRITE_SETTINGS".equals(androidName)) {
                return Settings.System.canWrite(activity) ? PERMISSION_GRANTED
                                                         : askedOrBlocked(name);
            }
            if ("android.permission.USE_FULL_SCREEN_INTENT".equals(androidName)) {
                if (Build.VERSION.SDK_INT < 34) {
                    return PERMISSION_GRANTED;
                }
                NotificationManager notify = (NotificationManager) activity.getSystemService(
                        Context.NOTIFICATION_SERVICE);
                if (notify == null) {
                    return PERMISSION_UNSUPPORTED;
                }
                return notify.canUseFullScreenIntent() ? PERMISSION_GRANTED : askedOrBlocked(name);
            }
        } catch (Throwable ignored) {
            return PERMISSION_UNSUPPORTED;
        }
        return -1;
    }

    private static Intent settingsIntent(String androidName) {
        String pkg = activity.getPackageName();
        Uri pkgUri = Uri.parse("package:" + pkg);
        if ("android.permission.MANAGE_EXTERNAL_STORAGE".equals(androidName)
                && Build.VERSION.SDK_INT >= 30) {
            return new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                    .setData(pkgUri);
        }
        if ("android.permission.SYSTEM_ALERT_WINDOW".equals(androidName)) {
            return new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION).setData(pkgUri);
        }
        if ("android.permission.REQUEST_INSTALL_PACKAGES".equals(androidName)
                && Build.VERSION.SDK_INT >= 26) {
            return new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES).setData(pkgUri);
        }
        if ("android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS".equals(androidName)) {
            return new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).setData(pkgUri);
        }
        if ("android.permission.SCHEDULE_EXACT_ALARM".equals(androidName)
                && Build.VERSION.SDK_INT >= 31) {
            return new Intent(Settings.ACTION_REQUEST_SCHEDULE_EXACT_ALARM).setData(pkgUri);
        }
        if ("android.permission.WRITE_SETTINGS".equals(androidName)) {
            return new Intent(Settings.ACTION_MANAGE_WRITE_SETTINGS).setData(pkgUri);
        }
        if ("android.permission.USE_FULL_SCREEN_INTENT".equals(androidName)
                && Build.VERSION.SDK_INT >= 34) {
            return new Intent("android.settings.MANAGE_APP_USE_FULL_SCREEN_INTENT").setData(pkgUri);
        }
        return new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS)
                .setData(Uri.fromParts("package", pkg, null));
    }

    private static boolean openSettings(final String name, final String androidName) {
        if (pendingPermission != null) {
            return false;
        }
        pendingPermission = name;
        pendingSettings = true;
        rememberAsked(name);
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    activity.startActivityForResult(settingsIntent(androidName),
                            REQ_PERMISSION_SETTINGS);
                } catch (Throwable failed) {
                    try {
                        activity.startActivityForResult(
                                new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS)
                                        .setData(Uri.fromParts("package",
                                                activity.getPackageName(), null)),
                                REQ_PERMISSION_SETTINGS);
                    } catch (Throwable ignored) {
                        pendingPermission = null;
                        pendingSettings = false;
                        nativePermissionResult(name, permissionState(name));
                    }
                }
            }
        });
        return true;
    }

    /**
     * The runtime state of a stable permission name, as zui::PermissionState.
     * Unsupported when the platform does not know the name or the manifest does
     * not declare it.
     */
    public static int permissionState(String name) {
        if (activity == null) {
            return PERMISSION_UNSUPPORTED;
        }
        final String androidName = androidName(name);
        if (androidName == null || !declared(androidName)) {
            return PERMISSION_UNSUPPORTED;
        }
        if (catalogSettings(name)) {
            int special = specialState(androidName, name);
            return special >= 0 ? special : PERMISSION_UNSUPPORTED;
        }
        if (Build.VERSION.SDK_INT < 23 || !catalogRuntime(name)) {
            // Install-time, or Android 5: the manifest grant is the whole story.
            return PERMISSION_GRANTED;
        }
        if (activity.checkSelfPermission(androidName)
                == android.content.pm.PackageManager.PERMISSION_GRANTED) {
            return PERMISSION_GRANTED;
        }
        if (!wasAsked(name)) {
            return PERMISSION_NOT_REQUESTED;
        }
        // After a refusal, the system dialog comes back only while it would
        // still make sense to show it.
        return activity.shouldShowRequestPermissionRationale(androidName) ? PERMISSION_DENIED
                                                                         : PERMISSION_BLOCKED;
    }

    /** Android SDK level the ask decision uses for API floors. */
    public static int sdkInt() {
        return Build.VERSION.SDK_INT;
    }

    /** Whether the manifest lists this catalog name. */
    public static boolean isDeclared(String name) {
        final String androidName = androidName(name);
        return androidName != null && declared(androidName);
    }

    /**
     * Shows the runtime permission dialog. The C++ ask decision chooses this
     * over a settings page; the result arrives in {@link #onRequestPermissionsResult}.
     */
    public static boolean showRuntimeDialog(String name) {
        final String androidName = androidName(name);
        if (activity == null || androidName == null || pendingPermission != null) {
            return false;
        }
        pendingPermission = name;
        pendingSettings = false;
        rememberAsked(name);
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                activity.requestPermissions(new String[] {androidName}, REQ_PERMISSION);
            }
        });
        return true;
    }

    /** Opens the settings page for a blocked or special permission. */
    public static boolean openPermissionSettings(String name) {
        final String androidName = androidName(name);
        if (activity == null || androidName == null) {
            return false;
        }
        return openSettings(name, androidName);
    }

    /**
     * Asks the user. The answer arrives through
     * {@link #onRequestPermissionsResult} or a settings page, which the
     * activity forwards here.
     *
     * @return false when the name is not declared, or no dialog could be shown.
     */
    public static boolean requestPermission(String name) {
        final String androidName = androidName(name);
        if (activity == null || androidName == null) {
            return false;
        }
        if (!declared(androidName)) {
            return false;
        }
        final int now = permissionState(name);
        if (now == PERMISSION_GRANTED || now == PERMISSION_UNSUPPORTED) {
            nativePermissionResult(name, now);
            return true;
        }
        if (catalogSettings(name) || now == PERMISSION_BLOCKED) {
            return openSettings(name, androidName);
        }
        if (Build.VERSION.SDK_INT < 23 || !catalogRuntime(name)) {
            nativePermissionResult(name, now);
            return true;
        }
        if (pendingPermission != null) {
            return false;
        }
        pendingPermission = name;
        pendingSettings = false;
        rememberAsked(name);
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                activity.requestPermissions(new String[] {androidName}, REQ_PERMISSION);
            }
        });
        return true;
    }

    /** Completes a settings-page request when the activity is in front again. */
    public static void onResume() {
        if (pendingPermission == null || !pendingSettings) {
            return;
        }
        final String name = pendingPermission;
        pendingPermission = null;
        pendingSettings = false;
        nativePermissionResult(name, permissionState(name));
    }

    /** @return true when this result belonged to zui. */
    public static boolean onRequestPermissionsResult(int request, String[] permissions,
                                                    int[] results) {
        if (request != REQ_PERMISSION) {
            return false;
        }
        final String name = pendingPermission;
        pendingPermission = null;
        pendingSettings = false;
        if (name == null) {
            return true;
        }
        boolean granted = false;
        if (results != null) {
            for (int result : results) {
                granted = granted || result == android.content.pm.PackageManager.PERMISSION_GRANTED;
            }
        }
        nativePermissionResult(name, granted ? PERMISSION_GRANTED : permissionState(name));
        return true;
    }

    /** @return true when this request belonged to zui. */
    public static boolean onActivityResult(int request, int result, Intent data) {
        if (request == REQ_PERMISSION_SETTINGS) {
            final String name = pendingPermission;
            pendingPermission = null;
            pendingSettings = false;
            if (name != null) {
                nativePermissionResult(name, permissionState(name));
            }
            return true;
        }
        if (request == REQ_PLUGIN_OPEN || request == REQ_PLUGIN_SAVE || request == REQ_PLUGIN_FOLDER) {
            deliverPluginPick(request, result, data);
            return true;
        }
        if (request != REQ_OPEN && request != REQ_SAVE && request != REQ_FOLDER) {
            return false;
        }
        if (result != Activity.RESULT_OK || data == null) {
            nativePickFinished(request, false, new String[0]);
            return true;
        }
        try {
            String[] paths = resolve(request, data);
            nativePickFinished(request, paths.length > 0, paths);
        } catch (Throwable ignored) {
            nativePickFinished(request, false, new String[0]);
        }
        return true;
    }

    private static void deliverPluginPick(int request, int result, Intent data) {
        int kind = REQ_OPEN;
        if (request == REQ_PLUGIN_SAVE) {
            kind = REQ_SAVE;
        } else if (request == REQ_PLUGIN_FOLDER) {
            kind = REQ_FOLDER;
        }
        boolean ok = false;
        String[] paths = new String[0];
        if (result == Activity.RESULT_OK && data != null) {
            try {
                paths = resolve(kind, data);
                ok = paths.length > 0;
            } catch (Throwable ignored) {
                paths = new String[0];
                ok = false;
            }
        }
        try {
            nativePluginPickFinished(ok, paths);
        } finally {
            endPluginPickWait();
        }
    }

    /** @return 1 started, 0 no activity, -1 the system rejected the intent. */
    public static int openDocument(boolean multi, String[] mimeTypes) {
        if (activity == null) {
            return 0;
        }
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            if (mimeTypes != null && mimeTypes.length > 0) {
                intent.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes);
            }
            intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, multi);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            activity.startActivityForResult(intent, REQ_OPEN);
            return 1;
        } catch (Throwable ignored) {
            return -1;
        }
    }

    public static int createDocument(String mime, String title) {
        if (activity == null) {
            return 0;
        }
        try {
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType(mime == null || mime.isEmpty() ? "application/octet-stream" : mime);
            if (title != null && !title.isEmpty()) {
                intent.putExtra(Intent.EXTRA_TITLE, title);
            }
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            activity.startActivityForResult(intent, REQ_SAVE);
            return 1;
        } catch (Throwable ignored) {
            return -1;
        }
    }

    public static int openTree() {
        if (activity == null) {
            return 0;
        }
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
            activity.startActivityForResult(intent, REQ_FOLDER);
            return 1;
        } catch (Throwable ignored) {
            return -1;
        }
    }

    public static boolean openUri(String uri) {
        if (activity == null || uri == null) {
            return false;
        }
        try {
            Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(uri));
            activity.startActivity(intent);
            return true;
        } catch (Throwable ignored) {
            return false;
        }
    }

    public static String filesDir() {
        if (activity == null) {
            return null;
        }
        return activity.getFilesDir().getAbsolutePath();
    }

    public static String cacheDirPath() {
        if (activity == null) {
            return null;
        }
        return activity.getCacheDir().getAbsolutePath();
    }

    public static boolean hasClipboard() {
        return clipboard() != null;
    }

    public static String clipboardText() {
        ClipboardManager manager = clipboard();
        if (manager == null) {
            return "";
        }
        ClipData clip = manager.getPrimaryClip();
        if (clip == null || clip.getItemCount() == 0) {
            return "";
        }
        CharSequence text = clip.getItemAt(0).coerceToText(activity);
        return text == null ? "" : text.toString();
    }

    public static boolean setClipboardText(String text) {
        ClipboardManager manager = clipboard();
        if (manager == null) {
            return false;
        }
        manager.setPrimaryClip(ClipData.newPlainText("zui", text == null ? "" : text));
        return true;
    }

    /** @return 0 unknown, 1 light, 2 dark. */
    public static int nightMode() {
        if (activity == null) {
            return 0;
        }
        try {
            android.app.UiModeManager manager =
                    (android.app.UiModeManager) activity.getSystemService(Context.UI_MODE_SERVICE);
            if (manager == null) {
                return 0;
            }
            return manager.getNightMode();
        } catch (Throwable ignored) {
            return 0;
        }
    }

    public static boolean animationsDisabled() {
        if (activity == null) {
            return false;
        }
        try {
            float scale = Settings.Global.getFloat(activity.getContentResolver(),
                    Settings.Global.ANIMATOR_DURATION_SCALE, 1.f);
            return scale == 0.f;
        } catch (Throwable ignored) {
            return false;
        }
    }

    /** IME overlap in CSS px (Android px divided by display density). */
    public static float keyboardBottomCss() {
        if (activity == null || Build.VERSION.SDK_INT < 30) {
            return 0.f;
        }
        View decor = activity.getWindow().getDecorView();
        WindowInsets insets = decor.getRootWindowInsets();
        if (insets == null) {
            return 0.f;
        }
        float density = activity.getResources().getDisplayMetrics().density;
        if (density <= 0.f) {
            density = 1.f;
        }
        return insets.getInsets(WindowInsets.Type.ime()).bottom / density;
    }

    private static ClipboardManager clipboard() {
        if (activity == null) {
            return null;
        }
        return (ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
    }

    private static String[] resolve(int request, Intent data) throws Exception {
        if (request == REQ_OPEN) {
            return copyOpen(data);
        }
        Uri uri = data.getData();
        if (uri == null) {
            return new String[0];
        }
        if (request == REQ_FOLDER) {
            persist(uri);
            return new String[] {pathOrUri(uri)};
        }
        persist(uri);
        String writable = writablePath(uri);
        if (writable == null) {
            return new String[0];
        }
        return new String[] {writable};
    }

    private static String[] copyOpen(Intent data) throws Exception {
        ClipData clip = data.getClipData();
        if (clip != null && clip.getItemCount() > 0) {
            String[] paths = new String[clip.getItemCount()];
            int n = 0;
            for (int i = 0; i < clip.getItemCount(); ++i) {
                Uri uri = clip.getItemAt(i).getUri();
                if (uri == null) {
                    continue;
                }
                paths[n++] = copyToCache(uri);
            }
            if (n == paths.length) {
                return paths;
            }
            String[] trimmed = new String[n];
            System.arraycopy(paths, 0, trimmed, 0, n);
            return trimmed;
        }
        Uri uri = data.getData();
        if (uri == null) {
            return new String[0];
        }
        return new String[] {copyToCache(uri)};
    }

    private static void persist(Uri uri) {
        try {
            int flags = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
            activity.getContentResolver().takePersistableUriPermission(uri, flags);
        } catch (Throwable ignored) {
        }
    }

    private static String pathOrUri(Uri uri) {
        if ("file".equals(uri.getScheme()) && uri.getPath() != null) {
            return uri.getPath();
        }
        return uri.toString();
    }

    private static String writablePath(Uri uri) throws Exception {
        if ("file".equals(uri.getScheme()) && uri.getPath() != null) {
            return uri.getPath();
        }
        ContentResolver resolver = activity.getContentResolver();
        ParcelFileDescriptor pfd = null;
        try {
            pfd = resolver.openFileDescriptor(uri, "rw");
        } catch (Throwable ignored) {
            pfd = resolver.openFileDescriptor(uri, "w");
        }
        if (pfd == null) {
            return pathOrUri(uri);
        }
        int fd = pfd.detachFd();
        return "/proc/self/fd/" + fd;
    }

    private static String copyToCache(Uri uri) throws Exception {
        String name = displayName(uri);
        File dir = new File(activity.getCacheDir(), "zui-pick");
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        File dest = new File(dir, System.currentTimeMillis() + "-" + name);
        try (InputStream in = activity.getContentResolver().openInputStream(uri);
                OutputStream out = new FileOutputStream(dest)) {
            if (in == null) {
                return pathOrUri(uri);
            }
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) >= 0) {
                if (n > 0) {
                    out.write(buf, 0, n);
                }
            }
        }
        return dest.getAbsolutePath();
    }

    private static String displayName(Uri uri) {
        String name = null;
        Cursor cursor = null;
        try {
            cursor = activity.getContentResolver().query(uri, null, null, null, null);
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (index >= 0) {
                    name = cursor.getString(index);
                }
            }
        } catch (Throwable ignored) {
            name = null;
        } finally {
            if (cursor != null) {
                cursor.close();
            }
        }
        if (name == null || name.isEmpty()) {
            name = "document";
        }
        return name.replace('/', '_').replace('\\', '_');
    }

    private static native void nativeSetActivity(Activity activity);

    private static native void nativePickFinished(int request, boolean ok, String[] paths);

    private static native void nativePluginPickFinished(boolean ok, String[] paths);

    private static native void nativePermissionResult(String name, int state);
}
