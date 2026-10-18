package zui;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;

/**
 * InvocationHandler compiled into the APK dex. The hotkey plugin wraps
 * Window.Callback with Proxy and this handler. ART will not DefineClass a
 * JVM class file, so the handler has to be a real dex class.
 */
public final class ZuiHotkeyInvoke implements InvocationHandler {
    public ZuiHotkeyInvoke() {}

    @Override
    public Object invoke(Object proxy, Method method, Object[] args) {
        return nativeInvoke(method, args);
    }

    private static native Object nativeInvoke(Method method, Object[] args);
}
