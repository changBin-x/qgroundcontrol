/**
 * @file JoystickRCSDKTest.h
 * @brief JoystickRCSDK 后端契约与生命周期单元测试
 */

#pragma once

#ifdef QGC_UNITTEST_BUILD
#include "UnitTest.h"
class JoystickRCSDKTest : public UnitTest
#else
#include <QtCore/QObject>
#include <QtTest/QTest>
class JoystickRCSDKTest : public QObject
#endif
{
    Q_OBJECT

private slots:
    void _invalidSnapshotCentersInputs_test();
    void _deviceIdentity_test();
    void _snapshotDeliveryAndMapping_test();
    void _qsettingsLoadSave_test();
    void _timeoutSafelyCenters_test();
};
