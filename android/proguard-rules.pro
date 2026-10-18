# Skydroid RCSDK uses callback and reflective SDK entry points.
-keep class com.skydroid.** { *; }

-keep class org.qtproject.qt.** { *; }
-keep class org.kde.necessitas.** { *; }
-keep class ru.dublgis.** { *; }
-keep class com.falsinsoft.qtandroidtools.** { *; }

# RCSDK bridge methods are registered and invoked from native code.
-keep class org.mavlink.qgroundcontrol.QGCRCSDKBridge { *; }

-keepclasseswithmembers class org.mavlink.qgroundcontrol.QGCActivity {
    native <methods>;
}
-keepclasseswithmembers class org.mavlink.qgroundcontrol.QGCUsbSerialManager {
    native <methods>;
}
# Static methods are resolved from C++ by method name/signature.
-keepclassmembers class org.mavlink.qgroundcontrol.QGCActivity {
    public static *;
}
-keepclassmembers class org.mavlink.qgroundcontrol.QGCUsbSerialManager {
    public static *;
}
-keep class org.mavlink.qgroundcontrol.QGCUsbId { *; }
-keep class org.mavlink.qgroundcontrol.QGCUsbSerialProber { *; }
-keep class org.mavlink.qgroundcontrol.QGCLogger { *; }
-keep class org.mavlink.qgroundcontrol.QGCFtdiSerialDriver { *; }
-keep class org.mavlink.qgroundcontrol.QGCFtdiSerialDriver$QGCFtdiSerialPort { *; }
-keep class org.mavlink.qgroundcontrol.QGCFtdiDriver { *; }
-keep class org.mavlink.qgroundcontrol.QGCSDLManager { *; }

# SDL - native method stubs required for JNI registration
-keep class org.libsdl.app.** { *; }

# GStreamer - Java callback classes are reached from native plugin code
-keep class org.freedesktop.gstreamer.** { *; }
-keep class org.freedesktop.gstreamer.androidmedia.** { *; }

# usb-serial-for-android
-keep class com.hoho.android.usbserial.** { *; }

# AndroidX FileProvider
-keep class androidx.core.content.FileProvider { *; }

# Preserve native method names
-keepclasseswithmembernames class * {
    native <methods>;
}

# Preserve enums (used by serialization)
-keepclassmembers enum * {
    public static **[] values();
    public static ** valueOf(java.lang.String);
}

# Optional RCSDK/H12/FishSemi deps not shipped in G20 AAR set — ignore for R8
-dontwarn com.fishsemi.sdk.**
-dontwarn com.skydroid.fpvplayer.**
-dontwarn org.apache.commons.net.**
