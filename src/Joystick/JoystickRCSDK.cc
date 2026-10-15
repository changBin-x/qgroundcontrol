/**
 * @file JoystickRCSDK.cc
 * @brief 云卓 G20 RCSDK 遥控输入后端实现文件
 * @details 继承自 Joystick 基类，将通过 AndroidRCSDK JNI 接口获取的遥控器通道快照
 *          通过 RCSDKChannelMapper 映射为标准逻辑轴和按钮，支持 QSettings 持久化与配置期原始通道分发。
 */

#include "JoystickRCSDK.h"

#include <QtCore/QDateTime>
#include <QtCore/QLoggingCategory>
#include <QtCore/QSettings>

#ifdef Q_OS_ANDROID
#include "AndroidRCSDK.h"
#endif

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(JoystickRCSDKLog, "Joystick.JoystickRCSDK")

namespace {
constexpr const char* kDefaultSettingsGroup = "JoystickSettingsV2/Skydroid G20 RCSDK";
constexpr int kDefaultPwmMinimum = 1000;
constexpr int kDefaultPwmCenter = 1500;
constexpr int kDefaultPwmMaximum = 2000;
constexpr int kDefaultButtonPressedPwm = 1950;
constexpr int kDefaultButtonReleasedPwm = 1050;
constexpr int kDefaultButtonThreshold = 200;
constexpr int kDefaultButtonBaseChannel = 4;
}

JoystickRCSDK::JoystickRCSDK(QObject *parent)
    : Joystick(QStringLiteral("Skydroid G20 RCSDK"),
               RCSDKChannelMapper::MaxLogicalAxes,
               RCSDKChannelMapper::MaxLogicalButtons,
               0,
               parent)
    , _settingsGroupPrefix(QString::fromLatin1(kDefaultSettingsGroup))
{
    _mapper.replaceSnapshot(QVector<int>(16, 1500), QDateTime::currentMSecsSinceEpoch(), 0);
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

    // 逻辑轴配置。未写入 settings 时 channel 默认为 -1，会导致 _getAxisValue 恒为 0，
    // Joystick 配置页指示器完全不动。缺省改为 axis i → RCSDK channel i 透传，交给 QGC 校准。
    for (int i = 0; i < RCSDKChannelMapper::MaxLogicalAxes; ++i) {
        settings.beginGroup(QStringLiteral("Axis_%1").arg(i));
        RCSDKChannelMapper::AxisBinding binding;
        binding.channel = settings.value(QStringLiteral("channel"), -1).toInt();
        binding.minimum = settings.value(QStringLiteral("minimum"), kDefaultPwmMinimum).toInt();
        binding.center  = settings.value(QStringLiteral("center"),  kDefaultPwmCenter).toInt();
        binding.maximum = settings.value(QStringLiteral("maximum"), kDefaultPwmMaximum).toInt();
        binding.reversed= settings.value(QStringLiteral("reversed"), false).toBool();
        settings.endGroup();
        if (binding.channel < 0) {
            binding.channel = i;
            binding.minimum = kDefaultPwmMinimum;
            binding.center = kDefaultPwmCenter;
            binding.maximum = kDefaultPwmMaximum;
            binding.reversed = false;
        }
        _mapper.setAxisBinding(i, binding);
    }

    // 逻辑按钮配置。G20 KeyChannels 样本中 CH0-3 为摇杆中位 1500，CH4+ 多为 1050/1950 开关量。
    // 缺省从 channel 4 起映射按钮，避免与摇杆轴抢同一通道的去抖状态。
    for (int i = 0; i < RCSDKChannelMapper::MaxLogicalButtons; ++i) {
        settings.beginGroup(QStringLiteral("Button_%1").arg(i));
        RCSDKChannelMapper::ButtonBinding binding;
        binding.channel       = settings.value(QStringLiteral("channel"), -1).toInt();
        binding.pressedValue  = settings.value(QStringLiteral("pressedValue"), kDefaultButtonPressedPwm).toInt();
        binding.releasedValue = settings.value(QStringLiteral("releasedValue"), kDefaultButtonReleasedPwm).toInt();
        binding.threshold     = settings.value(QStringLiteral("threshold"), kDefaultButtonThreshold).toInt();
        binding.debounceMs    = settings.value(QStringLiteral("debounceMs"), 50).toLongLong();
        settings.endGroup();
        if (binding.channel < 0) {
            const int defaultChannel = kDefaultButtonBaseChannel + i;
            if (defaultChannel < RCSDKChannelMapper::MaxChannels) {
                binding.channel = defaultChannel;
                binding.pressedValue = kDefaultButtonPressedPwm;
                binding.releasedValue = kDefaultButtonReleasedPwm;
                binding.threshold = kDefaultButtonThreshold;
                binding.debounceMs = 50;
            }
        }
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
    qCDebug(JoystickRCSDKLog) << "JoystickRCSDK::_open() called";
#ifdef Q_OS_ANDROID
    AndroidRCSDK* const rcsdk = AndroidRCSDK::instance();
    if (rcsdk && rcsdk->isAvailable()) {
        connect(rcsdk, &AndroidRCSDK::snapshotReady, this, &JoystickRCSDK::_onSnapshotReady, Qt::UniqueConnection);
        if (!rcsdk->start()) {
            // 启动失败不应让整个摇杆失效：AndroidRCSDK::start() 内部会自愈（startInstance 在实例缺失时
            // 重新 initialize）。保留轮询线程运行，等真实快照到达后再替换占位快照，避免出现“0 轴”死锁。
            qCWarning(JoystickRCSDKLog) << "AndroidRCSDK adapter start failed, keeping polling thread alive";
        }
    }
#endif

    _mapper.replaceSnapshot(QVector<int>(16, 1500), QDateTime::currentMSecsSinceEpoch(), 0);
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

    // 不在此处 emit rawChannelValuesChanged：配置页由 Joystick::_handleAxis() 用 _getAxisValue()
    // 统一发出。若这里再发原始 PWM 数组，会被随后的轴值覆盖，且长度/量纲与 Joystick 配置 UI 不一致。

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
