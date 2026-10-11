package org.mavlink.qgroundcontrol;

import android.content.Context;
import com.skydroid.rcsdk.KeyManager;
import com.skydroid.rcsdk.RCSDKManager;
import com.skydroid.rcsdk.SDKManagerCallBack;
import com.skydroid.rcsdk.common.callback.CompletionCallbackWith;
import com.skydroid.rcsdk.common.error.SkyException;
import com.skydroid.rcsdk.key.RemoteControllerKey;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

/** Owns the RCSDK lifecycle and the single G-series channel polling loop. */
final class QGCRCSDKManager {
    interface KeyChannelsCallback {
        void onResult(int[] values);

        void onError(Throwable error);
    }

    interface KeyChannelsReader {
        void get(KeyChannelsCallback callback);

        void connect(ConnectionCallback callback);
    }

    interface ConnectionCallback {
        void onRcConnected();

        void onRcConnectFail(Throwable error);

        void onRcDisconnect();
    }

    interface SdkDisconnector {
        void disconnect();
    }

    private static final long POLL_INTERVAL_MS = 100;
    private static final long RECONNECT_DELAY_MS = 100;
    private static final long SHUTDOWN_WAIT_MS = 1000;
    private static QGCRCSDKManager instance;

    private final KeyChannelsReader reader;
    private final QGCRCSDKBridge.NativeSink sink;
    private final SdkDisconnector disconnector;
    private final ScheduledExecutorService scheduler;
    private final long pollIntervalMs;
    private final CountDownLatch shutdownWaitEntered;
    private final long shutdownWaitMs;
    private final AtomicBoolean requestInFlight = new AtomicBoolean();
    private final AtomicLong generation = new AtomicLong();
    private final Object lifecycleLock = new Object();
    private ScheduledFuture<?> pollFuture;
    private ScheduledFuture<?> reconnectFuture;
    private boolean started;
    private boolean paused;
    private boolean connected;
    private boolean shutdown;
    private long activeConnectionToken = Long.MIN_VALUE;
    private long connectedCallbackToken = Long.MIN_VALUE;
    private long terminalCallbackToken = Long.MIN_VALUE;
    private long nextConnectionToken;

    static void initialize(Context context) {
        synchronized (QGCRCSDKManager.class) {
            if (instance == null) {
                instance = new QGCRCSDKManager(new ProductionReader(context),
                        QGCRCSDKBridge.nativeSink(), POLL_INTERVAL_MS, () ->
                                RCSDKManager.INSTANCE.disconnectRC());
            }
            instance.start();
        }
    }

    static void pause() {
        synchronized (QGCRCSDKManager.class) {
            if (instance != null) {
                instance.pausePolling();
            }
        }
    }

    static void resume() {
        synchronized (QGCRCSDKManager.class) {
            if (instance != null) {
                instance.resumePolling();
            }
        }
    }

    static void shutdownInstance() {
        synchronized (QGCRCSDKManager.class) {
            if (instance != null) {
                instance.shutdown();
                instance = null;
            }
        }
    }

    QGCRCSDKManager(KeyChannelsReader reader, QGCRCSDKBridge.NativeSink sink, long pollIntervalMs) {
        this(reader, sink, pollIntervalMs, () -> RCSDKManager.INSTANCE.disconnectRC());
    }

    QGCRCSDKManager(KeyChannelsReader reader, QGCRCSDKBridge.NativeSink sink, long pollIntervalMs,
            SdkDisconnector disconnector) {
        this(reader, sink, pollIntervalMs, disconnector, new CountDownLatch(0), SHUTDOWN_WAIT_MS);
    }

    QGCRCSDKManager(KeyChannelsReader reader, QGCRCSDKBridge.NativeSink sink, long pollIntervalMs,
            SdkDisconnector disconnector, CountDownLatch shutdownWaitEntered) {
        this(reader, sink, pollIntervalMs, disconnector, shutdownWaitEntered, SHUTDOWN_WAIT_MS);
    }

    QGCRCSDKManager(KeyChannelsReader reader, QGCRCSDKBridge.NativeSink sink, long pollIntervalMs,
            SdkDisconnector disconnector, CountDownLatch shutdownWaitEntered, long shutdownWaitMs) {
        this.shutdownWaitEntered = shutdownWaitEntered;
        this.shutdownWaitMs = shutdownWaitMs;
        this.reader = reader;
        this.sink = sink;
        this.pollIntervalMs = pollIntervalMs;
        this.disconnector = disconnector;
        scheduler = Executors.newSingleThreadScheduledExecutor(runnable -> {
            Thread thread = new Thread(runnable, "QGC-RCSDK");
            thread.setDaemon(true);
            return thread;
        });
    }

    synchronized void start() {
        synchronized (lifecycleLock) {
            if (shutdown || started) {
                return;
            }
            started = true;
            paused = false;
            generation.incrementAndGet();
            scheduleConnectLocked(newConnectionTokenLocked(), 0);
        }
    }

    synchronized void stop() {
        synchronized (lifecycleLock) {
            if (!started) {
                return;
            }
            started = false;
            paused = false;
            connected = false;
            generation.incrementAndGet();
            cancelPollingLocked();
            cancelReconnectLocked();
        }
    }

    synchronized void pausePolling() {
        synchronized (lifecycleLock) {
            if (!started || paused) {
                return;
            }
            paused = true;
            generation.incrementAndGet();
            cancelPollingLocked();
            cancelReconnectLocked();
        }
    }

    synchronized void resumePolling() {
        synchronized (lifecycleLock) {
            if (!started || !paused || shutdown) {
                return;
            }
            paused = false;
            if (connected) {
                schedulePollingLocked(generation.incrementAndGet());
            } else {
                scheduleConnectLocked(newConnectionTokenLocked(), 0);
            }
        }
    }

    synchronized void shutdown() {
        synchronized (lifecycleLock) {
            if (shutdown) {
                return;
            }
            shutdown = true;
            started = false;
            paused = false;
            connected = false;
            generation.incrementAndGet();
            cancelPollingLocked();
            cancelReconnectLocked();
        }
        final long deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(shutdownWaitMs);
        waitForRequestToFinish(deadline);
        disconnectWithoutBlocking(deadline);
        scheduler.shutdownNow();
        awaitScheduler(deadline);
    }

    private void awaitScheduler(long deadline) {
        long remaining = deadline - System.nanoTime();
        if (remaining <= 0) {
            return;
        }
        try {
            scheduler.awaitTermination(remaining, TimeUnit.NANOSECONDS);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
        }
    }

    private void disconnectWithoutBlocking(long deadline) {
        CountDownLatch disconnected = new CountDownLatch(1);
        Thread thread = new Thread(() -> {
            try {
                disconnector.disconnect();
            } catch (Throwable error) {
                // Shutdown has already invalidated the generation; never report teardown failures to NativeSink.
            } finally {
                disconnected.countDown();
            }
        }, "QGC-RCSDK-disconnect");
        thread.setDaemon(true);
        thread.start();
        long remaining = deadline - System.nanoTime();
        if (remaining <= 0) {
            return;
        }
        try {
            disconnected.await(remaining, TimeUnit.NANOSECONDS);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
        }
    }

    private long newConnectionTokenLocked() {
        long token = ++nextConnectionToken;
        activeConnectionToken = token;
        return token;
    }

    private void scheduleConnectLocked(long connectionToken, long delayMs) {
        if (!started || paused || shutdown || reconnectFuture != null && !reconnectFuture.isDone()) {
            return;
        }
        reconnectFuture = scheduler.schedule(() -> connect(connectionToken), delayMs,
                TimeUnit.MILLISECONDS);
    }

    private void connect(long connectionToken) {
        synchronized (lifecycleLock) {
            reconnectFuture = null;
            if (!isCurrentConnectionTokenLocked(connectionToken)) {
                return;
            }
        }
        try {
            reader.connect(new ConnectionCallback() {
                @Override
                public void onRcConnected() {
                    handleConnected(connectionToken);
                }

                @Override
                public void onRcConnectFail(Throwable error) {
                    handleConnectFailure(connectionToken, error);
                }

                @Override
                public void onRcDisconnect() {
                    handleDisconnected(connectionToken);
                }
            });
        } catch (ThreadDeath | VirtualMachineError error) {
            throw error;
        } catch (Throwable error) {
            handleConnectFailure(connectionToken, error);
        }
    }

    private void handleConnected(long connectionToken) {
        boolean shouldDispatch;
        long stateGeneration;
        synchronized (lifecycleLock) {
            if (!isCurrentConnectionTokenLocked(connectionToken)
                    || connectedCallbackToken == connectionToken
                    || terminalCallbackToken == connectionToken) {
                return;
            }
            connectedCallbackToken = connectionToken;
            connected = true;
            stateGeneration = generation.get();
            shouldDispatch = true;
            if (!paused) {
                schedulePollingLocked(stateGeneration);
            }
        }
        if (shouldDispatch) {
            dispatchStateSafely(QGCRCSDKBridge.STATE_CONNECTED,
                    "RC connected", stateGeneration);
        }
    }

    private void handleDisconnected(long connectionToken) {
        long nextGeneration;
        synchronized (lifecycleLock) {
            if (!isCurrentConnectionTokenLocked(connectionToken)
                    || terminalCallbackToken == connectionToken) {
                return;
            }
            terminalCallbackToken = connectionToken;
            connected = false;
            cancelPollingLocked();
            nextGeneration = generation.incrementAndGet();
            scheduleConnectLocked(newConnectionTokenLocked(), RECONNECT_DELAY_MS);
        }
        dispatchStateSafely(QGCRCSDKBridge.STATE_DISCONNECTED,
                "RC disconnected", nextGeneration);
    }

    private void handleConnectFailure(long connectionToken, Throwable error) {
        long nextGeneration;
        synchronized (lifecycleLock) {
            if (!isCurrentConnectionTokenLocked(connectionToken)
                    || connectedCallbackToken == connectionToken
                    || terminalCallbackToken == connectionToken) {
                return;
            }
            terminalCallbackToken = connectionToken;
            connected = false;
            cancelPollingLocked();
            nextGeneration = generation.incrementAndGet();
            scheduleConnectLocked(newConnectionTokenLocked(), RECONNECT_DELAY_MS);
        }
        dispatchStateSafely(QGCRCSDKBridge.STATE_ERROR,
                error == null ? "RCSDK error" : error.toString(), nextGeneration);
    }

    private void schedulePollingLocked(long pollingGeneration) {
        if (pollFuture != null && !pollFuture.isDone()) {
            return;
        }
        pollFuture = scheduler.scheduleWithFixedDelay(
                () -> pollOnce(pollingGeneration), 0, pollIntervalMs, TimeUnit.MILLISECONDS);
    }

    private void cancelPollingLocked() {
        if (pollFuture != null) {
            pollFuture.cancel(false);
            pollFuture = null;
        }
    }

    private void cancelReconnectLocked() {
        if (reconnectFuture != null) {
            reconnectFuture.cancel(false);
            reconnectFuture = null;
        }
    }

    private void pollOnce(long pollingGeneration) {
        synchronized (lifecycleLock) {
            if (!isCurrentLocked(pollingGeneration) || !connected || paused
                    || !requestInFlight.compareAndSet(false, true)) {
                return;
            }
        }
        try {
            reader.get(new KeyChannelsCallback() {
                @Override
                public void onResult(int[] values) {
                    try {
                        if (values != null && isCurrent(pollingGeneration)) {
                            dispatchChannelsSafely(values, pollingGeneration);
                        }
                    } finally {
                        finishRequest();
                    }
                }

                @Override
                public void onError(Throwable error) {
                    try {
                        if (isCurrent(pollingGeneration)) {
                            handleError(pollingGeneration, error);
                        }
                    } finally {
                        finishRequest();
                    }
                }
            });
        } catch (ThreadDeath | VirtualMachineError error) {
            finishRequest();
            throw error;
        } catch (Throwable error) {
            try {
                if (isCurrent(pollingGeneration)) {
                    handleError(pollingGeneration, error);
                }
            } finally {
                finishRequest();
            }
        }
    }

    private void handleError(long callbackGeneration, Throwable error) {
        synchronized (lifecycleLock) {
            if (!isCurrentLocked(callbackGeneration)) {
                return;
            }
        }
        dispatchStateSafely(QGCRCSDKBridge.STATE_ERROR,
                error == null ? "RCSDK error" : error.toString(), callbackGeneration);
    }

    private void dispatchChannelsSafely(int[] values, long callbackGeneration) {
        try {
            QGCRCSDKBridge.dispatchChannels(sink, values, callbackGeneration);
        } catch (Throwable error) {
            rethrowFatalError(error);
        }
    }

    private void dispatchStateSafely(int state, String message, long callbackGeneration) {
        try {
            QGCRCSDKBridge.dispatchState(sink, state, message, callbackGeneration);
        } catch (Throwable error) {
            rethrowFatalError(error);
        }
    }

    private static void rethrowFatalError(Throwable error) {
        if (error instanceof ThreadDeath) {
            throw (ThreadDeath) error;
        }
        if (error instanceof VirtualMachineError) {
            throw (VirtualMachineError) error;
        }
    }

    private boolean isCurrentLocked(long callbackGeneration) {
        return started && !shutdown && callbackGeneration == generation.get();
    }

    private boolean isCurrentConnectionTokenLocked(long connectionToken) {
        return started && !shutdown && connectionToken == activeConnectionToken;
    }

    private boolean isCurrent(long callbackGeneration) {
        synchronized (lifecycleLock) {
            return isCurrentLocked(callbackGeneration);
        }
    }

    private void finishRequest() {
        synchronized (lifecycleLock) {
            requestInFlight.set(false);
            lifecycleLock.notifyAll();
        }
    }

    private void waitForRequestToFinish(long deadline) {
        synchronized (lifecycleLock) {
            while (requestInFlight.get()) {
                long remaining = deadline - System.nanoTime();
                if (remaining <= 0) {
                    return;
                }
                shutdownWaitEntered.countDown();
                try {
                    TimeUnit.NANOSECONDS.timedWait(lifecycleLock, remaining);
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    return;
                }
            }
        }
    }

    private static final class ProductionReader implements KeyChannelsReader {
        private final Context context;

        ProductionReader(Context context) {
            this.context = context;
        }

        @Override
        public void connect(ConnectionCallback callback) {
            if (context == null) {
                callback.onRcConnectFail(new IllegalArgumentException("RCSDK context is null"));
                return;
            }
            RCSDKManager.INSTANCE.initSDK(context, new SDKManagerCallBack() {
                @Override
                public void onRcConnected() {
                    callback.onRcConnected();
                }

                @Override
                public void onRcConnectFail(SkyException error) {
                    callback.onRcConnectFail(error);
                }

                @Override
                public void onRcDisconnect() {
                    callback.onRcDisconnect();
                }
            });
            RCSDKManager.INSTANCE.setMainThreadCallBack(true);
            RCSDKManager.INSTANCE.connectToRC();
        }

        @Override
        public void get(KeyChannelsCallback callback) {
            KeyManager.INSTANCE.get(RemoteControllerKey.INSTANCE.getKeyChannels(),
                    new CompletionCallbackWith<int[]>() {
                        @Override
                        public void onSuccess(int[] values) {
                            callback.onResult(values);
                        }

                        @Override
                        public void onFailure(SkyException error) {
                            callback.onError(error);
                        }
                    });
        }
    }
}
