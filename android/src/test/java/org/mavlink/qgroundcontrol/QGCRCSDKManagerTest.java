package org.mavlink.qgroundcontrol;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.After;
import org.junit.Test;

public class QGCRCSDKManagerTest {
    private final List<QGCRCSDKManager> managers = new ArrayList<>();

    @After
    public void tearDown() {
        try {
            for (QGCRCSDKManager manager : managers) {
                try {
                    manager.shutdown();
                } catch (RuntimeException | LinkageError error) {
                    // Continue cleaning up all managers after an individual teardown failure.
                }
            }
        } finally {
            managers.clear();
        }
    }

    @Test
    public void startIsIdempotentAndSchedulesOnePoller() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        manager.start();
        reader.awaitConnectCount(1);
        reader.connection(0).onRcConnected();
        reader.awaitRequest();

        assertEquals(1, reader.requestCount.get());
    }

    @Test
    public void nullResultDoesNotReachNativeSink() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        reader.connection(0).onRcConnected();
        reader.awaitRequest();
        reader.request(0).onResult(null);

        assertEquals(0, sink.channelCount.get());
    }

    @Test
    public void stoppedGenerationDropsLateCallback() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        reader.connection(0).onRcConnected();
        reader.awaitRequest();
        manager.stop();
        reader.request(0).onResult(new int[] {1, 2, 3});

        assertEquals(0, sink.channelCount.get());
    }

    @Test
    public void channelsAreCopiedAndForwarded() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        reader.connection(0).onRcConnected();
        reader.awaitRequest();
        int[] values = new int[16];
        values[0] = 1500;
        reader.request(0).onResult(values);
        values[0] = -1;

        assertTrue(sink.channelCount.await(1, TimeUnit.SECONDS));
        assertEquals(1500, sink.lastValues[0]);
        assertEquals(16, sink.lastValues.length);
    }

    @Test
    public void channelSinkRuntimeExceptionDoesNotStopSubsequentPolling() throws Exception {
        FakeReader reader = new FakeReader();
        ThrowingSink sink = new ThrowingSink(true);
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        reader.connection(0).onRcConnected();
        reader.awaitRequestCount(1);
        reader.request(0).onResult(new int[] {1});
        reader.awaitRequestCount(2);
        reader.request(1).onResult(new int[] {2});

        assertEquals(2, sink.channelCount.get());
    }

    @Test
    public void stateSinkRuntimeExceptionDoesNotStopConnectionManagement() throws Exception {
        FakeReader reader = new FakeReader();
        ThrowingSink sink = new ThrowingSink(false);
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        QGCRCSDKManager.ConnectionCallback firstConnection = reader.connection(0);
        firstConnection.onRcConnected();
        firstConnection.onRcDisconnect();
        reader.awaitConnectCount(2);

        assertEquals(2, reader.connectCount.get());
        assertEquals(2, sink.stateCount.get());
    }

    @Test
    public void channelSinkAssertionErrorDoesNotStopSubsequentPolling() throws Exception {
        FakeReader reader = new FakeReader();
        ThrowingSink sink = new ThrowingSink(true, new AssertionError("channel assertion"));
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        reader.connection(0).onRcConnected();
        reader.awaitRequestCount(1);
        reader.request(0).onResult(new int[] {1});
        reader.awaitRequestCount(2);
        reader.request(1).onResult(new int[] {2});

        assertEquals(2, sink.channelCount.get());
    }

    @Test
    public void stateSinkAssertionErrorDoesNotStopConnectionManagement() throws Exception {
        FakeReader reader = new FakeReader();
        ThrowingSink sink = new ThrowingSink(false, new AssertionError("state assertion"));
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        QGCRCSDKManager.ConnectionCallback firstConnection = reader.connection(0);
        firstConnection.onRcConnected();
        reader.awaitRequestCount(1);
        firstConnection.onRcDisconnect();
        reader.awaitConnectCount(2);

        assertEquals(2, reader.connectCount.get());
        assertEquals(2, sink.stateCount.get());
    }

    @Test
    public void channelSinkThreadDeathIsRethrownWithoutStoppingTestRunner() throws Exception {
        ThreadDeath failure = new ThreadDeath();
        assertFatalChannelFailureIsRethrown(failure);
    }

    @Test
    public void channelSinkVirtualMachineErrorIsRethrownWithoutStoppingTestRunner() throws Exception {
        VirtualMachineError failure = new TestVirtualMachineError();
        assertFatalChannelFailureIsRethrown(failure);
    }

    private void assertFatalChannelFailureIsRethrown(Throwable failure) throws Exception {
        FakeReader reader = new FakeReader();
        ThrowingSink sink = new ThrowingSink(true, failure);
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        reader.connection(0).onRcConnected();
        reader.awaitRequestCount(1);

        AtomicReference<Throwable> capturedFailure = new AtomicReference<>();
        CountDownLatch callbackFinished = new CountDownLatch(1);
        Thread callbackThread = new Thread(() -> {
            try {
                reader.request(0).onResult(new int[] {1});
            } catch (Throwable error) {
                capturedFailure.set(error);
            } finally {
                callbackFinished.countDown();
            }
        }, "QGC-RCSDK-test-fatal-callback");
        try {
            callbackThread.start();
            assertTrue(callbackFinished.await(1, TimeUnit.SECONDS));
            assertSame(failure, capturedFailure.get());
        } finally {
            callbackThread.interrupt();
            joinBounded(callbackThread);
        }
    }

    private static final class TestVirtualMachineError extends VirtualMachineError {
        private static final long serialVersionUID = 1L;
    }

    @Test
    public void pauseAndResumeStopsStaleRequestAndRestartsPolling() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        reader.connection(0).onRcConnected();
        reader.awaitRequestCount(1);
        manager.pausePolling();
        reader.request(0).onResult(new int[] {1});
        manager.resumePolling();
        reader.awaitRequestCount(2);

        assertEquals(1, sink.channelCount.getCount());
    }

    @Test
    public void requestExceptionReleasesInFlightGuard() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        reader.connection(0).onRcConnected();
        reader.awaitRequestCount(1);
        reader.request(0).onError(new IllegalStateException("request failed"));
        reader.awaitRequestCount(2);

        assertTrue(sink.errorCount.get() >= 1);
    }

    @Test
    public void staleDisconnectDoesNotScheduleReconnect() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        reader.connection(0).onRcConnected();
        manager.stop();
        reader.connection(0).onRcDisconnect();

        assertEquals(1, reader.connectCount.get());
    }

    @Test
    public void connectFailureInvalidatesGenerationAndRetries() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        reader.connection(0).onRcConnectFail(new IllegalStateException("not ready"));
        reader.awaitConnectCount(2);
        reader.connection(1).onRcConnected();
        reader.awaitRequestCount(1);

        assertTrue(sink.errorCount.get() >= 1);
        assertEquals(2, reader.connectCount.get());
    }

    @Test
    public void shutdownCallsInjectedDisconnectorWithoutWaitingForever() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        AtomicInteger disconnectCount = new AtomicInteger();
        QGCRCSDKManager manager = new QGCRCSDKManager(reader, sink, 10,
                disconnectCount::incrementAndGet);
        managers.add(manager);

        manager.start();
        manager.shutdown();

        assertEquals(1, disconnectCount.get());
    }

    @Test
    public void connectedCallbackIsConsumedOnceButDisconnectStartsOneReconnect() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        QGCRCSDKManager.ConnectionCallback callback = reader.connection(0);
        callback.onRcConnected();
        reader.awaitRequestCount(1);
        callback.onRcConnected();
        callback.onRcConnectFail(new IllegalStateException("late failure"));
        callback.onRcDisconnect();
        callback.onRcDisconnect();
        callback.onRcConnectFail(new IllegalStateException("duplicate failure"));
        callback.onRcConnectFail(new IllegalStateException("duplicate failure"));

        reader.awaitConnectCount(2);
        assertEquals(1, sink.connectedCount.get());
        assertEquals(0, sink.errorCount.get());
        assertEquals(1, sink.disconnectedCount.get());
        assertEquals(1, reader.requestCount.get());
        assertEquals(2, reader.connectCount.get());
    }

    @Test
    public void repeatedDisconnectIsConsumedOnce() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        QGCRCSDKManager.ConnectionCallback callback = reader.connection(0);
        callback.onRcConnected();
        callback.onRcDisconnect();
        callback.onRcDisconnect();

        reader.awaitConnectCount(2);
        assertEquals(1, sink.disconnectedCount.get());
        assertEquals(2, reader.connectCount.get());
    }

    @Test
    public void repeatedDisconnectAndFailureScheduleOneReconnect() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        QGCRCSDKManager.ConnectionCallback callback = reader.connection(0);
        callback.onRcConnected();
        callback.onRcDisconnect();
        callback.onRcDisconnect();
        callback.onRcConnectFail(new IllegalStateException("duplicate failure"));

        reader.awaitConnectCount(2);
        assertEquals(2, reader.connectCount.get());
    }

    @Test
    public void shutdownWaitsForInFlightCallbackBeforeDisconnect() throws Exception {
        FakeReader reader = new FakeReader();
        AtomicBoolean callbackFinished = new AtomicBoolean();
        BlockingSink sink = new BlockingSink(callbackFinished);
        CountDownLatch disconnectEntered = new CountDownLatch(1);
        AtomicBoolean disconnectBeforeCallback = new AtomicBoolean();
        CountDownLatch shutdownStarted = new CountDownLatch(1);
        CountDownLatch shutdownWaitingForCallback = new CountDownLatch(1);
        CountDownLatch shutdownFinished = new CountDownLatch(1);
        QGCRCSDKManager manager = new QGCRCSDKManager(reader, sink, 10,
                () -> {
                    if (!callbackFinished.get()) {
                        disconnectBeforeCallback.set(true);
                    }
                    disconnectEntered.countDown();
                }, shutdownWaitingForCallback);
        managers.add(manager);

        manager.start();
        reader.awaitConnectCount(1);
        reader.connection(0).onRcConnected();
        reader.awaitRequest();
        Thread callbackThread = new Thread(() -> reader.request(0).onResult(new int[] {1}),
                "QGC-RCSDK-test-callback");
        Thread shutdownThread = new Thread(() -> {
            try {
                shutdownStarted.countDown();
                manager.shutdown();
            } finally {
                shutdownFinished.countDown();
            }
        }, "QGC-RCSDK-test-shutdown");
        try {
            callbackThread.start();
            assertTrue(sink.callbackEntered.await(1, TimeUnit.SECONDS));

            shutdownThread.start();
            assertTrue(shutdownStarted.await(1, TimeUnit.SECONDS));
            assertTrue(shutdownWaitingForCallback.await(1, TimeUnit.SECONDS));
            assertEquals(1, disconnectEntered.getCount());
            sink.releaseCallback.countDown();
            assertTrue(disconnectEntered.await(1, TimeUnit.SECONDS));
            assertFalse(disconnectBeforeCallback.get());
            assertTrue(shutdownFinished.await(1, TimeUnit.SECONDS));
        } finally {
            sink.releaseCallback.countDown();
            callbackThread.interrupt();
            shutdownThread.interrupt();
            joinBounded(callbackThread);
            joinBounded(shutdownThread);
        }
    }

    @Test
    public void shutdownContinuesAfterCallbackDeadlineBeforeDisconnect() throws Exception {
        FakeReader reader = new FakeReader();
        AtomicBoolean callbackFinished = new AtomicBoolean();
        BlockingSink sink = new BlockingSink(callbackFinished);
        CountDownLatch shutdownWaitingForCallback = new CountDownLatch(1);
        CountDownLatch disconnectEntered = new CountDownLatch(1);
        CountDownLatch shutdownFinished = new CountDownLatch(1);
        AtomicBoolean disconnectBeforeCallback = new AtomicBoolean();
        QGCRCSDKManager manager = new QGCRCSDKManager(reader, sink, 10,
                () -> {
                    if (!callbackFinished.get()) {
                        disconnectBeforeCallback.set(true);
                    }
                    disconnectEntered.countDown();
                }, shutdownWaitingForCallback, 50);
        managers.add(manager);

        manager.start();
        reader.awaitConnectCount(1);
        reader.connection(0).onRcConnected();
        reader.awaitRequest();
        Thread callbackThread = new Thread(() -> reader.request(0).onResult(new int[] {1}),
                "QGC-RCSDK-test-callback");
        Thread shutdownThread = new Thread(() -> {
            try {
                manager.shutdown();
            } finally {
                shutdownFinished.countDown();
            }
        }, "QGC-RCSDK-test-shutdown");
        try {
            callbackThread.start();
            assertTrue(sink.callbackEntered.await(1, TimeUnit.SECONDS));

            shutdownThread.start();
            assertTrue(shutdownWaitingForCallback.await(1, TimeUnit.SECONDS));
            assertTrue(shutdownFinished.await(1, TimeUnit.SECONDS));
            assertTrue(disconnectEntered.await(1, TimeUnit.SECONDS));
            assertTrue(disconnectBeforeCallback.get());
        } finally {
            sink.releaseCallback.countDown();
            callbackThread.interrupt();
            shutdownThread.interrupt();
            joinBounded(callbackThread);
            joinBounded(shutdownThread);
        }
        assertTrue(callbackFinished.get());
    }

    @Test
    public void blockingDisconnectorDoesNotBlockShutdownForever() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        CountDownLatch disconnectEntered = new CountDownLatch(1);
        CountDownLatch releaseDisconnect = new CountDownLatch(1);
        CountDownLatch shutdownFinished = new CountDownLatch(1);
        QGCRCSDKManager manager = new QGCRCSDKManager(reader, sink, 10, () -> {
            disconnectEntered.countDown();
            awaitUninterruptibly(releaseDisconnect);
        });
        managers.add(manager);

        manager.start();
        Thread shutdownThread = new Thread(() -> {
            try {
                manager.shutdown();
            } finally {
                shutdownFinished.countDown();
            }
        }, "QGC-RCSDK-test-shutdown");
        try {
            shutdownThread.start();
            assertTrue(disconnectEntered.await(1, TimeUnit.SECONDS));
            assertTrue(shutdownFinished.await(2, TimeUnit.SECONDS));
        } finally {
            releaseDisconnect.countDown();
            shutdownThread.interrupt();
            joinBounded(shutdownThread);
        }
    }

    @Test
    public void shutdownDisconnectorFailureDoesNotReachNativeSink() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        QGCRCSDKManager manager = new QGCRCSDKManager(reader, sink, 10,
                () -> {
                    throw new IllegalStateException("disconnect failed");
                });
        managers.add(manager);

        manager.start();
        manager.shutdown();

        assertEquals(0, sink.errorCount.get());
    }

    @Test
    public void disconnectAfterPauseResumeInvalidatesStateAndSchedulesOneReconnect() throws Exception {
        FakeReader reader = new FakeReader();
        RecordingSink sink = new RecordingSink();
        QGCRCSDKManager manager = newManager(reader, sink);

        manager.start();
        reader.awaitConnectCount(1);
        QGCRCSDKManager.ConnectionCallback firstConnection = reader.connection(0);
        firstConnection.onRcConnected();
        reader.awaitRequest();
        manager.pausePolling();
        reader.request(0).onResult(new int[] {1});
        assertEquals(0, sink.channelCount.getCount());
        manager.resumePolling();
        reader.awaitRequestCount(2);
        firstConnection.onRcDisconnect();

        reader.awaitConnectCount(2);
        assertEquals(1, sink.disconnectedCount.get());
        assertEquals(4, sink.lastDisconnectedGeneration);
        assertEquals(2, reader.connectCount.get());
    }

    private static void joinBounded(Thread thread) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(1);
        while (thread.isAlive()) {
            long remaining = deadline - System.nanoTime();
            if (remaining <= 0) {
                return;
            }
            try {
                thread.join(Math.max(1, TimeUnit.NANOSECONDS.toMillis(remaining)));
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                return;
            }
        }
    }

    private static void awaitUninterruptibly(CountDownLatch latch) {
        boolean interrupted = false;
        while (true) {
            try {
                latch.await();
                break;
            } catch (InterruptedException error) {
                interrupted = true;
            }
        }
        if (interrupted) {
            Thread.currentThread().interrupt();
        }
    }

    private QGCRCSDKManager newManager(FakeReader reader, QGCRCSDKBridge.NativeSink sink) {
        QGCRCSDKManager manager = new QGCRCSDKManager(reader, sink, 10);
        managers.add(manager);
        return manager;
    }

    private static final class FakeReader implements QGCRCSDKManager.KeyChannelsReader {
        private final AtomicInteger requestCount = new AtomicInteger();
        private final AtomicInteger connectCount = new AtomicInteger();
        private final List<QGCRCSDKManager.KeyChannelsCallback> callbacks = new ArrayList<>();
        private final List<QGCRCSDKManager.ConnectionCallback> connections = new ArrayList<>();

        @Override
        public synchronized void get(QGCRCSDKManager.KeyChannelsCallback callback) {
            callbacks.add(callback);
            requestCount.incrementAndGet();
            notifyAll();
        }

        synchronized void awaitRequest() throws InterruptedException {
            awaitRequestCount(1);
        }

        synchronized void awaitRequestCount(int count) throws InterruptedException {
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(1);
            while (requestCount.get() < count) {
                long remaining = deadline - System.nanoTime();
                if (remaining <= 0 || !waitForChange(remaining)) {
                    throw new AssertionError("poll request was not started");
                }
            }
        }

        synchronized void awaitConnectCount(int count) throws InterruptedException {
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
            while (connectCount.get() < count) {
                long remaining = deadline - System.nanoTime();
                if (remaining <= 0 || !waitForChange(remaining)) {
                    throw new AssertionError("connect retry was not started");
                }
            }
        }

        private boolean waitForChange(long remaining) throws InterruptedException {
            TimeUnit.NANOSECONDS.timedWait(this, remaining);
            return true;
        }

        synchronized QGCRCSDKManager.KeyChannelsCallback request(int index) {
            return callbacks.get(index);
        }

        synchronized QGCRCSDKManager.ConnectionCallback connection(int index) {
            return connections.get(index);
        }

        @Override
        public synchronized void connect(QGCRCSDKManager.ConnectionCallback callback) {
            connections.add(callback);
            connectCount.incrementAndGet();
            notifyAll();
        }
    }

    private static final class BlockingSink implements QGCRCSDKBridge.NativeSink {
        private final AtomicBoolean callbackFinished;
        private final CountDownLatch callbackEntered = new CountDownLatch(1);
        private final CountDownLatch releaseCallback = new CountDownLatch(1);

        BlockingSink(AtomicBoolean callbackFinished) {
            this.callbackFinished = callbackFinished;
        }

        @Override
        public void onChannels(int[] values, long generation) {
            callbackEntered.countDown();
            awaitUninterruptibly(releaseCallback);
            callbackFinished.set(true);
        }

        @Override
        public void onState(int state, String message, long generation) {
        }
    }

    private static final class ThrowingSink implements QGCRCSDKBridge.NativeSink {
        private final boolean throwFromChannels;
        private final Throwable failure;
        private final AtomicInteger channelCount = new AtomicInteger();
        private final AtomicInteger stateCount = new AtomicInteger();

        ThrowingSink(boolean throwFromChannels) {
            this(throwFromChannels, new IllegalStateException("sink callback failed"));
        }

        ThrowingSink(boolean throwFromChannels, Throwable failure) {
            this.throwFromChannels = throwFromChannels;
            this.failure = failure;
        }

        @Override
        public void onChannels(int[] values, long generation) {
            channelCount.incrementAndGet();
            if (throwFromChannels) {
                throw failure;
            }
        }

        @Override
        public void onState(int state, String message, long generation) {
            stateCount.incrementAndGet();
            if (!throwFromChannels) {
                throw failure;
            }
        }
    }

    private static final class RecordingSink implements QGCRCSDKBridge.NativeSink {
        private final CountDownLatch channelCount = new CountDownLatch(1);
        private final AtomicInteger connectedCount = new AtomicInteger();
        private final AtomicInteger disconnectedCount = new AtomicInteger();
        private final AtomicInteger errorCount = new AtomicInteger();
        private volatile int[] lastValues;
        private volatile long lastDisconnectedGeneration = Long.MIN_VALUE;
        @Override
        public void onChannels(int[] values, long generation) {
            lastValues = values;
            channelCount.countDown();
        }

        @Override
        public void onState(int state, String message, long generation) {
            if (state == QGCRCSDKBridge.STATE_CONNECTED) {
                connectedCount.incrementAndGet();
            } else if (state == QGCRCSDKBridge.STATE_DISCONNECTED) {
                disconnectedCount.incrementAndGet();
                lastDisconnectedGeneration = generation;
            } else if (state == QGCRCSDKBridge.STATE_ERROR) {
                errorCount.incrementAndGet();
            }
        }
    }
}
