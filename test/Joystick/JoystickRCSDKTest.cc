/**
 * @file JoystickRCSDKTest.cc
 * @brief JoystickRCSDK 后端契约与生命周期单元测试实现
 */

#include "JoystickRCSDKTest.h"
#include "JoystickRCSDK.h"

#include <QtCore/QDateTime>
#include <QtCore/QSettings>

void JoystickRCSDKTest::_deviceIdentity_test()
{
    JoystickRCSDK joystick;
    QCOMPARE(joystick.name(), QStringLiteral("Skydroid G20 RCSDK"));
    QCOMPARE(joystick.axisCount(), RCSDKChannelMapper::MaxLogicalAxes);
    QCOMPARE(joystick.buttonCount(), RCSDKChannelMapper::MaxLogicalButtons);
    QVERIFY(joystick.requiresCalibration());
    QVERIFY(!joystick.isGamepad());
    QCOMPARE(joystick.guid(), QStringLiteral("Skydroid_G20_RCSDK"));
}

void JoystickRCSDKTest::_invalidSnapshotCentersInputs_test()
{
    JoystickRCSDK joystick;
    RCSDKChannelMapper::AxisBinding axisBinding;
    axisBinding.channel = 0;
    axisBinding.minimum = 1000;
    axisBinding.center = 1500;
    axisBinding.maximum = 2000;
    joystick.setAxisBinding(0, axisBinding);

    RCSDKChannelMapper::ButtonBinding buttonBinding;
    buttonBinding.channel = 1;
    buttonBinding.pressedValue = 2000;
    buttonBinding.releasedValue = 1000;
    buttonBinding.threshold = 50;
    joystick.setButtonBinding(0, buttonBinding);

    joystick.invalidateSnapshotForTest();
    QCOMPARE(joystick.axisValueForTest(0), 0);
    QVERIFY(!joystick.buttonValueForTest(0));
}

void JoystickRCSDKTest::_snapshotDeliveryAndMapping_test()
{
    JoystickRCSDK joystick;
    RCSDKChannelMapper::AxisBinding axisBinding;
    axisBinding.channel = 0;
    axisBinding.minimum = 1000;
    axisBinding.center = 1500;
    axisBinding.maximum = 2000;
    joystick.setAxisBinding(0, axisBinding);

    RCSDKChannelMapper::ButtonBinding buttonBinding;
    buttonBinding.channel = 1;
    buttonBinding.pressedValue = 2000;
    buttonBinding.releasedValue = 1000;
    buttonBinding.threshold = 50;
    buttonBinding.debounceMs = 0; // 测试中立即生效
    joystick.setButtonBinding(0, buttonBinding);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 1. 投递第一帧快照（极小值与按下态）
    QVector<int> values = {1000, 2000};
    joystick.setSnapshotForTest(values, now, 1);
    QVERIFY(joystick.updateForTest());

    QCOMPARE(joystick.axisValueForTest(0), static_cast<int>(Joystick::AxisMin));
    QVERIFY(joystick.buttonValueForTest(0));

    // 2. 投递第二帧快照（极大值与释放态）
    values = {2000, 1000};
    joystick.setSnapshotForTest(values, now + 50, 2);
    QVERIFY(joystick.updateForTest());

    QCOMPARE(joystick.axisValueForTest(0), static_cast<int>(Joystick::AxisMax));
    QVERIFY(!joystick.buttonValueForTest(0));
}

void JoystickRCSDKTest::_qsettingsLoadSave_test()
{
    // 使用独立隔离的测试 settings key
    QSettings settings;
    settings.beginGroup(QStringLiteral("JoystickSettingsV2/Skydroid G20 RCSDK_UnitTest"));
    settings.remove(QString());
    settings.endGroup();

    JoystickRCSDK joystick;
    joystick.setSettingsGroupPrefixForTest(QStringLiteral("JoystickSettingsV2/Skydroid G20 RCSDK_UnitTest"));

    RCSDKChannelMapper::AxisBinding axis0;
    axis0.channel = 2;
    axis0.minimum = 1050;
    axis0.center = 1520;
    axis0.maximum = 1980;
    axis0.reversed = true;
    joystick.setAxisBinding(0, axis0);

    RCSDKChannelMapper::ButtonBinding btn0;
    btn0.channel = 5;
    btn0.pressedValue = 1800;
    btn0.releasedValue = 1100;
    btn0.threshold = 60;
    btn0.debounceMs = 40;
    joystick.setButtonBinding(0, btn0);

    joystick.saveSettings();

    // 创建新实例并加载
    JoystickRCSDK joystick2;
    joystick2.setSettingsGroupPrefixForTest(QStringLiteral("JoystickSettingsV2/Skydroid G20 RCSDK_UnitTest"));
    joystick2.loadSettings();

    const auto loadedAxis = joystick2.axisBinding(0);
    QCOMPARE(loadedAxis.channel, 2);
    QCOMPARE(loadedAxis.minimum, 1050);
    QCOMPARE(loadedAxis.center, 1520);
    QCOMPARE(loadedAxis.maximum, 1980);
    QCOMPARE(loadedAxis.reversed, true);

    const auto loadedBtn = joystick2.buttonBinding(0);
    QCOMPARE(loadedBtn.channel, 5);
    QCOMPARE(loadedBtn.pressedValue, 1800);
    QCOMPARE(loadedBtn.releasedValue, 1100);
    QCOMPARE(loadedBtn.threshold, 60);
    QCOMPARE(loadedBtn.debounceMs, 40);

    // 清理测试 settings
    settings.beginGroup(QStringLiteral("JoystickSettingsV2/Skydroid G20 RCSDK_UnitTest"));
    settings.remove(QString());
    settings.endGroup();
}

void JoystickRCSDKTest::_timeoutSafelyCenters_test()
{
    JoystickRCSDK joystick;
    RCSDKChannelMapper::AxisBinding axisBinding;
    axisBinding.channel = 0;
    axisBinding.minimum = 1000;
    axisBinding.center = 1500;
    axisBinding.maximum = 2000;
    joystick.setAxisBinding(0, axisBinding);

    // 模拟 10 秒前的陈旧快照
    const qint64 oldTime = QDateTime::currentMSecsSinceEpoch() - 10000;
    joystick.setSnapshotForTest({2000}, oldTime, 1);
    QVERIFY(joystick.updateForTest());

    // 查询当前时间，轴应因超时安全回零
    QCOMPARE(joystick.axisValueForTest(0), 0);
}

#ifdef QGC_UNITTEST_BUILD
UT_REGISTER_TEST(JoystickRCSDKTest, TestLabel::Unit, TestLabel::Joystick)
#endif
