package zui;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * Broadcast receiver compiled into the APK dex. The tray plugin posts an
 * ongoing notification whose content intent delivers this action. ART loads
 * the class through the application ClassLoader.
 */
public final class ZuiTrayReceiver extends BroadcastReceiver {
    public ZuiTrayReceiver() {}

    @Override
    public void onReceive(Context context, Intent intent) {
        nativeOnActivate();
    }

    private static native void nativeOnActivate();
}
