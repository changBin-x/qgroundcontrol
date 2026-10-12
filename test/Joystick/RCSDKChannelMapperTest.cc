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

#ifdef QGC_UNITTEST_BUILD
UT_REGISTER_TEST_LIGHTWEIGHT(RCSDKChannelMapperTest, TestLabel::Unit, TestLabel::Joystick)
#endif
