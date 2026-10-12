/**
 * @file AndroidRCSDK.h
 * @brief Android RCSDK JNI 适配层头文件
 * @details 将云卓 G20 RCSDK 在 Android Java 层轮询得到的遥控通道数组与状态通过 JNI 跨线程桥接至 Qt 事件循环，
 *          对外提供不可变快照及生命周期管理接口，并具备排队回调自动失效机制。
 */

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QtGlobal>
#include <QtCore/QVector>

#include <atomic>

/// 遥控器通道不可变快照结构体
struct RCSDKChannelSnapshot
{
    QVector<int> values;       ///< 各物理通道的原始 PWM 采集值数组
    qint64 timestampMs = 0;    ///< 快照在 JNI 接收层生成时的时间戳（毫秒）
    quint64 generation = 0;    ///< 对应 Java 轮询管理器的生命周期版本号
    bool valid = false;        ///< 快照有效性标志（数组非空且未超限）
};

Q_DECLARE_METATYPE(RCSDKChannelSnapshot)

/**
 * @class AndroidRCSDK
 * @brief Qt 端 Android RCSDK 单例适配器
 * @details 负责处理从 Java QGCRCSDKBridge 发起的 JNI 回调，通过原子代数校验过滤过期旧数据，
 *          并以 Qt::QueuedConnection 安全将数据派发到 Qt 主事件循环中。
 */
class AndroidRCSDK : public QObject
{
    Q_OBJECT

public:
    /// 获取进程内唯一的 Android RCSDK 适配器单例指针
    static AndroidRCSDK* instance();

    /// 启动 Java 层的 RCSDK 轮询管理器
    bool start();

    /// 停止 Java 层的 RCSDK 轮询管理器并立即使排队回调失效
    void stop();

    /// 使之前生命周期内排队在 Qt 事件循环中的旧回调失效
    void invalidateNativeCallbacks();

    /// 检查 Java 端 RCSDK Bridge 和 Manager 类是否可用
    bool isAvailable() const;

    /// 登记当前接收到的 native 回调代数，并返回匹配的生命周期纪元（epoch）
    quint64 registerNativeGeneration(quint64 generation);

    /// 校验排队执行的 lambda 是否仍属于当前生命周期
    bool isNativeGenerationCurrent(quint64 generation, quint64 callbackEpoch) const;

signals:
    /// 当收到有效、不可变的新通道快照时在 Qt 线程中触发
    void snapshotReady(const RCSDKChannelSnapshot& snapshot);

    /// 当 Java 端报告 RCSDK 状态变更时在 Qt 线程中触发
    void connectionStateChanged(const QString& state);

private:
    explicit AndroidRCSDK(QObject* parent = nullptr);

    // 奇数表示正在更新失效生命周期，偶数表示状态稳定
    std::atomic<quint64> _lifecycleSequence{0};
    std::atomic<quint64> _latestGeneration{0};
    // 纪元 0 保留，有效纪元从 1 开始递增
    std::atomic<quint64> _callbackEpoch{1};
};

namespace AndroidRCSDKRegistration {
/// 在 JNI_OnLoad 期间注册 Java 到 C++ 的 native 方法映射
[[nodiscard]] bool setNativeMethods();
}
