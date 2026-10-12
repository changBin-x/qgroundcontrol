/**
 * @file RCSDKChannelMapperTest.cc
 * @brief RCSDKChannelMapper 单元测试实现文件
 */

#include "RCSDKChannelMapperTest.h"
#include "RCSDKChannelMapper.h"

void RCSDKChannelMapperTest::_axisMapping_test_data()
{
    QTest::addColumn<QVector<int>>("values");
    QTest::addColumn<int>("minimum");
    QTest::addColumn<int>("center");
    QTest::addColumn<int>("maximum");
    QTest::addColumn<int>("expected");

    QTest::newRow("center") << QVector<int>{1500} << 1050 << 1500 << 1950 << 0;
    QTest::newRow("minimum") << QVector<int>{1050} << 1050 << 1500 << 1950 << static_cast<int>(RCSDKChannelMapper::AxisMin);
    QTest::newRow("maximum") << QVector<int>{1950} << 1050 << 1500 << 1950 << static_cast<int>(RCSDKChannelMapper::AxisMax);
    QTest::newRow("negative") << QVector<int>{-100} << -100 << 0 << 100 << static_cast<int>(RCSDKChannelMapper::AxisMin);
    QTest::newRow("clamped_min") << QVector<int>{900} << 1050 << 1500 << 1950 << static_cast<int>(RCSDKChannelMapper::AxisMin);
    QTest::newRow("clamped_max") << QVector<int>{2100} << 1050 << 1500 << 1950 << static_cast<int>(RCSDKChannelMapper::AxisMax);
}

void RCSDKChannelMapperTest::_axisMapping_test()
{
    QFETCH(QVector<int>, values);
    QFETCH(int, minimum);
    QFETCH(int, center);
    QFETCH(int, maximum);
    QFETCH(int, expected);

    RCSDKChannelMapper mapper;
    RCSDKChannelMapper::AxisBinding binding;
    binding.channel = 0;
    binding.minimum = minimum;
    binding.center = center;
    binding.maximum = maximum;
    binding.reversed = false;

    mapper.setAxisBinding(0, binding);
    mapper.replaceSnapshot(values, 1000, 1);

    const int actual = mapper.axisValue(0, 1000);
    QCOMPARE(actual, expected);
}

void RCSDKChannelMapperTest::_axisBoundaryAndReversed_test()
{
    RCSDKChannelMapper mapper;

    // 1. 未配置 binding，默认返回 0
    mapper.replaceSnapshot({1500, 1600}, 1000, 1);
    QCOMPARE(mapper.axisValue(0, 1000), 0);

    // 2. minimum >= maximum 异常保护
    RCSDKChannelMapper::AxisBinding invalidBinding;
    invalidBinding.channel = 0;
    invalidBinding.minimum = 2000;
    invalidBinding.maximum = 1000;
    invalidBinding.center = 1500;
    mapper.setAxisBinding(0, invalidBinding);
    QCOMPARE(mapper.axisValue(0, 1000), 0);

    // 3. 通道越界或负数通道
    RCSDKChannelMapper::AxisBinding outOfBoundsBinding;
    outOfBoundsBinding.channel = 5;
    outOfBoundsBinding.minimum = 1000;
    outOfBoundsBinding.center = 1500;
    outOfBoundsBinding.maximum = 2000;
    mapper.setAxisBinding(0, outOfBoundsBinding);
    QCOMPARE(mapper.axisValue(0, 1000), 0);

    // 4. 逻辑轴索引越界 (< 0 或 >= MaxLogicalAxes)
    QCOMPARE(mapper.axisValue(-1, 1000), 0);
    QCOMPARE(mapper.axisValue(RCSDKChannelMapper::MaxLogicalAxes, 1000), 0);

    // 5. 反向（reversed）映射测试
    RCSDKChannelMapper::AxisBinding reversedBinding;
    reversedBinding.channel = 0;
    reversedBinding.minimum = 1000;
    reversedBinding.center = 1500;
    reversedBinding.maximum = 2000;
    reversedBinding.reversed = true;
    mapper.setAxisBinding(0, reversedBinding);

    mapper.replaceSnapshot({1000}, 1000, 2);
    QCOMPARE(mapper.axisValue(0, 1000), static_cast<int>(RCSDKChannelMapper::AxisMax));

    mapper.replaceSnapshot({2000}, 1000, 3);
    QCOMPARE(mapper.axisValue(0, 1000), static_cast<int>(RCSDKChannelMapper::AxisMin));
}

void RCSDKChannelMapperTest::_snapshotLifecycleAndTimeout_test()
{
    RCSDKChannelMapper mapper;
    RCSDKChannelMapper::AxisBinding binding;
    binding.channel = 8;
    binding.minimum = 1000;
    binding.center = 1500;
    binding.maximum = 2000;
    mapper.setAxisBinding(0, binding);

    // 1. 空数组快照 -> 轴值回零
    mapper.replaceSnapshot({}, 1000, 1);
    QCOMPARE(mapper.axisValue(0, 1000), 0);

    // 2. 正常 16 通道数组
    QVector<int> values16(16, 1500);
    values16[8] = 2000;
    mapper.replaceSnapshot(values16, 1000, 2);
    QCOMPARE(mapper.axisValue(0, 1000), static_cast<int>(RCSDKChannelMapper::AxisMax));

    // 3. 数组动态变短为 4 通道（channel 8 丢失） -> 轴值回零
    QVector<int> values4(4, 1500);
    mapper.replaceSnapshot(values4, 1050, 3);
    QCOMPARE(mapper.axisValue(0, 1050), 0);

    // 4. 数组重新变长并更新
    values16[8] = 1000;
    mapper.replaceSnapshot(values16, 1100, 4);
    QCOMPARE(mapper.axisValue(0, 1100), static_cast<int>(RCSDKChannelMapper::AxisMin));

    // 5. 快照失效 (invalidate) -> 轴值回零
    mapper.invalidate();
    QCOMPARE(mapper.axisValue(0, 1100), 0);

    // 6. 快照超时 (nowMs - timestampMs > timeoutMs) -> 轴值回零
    mapper.replaceSnapshot(values16, 1000, 5);
    QCOMPARE(mapper.axisValue(0, 1000), static_cast<int>(RCSDKChannelMapper::AxisMin));
    QCOMPARE(mapper.axisValue(0, 1000 + RCSDKChannelMapper::DefaultSnapshotTimeoutMs), static_cast<int>(RCSDKChannelMapper::AxisMin));
    QCOMPARE(mapper.axisValue(0, 1000 + RCSDKChannelMapper::DefaultSnapshotTimeoutMs + 1), 0);
}

void RCSDKChannelMapperTest::_buttonDebounceAndFailure_test()
{
    RCSDKChannelMapper mapper;
    RCSDKChannelMapper::ButtonBinding binding;
    binding.channel = 2;
    binding.pressedValue = 2000;
    binding.releasedValue = 1000;
    binding.threshold = 50;
    binding.debounceMs = 50;
    mapper.setButtonBinding(0, binding);

    // 初始状态：未提供快照，按钮应为 false
    QVERIFY(!mapper.buttonValue(0, 1000));

    // 1. 提供释放态快照
    mapper.replaceSnapshot({1500, 1500, 1000}, 1000, 1);
    QVERIFY(!mapper.buttonValue(0, 1000));

    // 2. 采样到按下值（2000），但时间仅经过 20ms（< 50ms 去抖窗口）-> 保持 false
    mapper.replaceSnapshot({1500, 1500, 2010}, 1020, 2);
    QVERIFY(!mapper.buttonValue(0, 1020));

    // 3. 采样值持续保持到 1060ms（距首次 1020ms 已过 40ms，还未达 50ms）-> 保持 false
    mapper.replaceSnapshot({1500, 1500, 1995}, 1060, 3);
    QVERIFY(!mapper.buttonValue(0, 1060));

    // 4. 采样值到达 1075ms（距 1020ms 已达 55ms >= 50ms 去抖窗口）-> 触发按下 true
    mapper.replaceSnapshot({1500, 1500, 2000}, 1075, 4);
    QVERIFY(mapper.buttonValue(0, 1075));

    // 5. 瞬间出现短时毛刺抖动为释放态（持续 20ms）-> 仍然保持按下 true
    mapper.replaceSnapshot({1500, 1500, 1000}, 1080, 5);
    QVERIFY(mapper.buttonValue(0, 1080));
    mapper.replaceSnapshot({1500, 1500, 2000}, 1095, 6);
    QVERIFY(mapper.buttonValue(0, 1095));

    // 6. 持续释放超过 50ms -> 切换为 false
    mapper.replaceSnapshot({1500, 1500, 1000}, 1100, 7);
    QVERIFY(mapper.buttonValue(0, 1100)); // 刚释放，未达去抖
    mapper.replaceSnapshot({1500, 1500, 1000}, 1160, 8);
    QVERIFY(!mapper.buttonValue(0, 1160)); // 经过 60ms，确认释放

    // 7. 数组截断（channel 2 不存在）或失效 -> 立即回退 false
    mapper.replaceSnapshot({1500, 1500, 2000}, 1200, 9);
    mapper.replaceSnapshot({1500, 1500, 2000}, 1260, 10);
    QVERIFY(mapper.buttonValue(0, 1260)); // 已按下

    mapper.replaceSnapshot({1500}, 1270, 11);
    QVERIFY(!mapper.buttonValue(0, 1270)); // 长度变短，直接安全失效
}

void RCSDKChannelMapperTest::_configurationRawValuesFixture_test()
{
    RCSDKChannelMapper mapper;
    const qint64 now = 1000;
    const QVector<int> sample{
        1500, 1500, 1500, 1500, 1950, 1050, 1050, 1950,
        1050, 1050, 1050, 1050, 1050, 1500, 1050, 1050,
    };
    mapper.replaceSnapshot(sample, now, 1);
    QCOMPARE(mapper.rawValues(), sample);
    QCOMPARE(mapper.rawValues().size(), 16);

    // 验证动态选择 channel 4（1950）绑定到逻辑轴 0
    RCSDKChannelMapper::AxisBinding axis0;
    axis0.channel = 4;
    axis0.minimum = 1050;
    axis0.center = 1500;
    axis0.maximum = 1950;
    mapper.setAxisBinding(0, axis0);
    QCOMPARE(mapper.axisValue(0, now), static_cast<int>(RCSDKChannelMapper::AxisMax));
}

void RCSDKChannelMapperTest::_multiPositionSwitchAndPulse_test()
{
    RCSDKChannelMapper mapper;
    qint64 t = 1000;

    // 多段开关：例如 3 段开关在通道 6，位置 1 = 1000, 位置 2 = 1500, 位置 3 = 2000
    // 分别映射为 3 个逻辑按钮（Button 0, Button 1, Button 2）
    RCSDKChannelMapper::ButtonBinding pos1{6, 1000, 0, 50, 30};
    RCSDKChannelMapper::ButtonBinding pos2{6, 1500, 0, 50, 30};
    RCSDKChannelMapper::ButtonBinding pos3{6, 2000, 0, 50, 30};
    mapper.setButtonBinding(0, pos1);
    mapper.setButtonBinding(1, pos2);
    mapper.setButtonBinding(2, pos3);

    // 位置 1 (1000)
    mapper.replaceSnapshot({1500, 1500, 1500, 1500, 1500, 1500, 1000}, t, 1);
    t += 50;
    mapper.replaceSnapshot({1500, 1500, 1500, 1500, 1500, 1500, 1000}, t, 2);
    QVERIFY(mapper.buttonValue(0, t));
    QVERIFY(!mapper.buttonValue(1, t));
    QVERIFY(!mapper.buttonValue(2, t));

    // 切换到位置 2 (1500)
    t += 10;
    mapper.replaceSnapshot({1500, 1500, 1500, 1500, 1500, 1500, 1500}, t, 3);
    t += 50;
    mapper.replaceSnapshot({1500, 1500, 1500, 1500, 1500, 1500, 1500}, t, 4);
    QVERIFY(!mapper.buttonValue(0, t));
    QVERIFY(mapper.buttonValue(1, t));
    QVERIFY(!mapper.buttonValue(2, t));

    // 切换到位置 3 (2000)
    t += 10;
    mapper.replaceSnapshot({1500, 1500, 1500, 1500, 1500, 1500, 2000}, t, 5);
    t += 50;
    mapper.replaceSnapshot({1500, 1500, 1500, 1500, 1500, 1500, 2000}, t, 6);
    QVERIFY(!mapper.buttonValue(0, t));
    QVERIFY(!mapper.buttonValue(1, t));
    QVERIFY(mapper.buttonValue(2, t));

    // 脉冲按钮去抖测试（channel 7 脉冲按键，平时 1000，瞬时按下 2000，回弹 1000）
    RCSDKChannelMapper::ButtonBinding pulseBtn{7, 2000, 1000, 50, 40};
    mapper.setButtonBinding(3, pulseBtn);

    // 脉冲未达去抖时间（持续 20ms < 40ms），不误触发
    t += 10;
    mapper.replaceSnapshot({1500, 1500, 1500, 1500, 1500, 1500, 2000, 2000}, t, 7);
    t += 20;
    mapper.replaceSnapshot({1500, 1500, 1500, 1500, 1500, 1500, 2000, 1000}, t, 8);
    QVERIFY(!mapper.buttonValue(3, t));

    // 脉冲持续有效（持续 50ms >= 40ms），确认触发
    t += 10;
    mapper.replaceSnapshot({1500, 1500, 1500, 1500, 1500, 1500, 2000, 2000}, t, 9);
    t += 50;
    mapper.replaceSnapshot({1500, 1500, 1500, 1500, 1500, 1500, 2000, 2000}, t, 10);
    QVERIFY(mapper.buttonValue(3, t));
}

#ifdef QGC_UNITTEST_BUILD
UT_REGISTER_TEST_LIGHTWEIGHT(RCSDKChannelMapperTest, TestLabel::Unit, TestLabel::Joystick)
#endif
