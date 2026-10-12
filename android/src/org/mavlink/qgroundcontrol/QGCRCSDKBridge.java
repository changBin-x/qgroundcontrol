package org.mavlink.qgroundcontrol;

/** Java boundary used by the Qt JNI adapter and by manager tests. */
final class QGCRCSDKBridge {
    interface NativeSink {
        void onChannels(int[] values, long generation);

        void onState(int state, String message, long generation);

        /** Drops queued C++ callbacks after Java pause/stop/shutdown. */
        void invalidate();
    }

    static final int STATE_CONNECTED = 1;
    static final int STATE_DISCONNECTED = 2;
    static final int STATE_ERROR = 3;

    private static final NativeSink NATIVE_SINK = new NativeSink() {
        @Override
        public void onChannels(int[] values, long generation) {
            nativeChannels(values, generation);
        }

        @Override
        public void onState(int state, String message, long generation) {
            nativeState(state, message, generation);
        }

        @Override
        public void invalidate() {
            nativeInvalidateCallbacks();
        }
    };

    private QGCRCSDKBridge() {
    }

    static NativeSink nativeSink() {
        return NATIVE_SINK;
    }

    static void dispatchChannels(NativeSink sink, int[] values, long generation) {
        if (sink != null && values != null) {
            sink.onChannels(values.clone(), generation);
        }
    }

    static void dispatchState(NativeSink sink, int state, String message, long generation) {
        if (sink != null) {
            sink.onState(state, message, generation);
        }
    }

    static void dispatchInvalidate(NativeSink sink) {
        if (sink != null) {
            sink.invalidate();
        }
    }

    static native void nativeChannels(int[] values, long generation);

    static native void nativeState(int state, String message, long generation);

    /** Invalidates queued C++ callbacks after Java pause/stop/shutdown. */
    static native void nativeInvalidateCallbacks();
}
