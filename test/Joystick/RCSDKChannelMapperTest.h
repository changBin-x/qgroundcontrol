/**
 * @file RCSDKChannelMapperTest.h
 * @brief RCSDKChannelMapper 映射器单元测试
 */

#pragma once

#ifdef QGC_UNITTEST_BUILD
#include "UnitTest.h"
class RCSDKChannelMapperTest : public UnitTest
#else
#include <QtCore/QObject>
#include <QtTest/QTest>
class RCSDKChannelMapperTest : public QObject
#endif
{
    Q_OBJECT

private slots:
    void _axisMapping_test();
    void _axisMapping_test_data();
    void _axisBoundaryAndReversed_test();
    void _snapshotLifecycleAndTimeout_test();
    void _buttonDebounceAndFailure_test();
    void _configurationRawValuesFixture_test();
    void _multiPositionSwitchAndPulse_test();
};
