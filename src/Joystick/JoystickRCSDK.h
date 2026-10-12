/**
 * @file JoystickRCSDK.h
 * @brief 云卓 G20 RCSDK 遥控输入后端
 * @details 继承自 QGroundControl 的 Joystick 抽象基类，将通过 AndroidRCSDK JNI 接口
 *          获取的遥控器通道快照接入 QGC 统一的摇杆/按键抽象。
 */

#pragma once

#include <QtCore/QMutex>
#include <QtCore/QPointer>
#include <QtCore/QString>

#include "Joystick.h"
#include "RCSDKChannelMapper.h"

#ifdef Q_OS_ANDROID
#include "AndroidRCSDK.h"
#else
struct RCSDKChannelSnapshot
{
    QVector<int> values;
    qint64 timestampMs = 0;
    quint64 generation = 0;
    bool valid = false;
};
#endif

class JoystickRCSDK : public Joystick
{
    Q_OBJECT

#ifdef QGC_UNITTEST_BUILD
    friend class JoystickRCSDKTest;
#endif

public:
    explicit JoystickRCSDK(QObject *parent = nullptr);
    ~JoystickRCSDK() override;

    // 设备身份相关属性重写
    bool requiresCalibration() const override { return true; }
    bool isGamepad() const override { return false; }
    QString guid() const override { return QStringLiteral("Skydroid_G20_RCSDK"); }
    quint16 vendorId() const override { return 0x2026; }
    quint16 productId() const override { return 0x0020; }
    QString deviceType() const override { return QStringLiteral("Skydroid G20"); }

    // 设置存储与加载
    void loadSettings();
    void saveSettings();

    // 通道映射绑定配置
    void setAxisBinding(int axis, const RCSDKChannelMapper::AxisBinding& binding);
    RCSDKChannelMapper::AxisBinding axisBinding(int axis) const;

    void setButtonBinding(int button, const RCSDKChannelMapper::ButtonBinding& binding);
    RCSDKChannelMapper::ButtonBinding buttonBinding(int button) const;

    // 测试专用安全通道桩
    void invalidateSnapshotForTest();
    void setSnapshotForTest(const QVector<int>& values, qint64 timestampMs, quint64 version);
    bool updateForTest();
    int axisValueForTest(int axis) const;
    bool buttonValueForTest(int button) const;
    void setSettingsGroupPrefixForTest(const QString& prefix);

protected:
    bool _open() override;
    void _close() override;
    bool _update() override;

    bool _getButton(int index) const override;
    int _getAxisValue(int index) const override;
    bool _getHat(int hat, int index) const override;

private slots:
    void _onSnapshotReady(const struct RCSDKChannelSnapshot& snapshot);

private:
    mutable QMutex _snapshotLock;
    RCSDKChannelMapper::Snapshot _pendingSnapshot;
    bool _hasPendingSnapshot = false;

    RCSDKChannelMapper _mapper;
    QString _settingsGroupPrefix;
};
