package zui;

import android.media.session.MediaSession;

/**
 * MediaSession.Callback compiled into the APK dex. ART loads this class
 * through the application ClassLoader. It does not accept a JVM class file
 * from JNI DefineClass.
 */
public final class ZuiMediaCallback extends MediaSession.Callback {
    public ZuiMediaCallback() {}

    @Override
    public void onPlay() {
        nativeOnPlay();
    }

    @Override
    public void onSkipToNext() {
        nativeOnNext();
    }

    @Override
    public void onSkipToPrevious() {
        nativeOnPrevious();
    }

    private static native void nativeOnPlay();

    private static native void nativeOnNext();

    private static native void nativeOnPrevious();
}
