/**
 * @file JoystickRCSDK.cc
 * @brief 云卓 G20 RCSDK 遥控输入后端实现文件
 * @details 继承自 Joystick 基类，将通过 AndroidRCSDK JNI 接口获取的遥控器通道快照
 *          通过 RCSDKChannelMapper 映射为标准逻辑轴和按钮，支持 QSettings 持久化与配置期原始通道分发。
 */

#include "JoystickRCSDK.h"

#include <QtCore/QDateTime>
#include <QtCore/QSettings>
#include <QtCore/QLoggingCategory>

#ifdef Q_OS_ANDROID
#include "AndroidRCSDK.h"
#endif

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(JoystickRCSDKLog, "Joystick.JoystickRCSDK")

namespace {
constexpr const char* kDefaultSettingsGroup = "JoystickSettingsV2/Skydroid G20 RCSDK";
}

JoystickRCSDK::JoystickRCSDK(QObject *parent)
    : Joystick(QStringLiteral("Skydroid G20 RCSDK"),
               RCSDKChannelMapper::MaxLogicalAxes,
               RCSDKChannelMapper::MaxLogicalButtons,
               0,
               parent)
    , _settingsGroupPrefix(QString::fromLatin1(kDefaultSettingsGroup))
{
    loadSettings();
}

JoystickRCSDK::~JoystickRCSDK()
{
    _close();
}

void JoystickRCSDK::loadSettings()
{
    QSettings settings;
    settings.sync();
    settings.beginGroup(_settingsGroupPrefix);

    // 逻辑轴配置
    for (int i = 0; i < RCSDKChannelMapper::MaxLogicalAxes; ++i) {
        settings.beginGroup(QStringLiteral("Axis_%1").arg(i));
        RCSDKChannelMapper::AxisBinding binding;
        binding.channel = settings.value(QStringLiteral("channel"), -1).toInt();
        binding.minimum = settings.value(QStringLiteral("minimum"), 1000).toInt();
        binding.center  = settings.value(QStringLiteral("center"),  1500).toInt();
        binding.maximum = settings.value(QStringLiteral("maximum"), 2000).toInt();
        binding.reversed= settings.value(QStringLiteral("reversed"), false).toBool();
        settings.endGroup();
        _mapper.setAxisBinding(i, binding);
    }

    // 逻辑按钮配置
    for (int i = 0; i < RCSDKChannelMapper::MaxLogicalButtons; ++i) {
        settings.beginGroup(QStringLiteral("Button_%1").arg(i));
        RCSDKChannelMapper::ButtonBinding binding;
        binding.channel       = settings.value(QStringLiteral("channel"), -1).toInt();
        binding.pressedValue  = settings.value(QStringLiteral("pressedValue"), 2000).toInt();
        binding.releasedValue = settings.value(QStringLiteral("releasedValue"), 1000).toInt();
        binding.threshold     = settings.value(QStringLiteral("threshold"), 50).toInt();
        binding.debounceMs    = settings.value(QStringLiteral("debounceMs"), 50).toLongLong();
        settings.endGroup();
        _mapper.setButtonBinding(i, binding);
    }

    settings.endGroup();
}

void JoystickRCSDK::saveSettings()
{
    QSettings settings;
    settings.beginGroup(_settingsGroupPrefix);

    // 逻辑轴配置
    for (int i = 0; i < RCSDKChannelMapper::MaxLogicalAxes; ++i) {
        settings.beginGroup(QStringLiteral("Axis_%1").arg(i));
        const auto binding = _mapper.axisBinding(i);
        settings.setValue(QStringLiteral("channel"), binding.channel);
        settings.setValue(QStringLiteral("minimum"), binding.minimum);
        settings.setValue(QStringLiteral("center"),  binding.center);
        settings.setValue(QStringLiteral("maximum"), binding.maximum);
        settings.setValue(QStringLiteral("reversed"), binding.reversed);
        settings.endGroup();
    }

    // 逻辑按钮配置
    for (int i = 0; i < RCSDKChannelMapper::MaxLogicalButtons; ++i) {
        settings.beginGroup(QStringLiteral("Button_%1").arg(i));
        const auto binding = _mapper.buttonBinding(i);
        settings.setValue(QStringLiteral("channel"), binding.channel);
        settings.setValue(QStringLiteral("pressedValue"), binding.pressedValue);
        settings.setValue(QStringLiteral("releasedValue"), binding.releasedValue);
        settings.setValue(QStringLiteral("threshold"), binding.threshold);
        settings.setValue(QStringLiteral("debounceMs"), binding.debounceMs);
        settings.endGroup();
    }

    settings.endGroup();
    settings.sync();
}

void JoystickRCSDK::setAxisBinding(int axis, const RCSDKChannelMapper::AxisBinding& binding)
{
    _mapper.setAxisBinding(axis, binding);
}

RCSDKChannelMapper::AxisBinding JoystickRCSDK::axisBinding(int axis) const
{
    return _mapper.axisBinding(axis);
}

void JoystickRCSDK::setButtonBinding(int button, const RCSDKChannelMapper::ButtonBinding& binding)
{
    _mapper.setButtonBinding(button, binding);
}

RCSDKChannelMapper::ButtonBinding JoystickRCSDK::buttonBinding(int button) const
{
    return _mapper.buttonBinding(button);
}

void JoystickRCSDK::invalidateSnapshotForTest()
{
    QMutexLocker locker(&_snapshotLock);
    _pendingSnapshot.valid = false;
    _hasPendingSnapshot = true;
    _mapper.invalidate();
}

void JoystickRCSDK::setSnapshotForTest(const QVector<int>& values, qint64 timestampMs, quint64 version)
{
    QMutexLocker locker(&_snapshotLock);
    _pendingSnapshot.values = values;
    _pendingSnapshot.timestampMs = timestampMs;
    _pendingSnapshot.version = version;
    _pendingSnapshot.valid = true;
    _hasPendingSnapshot = true;
}

bool JoystickRCSDK::updateForTest()
{
    return _update();
}

int JoystickRCSDK::axisValueForTest(int axis) const
{
    return _getAxisValue(axis);
}

bool JoystickRCSDK::buttonValueForTest(int button) const
{
    return _getButton(button);
}

void JoystickRCSDK::setSettingsGroupPrefixForTest(const QString& prefix)
{
    _settingsGroupPrefix = prefix;
}

bool JoystickRCSDK::_open()
{
#ifdef Q_OS_ANDROID
    AndroidRCSDK* const rcsdk = AndroidRCSDK::instance();
    if (rcsdk && rcsdk->isAvailable()) {
        connect(rcsdk, &AndroidRCSDK::snapshotReady, this, &JoystickRCSDK::_onSnapshotReady, Qt::UniqueConnection);
        if (!rcsdk->start()) {
            qCWarning(JoystickRCSDKLog) << "Failed to start AndroidRCSDK adapter";
            return false;
        }
    }
#endif

    loadSettings();
    return true;
}

void JoystickRCSDK::_close()
{
#ifdef Q_OS_ANDROID
    AndroidRCSDK* const rcsdk = AndroidRCSDK::instance();
    if (rcsdk) {
        disconnect(rcsdk, &AndroidRCSDK::snapshotReady, this, &JoystickRCSDK::_onSnapshotReady);
    }
#endif

    _mapper.invalidate();
}

bool JoystickRCSDK::_update()
{
    RCSDKChannelMapper::Snapshot localSnapshot;
    bool hasSnapshot = false;

    {
        QMutexLocker locker(&_snapshotLock);
        if (_hasPendingSnapshot) {
            localSnapshot = _pendingSnapshot;
            hasSnapshot = true;
            _hasPendingSnapshot = false;
        }
    }

    if (hasSnapshot) {
        if (localSnapshot.valid) {
            _mapper.replaceSnapshot(localSnapshot.values, localSnapshot.timestampMs, localSnapshot.version);
        } else {
            _mapper.invalidate();
        }
    }

    const QVector<int> raw = _mapper.rawValues();
    if (!raw.isEmpty()) {
        emit rawChannelValuesChanged(raw);
    }

    return true;
}

bool JoystickRCSDK::_getButton(int index) const
{
    return _mapper.buttonValue(index, QDateTime::currentMSecsSinceEpoch());
}

int JoystickRCSDK::_getAxisValue(int index) const
{
    return _mapper.axisValue(index, QDateTime::currentMSecsSinceEpoch());
}

bool JoystickRCSDK::_getHat(int /*hat*/, int /*index*/) const
{
    return false;
}

void JoystickRCSDK::_onSnapshotReady(const struct RCSDKChannelSnapshot& snapshot)
{
    QMutexLocker locker(&_snapshotLock);
    _pendingSnapshot.values = snapshot.values;
    _pendingSnapshot.timestampMs = snapshot.timestampMs;
    _pendingSnapshot.version = snapshot.generation;
    _pendingSnapshot.valid = snapshot.valid;
    _hasPendingSnapshot = true;
}
