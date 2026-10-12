/**
 * @file AndroidRCSDK.cc
 * @brief Android RCSDK JNI 适配层实现文件
 * @details 实现了 QGCRCSDKBridge 的 JNI 回调处理函数（jniNativeChannels、jniNativeState、jniNativeInvalidateCallbacks），
 *          提供线程安全的生命周期序列管理、代数验证以及 Qt 事件驱动的快照投递。
 */

#include "AndroidRCSDK.h"

#include <jni.h>

#include <atomic>
#include <iterator>
#include <limits>
#include <utility>

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QJniEnvironment>
#include <QtCore/QJniObject>
#include <QtCore/QMetaObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QThread>

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(AndroidRCSDKLog, "Android.AndroidRCSDK")

namespace {

constexpr const char* kBridgeClassName = "org/mavlink/qgroundcontrol/QGCRCSDKBridge";
constexpr const char* kManagerClassName = "org/mavlink/qgroundcontrol/QGCRCSDKManager";
constexpr const int kMaximumChannelCount = 64;

void jniNativeChannels(JNIEnv* env, jobject, jintArray values, jlong generation)
{
    if (!env || !values || generation <= 0) {
        if (env) {
            (void)QJniEnvironment::checkAndClearExceptions(env);
        }
        return;
    }

    const jsize length = env->GetArrayLength(values);
    if (QJniEnvironment::checkAndClearExceptions(env) || length <= 0 || length > kMaximumChannelCount) {
        (void)QJniEnvironment::checkAndClearExceptions(env);
        return;
    }

    jint* const elements = env->GetIntArrayElements(values, nullptr);
    if (!elements) {
        (void)QJniEnvironment::checkAndClearExceptions(env);
        return;
    }

    QVector<int> copiedValues;
    copiedValues.reserve(length);
    for (jsize index = 0; index < length; ++index) {
        copiedValues.append(static_cast<int>(elements[index]));
    }
    env->ReleaseIntArrayElements(values, elements, JNI_ABORT);
    if (QJniEnvironment::checkAndClearExceptions(env)) {
        return;
    }

    AndroidRCSDK* const adapter = AndroidRCSDK::instance();
    if (!adapter) {
        (void)QJniEnvironment::checkAndClearExceptions(env);
        return;
    }

    const quint64 copiedGeneration = static_cast<quint64>(generation);
    const quint64 callbackEpoch = adapter->registerNativeGeneration(copiedGeneration);
    (void)QMetaObject::invokeMethod(
        adapter,
        [adapter, values = std::move(copiedValues), copiedGeneration, callbackEpoch]() {
            if (!adapter->isNativeGenerationCurrent(copiedGeneration, callbackEpoch)) {
                return;
            }

            RCSDKChannelSnapshot snapshot;
            snapshot.values = values;
            snapshot.timestampMs = QDateTime::currentMSecsSinceEpoch();
            snapshot.generation = copiedGeneration;
            snapshot.valid = !snapshot.values.isEmpty();
            if (snapshot.valid) {
                emit adapter->snapshotReady(snapshot);
            }
        },
        Qt::QueuedConnection);
}

void jniNativeState(JNIEnv* env, jobject, jint state, jstring message, jlong generation)
{
    if (!env || generation <= 0) {
        if (env) {
            (void)QJniEnvironment::checkAndClearExceptions(env);
        }
        return;
    }

    QString stateString;
    if (message) {
        stateString = QJniObject(message).toString();
    }
    if (QJniEnvironment::checkAndClearExceptions(env)) {
        return;
    }

    QString stateName;
    switch (state) {
    case 1:
        stateName = QStringLiteral("connected");
        break;
    case 2:
        stateName = QStringLiteral("disconnected");
        break;
    case 3:
        stateName = QStringLiteral("error");
        break;
    default:
        stateName = QStringLiteral("unknown");
        break;
    }
    if (!stateString.isEmpty()) {
        stateName += QStringLiteral(": ") + stateString;
    }

    AndroidRCSDK* const adapter = AndroidRCSDK::instance();
    if (!adapter) {
        (void)QJniEnvironment::checkAndClearExceptions(env);
        return;
    }

    const quint64 copiedGeneration = static_cast<quint64>(generation);
    const quint64 callbackEpoch = adapter->registerNativeGeneration(copiedGeneration);
    (void)QMetaObject::invokeMethod(
        adapter,
        [adapter, stateName = std::move(stateName), copiedGeneration, callbackEpoch]() {
            if (!adapter->isNativeGenerationCurrent(copiedGeneration, callbackEpoch)) {
                return;
            }

            emit adapter->connectionStateChanged(stateName);
        },
        Qt::QueuedConnection);
}

void jniNativeInvalidateCallbacks(JNIEnv* env, jobject)
{
    AndroidRCSDK* const adapter = AndroidRCSDK::instance();
    if (adapter) {
        adapter->invalidateNativeCallbacks();
    }
    if (env) {
        (void)QJniEnvironment::checkAndClearExceptions(env);
    }
}

}  // namespace

AndroidRCSDK::AndroidRCSDK(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<RCSDKChannelSnapshot>();
}

AndroidRCSDK* AndroidRCSDK::instance()
{
    static std::atomic<AndroidRCSDK*> adapter{nullptr};
    if (AndroidRCSDK* const existingAdapter = adapter.load(std::memory_order_acquire)) {
        return existingAdapter;
    }

    QCoreApplication* const application = QCoreApplication::instance();
    if (!application || QThread::currentThread() != application->thread()) {
        return nullptr;
    }

    AndroidRCSDK* const createdAdapter = new AndroidRCSDK(application);
    AndroidRCSDK* expectedAdapter = nullptr;
    if (adapter.compare_exchange_strong(
            expectedAdapter, createdAdapter, std::memory_order_release, std::memory_order_acquire)) {
        return createdAdapter;
    }

    delete createdAdapter;
    return expectedAdapter;
}

bool AndroidRCSDK::start()
{
    if (QThread::currentThread() != thread()) {
        return false;
    }

    invalidateNativeCallbacks();
    if (!isAvailable()) {
        return false;
    }

    QJniObject::callStaticMethod<void>(kManagerClassName, "resume", "()V");
    QJniEnvironment env;
    if (env.checkAndClearExceptions()) {
        qCWarning(AndroidRCSDKLog) << "Unable to start the RCSDK manager";
        return false;
    }
    return true;
}

void AndroidRCSDK::stop()
{
    if (QThread::currentThread() != thread()) {
        return;
    }

    invalidateNativeCallbacks();
    if (!isAvailable()) {
        return;
    }

    QJniObject::callStaticMethod<void>(kManagerClassName, "shutdownInstance", "()V");
    QJniEnvironment env;
    if (env.checkAndClearExceptions()) {
        qCWarning(AndroidRCSDKLog) << "Unable to stop the RCSDK manager";
    }
}

quint64 AndroidRCSDK::registerNativeGeneration(quint64 generation)
{
    if (generation == 0) {
        return 0;
    }

    const quint64 sequence = _lifecycleSequence.load(std::memory_order_acquire);
    if ((sequence & 1U) != 0U) {
        return 0;
    }

    const quint64 callbackEpoch = _callbackEpoch.load(std::memory_order_relaxed);
    quint64 latestGeneration = _latestGeneration.load(std::memory_order_relaxed);
    if (latestGeneration < generation
        && !_latestGeneration.compare_exchange_strong(
            latestGeneration, generation, std::memory_order_relaxed, std::memory_order_relaxed)) {
        return 0;
    }

    // The sequence acquire/recheck pairs the epoch and generation snapshot with invalidateNativeCallbacks().
    const quint64 confirmedSequence = _lifecycleSequence.load(std::memory_order_acquire);
    if (sequence != confirmedSequence || (confirmedSequence & 1U) != 0U) {
        return 0;
    }

    return callbackEpoch;
}

bool AndroidRCSDK::isNativeGenerationCurrent(quint64 generation, quint64 callbackEpoch) const
{
    if (generation == 0 || callbackEpoch == 0) {
        return false;
    }

    const quint64 sequence = _lifecycleSequence.load(std::memory_order_acquire);
    if ((sequence & 1U) != 0U) {
        return false;
    }

    const quint64 currentEpoch = _callbackEpoch.load(std::memory_order_relaxed);
    const quint64 latestGeneration = _latestGeneration.load(std::memory_order_relaxed);
    const quint64 confirmedSequence = _lifecycleSequence.load(std::memory_order_acquire);
    if (sequence != confirmedSequence || (confirmedSequence & 1U) != 0U) {
        return false;
    }

    return callbackEpoch == currentEpoch && generation >= latestGeneration;
}

void AndroidRCSDK::invalidateNativeCallbacks()
{
    quint64 sequence = _lifecycleSequence.load(std::memory_order_acquire);
    while ((sequence & 1U) != 0U
           || !_lifecycleSequence.compare_exchange_weak(
               sequence, sequence + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
    }

    const quint64 currentEpoch = _callbackEpoch.load(std::memory_order_relaxed);
    constexpr quint64 kMaximumValue = std::numeric_limits<quint64>::max();
    _callbackEpoch.store(currentEpoch == kMaximumValue ? 1 : currentEpoch + 1, std::memory_order_relaxed);
    _latestGeneration.store(0, std::memory_order_relaxed);

    // A stable sequence is always even. Explicitly wrap at the last even value instead of relying on
    // unsigned arithmetic so the odd writer marker can never be skipped by an overflow.
    const quint64 nextSequence = sequence == kMaximumValue - 1 ? 0 : sequence + 2;
    _lifecycleSequence.store(nextSequence, std::memory_order_release);
}

bool AndroidRCSDK::isAvailable() const
{
    return QJniObject::isClassAvailable(kBridgeClassName) && QJniObject::isClassAvailable(kManagerClassName);
}

namespace AndroidRCSDKRegistration {

[[nodiscard]] bool setNativeMethods()
{
    const JNINativeMethod javaMethods[] {
        {"nativeChannels", "([IJ)V", reinterpret_cast<void*>(jniNativeChannels)},
        {"nativeState", "(ILjava/lang/String;J)V", reinterpret_cast<void*>(jniNativeState)},
        {"nativeInvalidateCallbacks", "()V", reinterpret_cast<void*>(jniNativeInvalidateCallbacks)},
    };

    QJniEnvironment env;
    const bool registered = env.registerNativeMethods(kBridgeClassName, javaMethods, std::size(javaMethods));
    const bool hadException = env.checkAndClearExceptions();
    if (!registered || hadException) {
        qCWarning(AndroidRCSDKLog) << "Failed to register RCSDK native methods"
                                    << "registered:" << registered
                                    << "jniException:" << hadException;
        return false;
    }

    return true;
}

}  // namespace AndroidRCSDKRegistration
