/**
 * @file RCSDKChannelMapper.h
 * @brief 云卓 G20 RCSDK 遥控通道映射器
 * @details 负责将 Android RCSDK 采集到的原始通道 PWM 数组（如 1000~2000）映射到 QGC 抽象的逻辑轴与逻辑按钮。
 *          包含轴的区间归一化（映射至 [-32768, 32767]）、中位回零、死区与反向配置，
 *          以及按钮状态机去抖与快照过期（超时）安全回零防护。
 */

#pragma once

#include <QtCore/QVector>
#include <QtCore/QtGlobal>

#include <array>

class RCSDKChannelMapper
{
public:
    /// 支持的最大通道数量
    static constexpr int MaxChannels = 64;
    /// 支持的最大逻辑轴数量
    static constexpr int MaxLogicalAxes = 6;
    /// 支持的最大逻辑按钮数量
    static constexpr int MaxLogicalButtons = 32;
    /// 逻辑轴输出最小量程（对应 Joystick::AxisMin）
    static constexpr int AxisMin = -32768;
    /// 逻辑轴输出最大量程（对应 Joystick::AxisMax）
    static constexpr int AxisMax = 32767;
    /// 默认快照超时时间（毫秒，超过此时间快照视为失效回零）
    static constexpr qint64 DefaultSnapshotTimeoutMs = 500;

    /// 遥控通道快照结构
    struct Snapshot
    {
        QVector<int> values;       ///< 各通道原始数值
        qint64 timestampMs = 0;    ///< 快照时间戳（毫秒）
        quint64 version = 0;       ///< 快照单调版本号
        bool valid = false;        ///< 快照有效性标志
    };

    /// 逻辑轴绑定配置
    struct AxisBinding
    {
        int channel = -1;          ///< 绑定的通道索引（0-based，-1 表示未绑定）
        int minimum = 0;           ///< 该通道对应的最小原始值
        int center = 0;            ///< 该通道对应的中位原始值
        int maximum = 0;           ///< 该通道对应的最大原始值
        bool reversed = false;     ///< 是否反向输出
    };

    /// 逻辑按钮绑定配置
    struct ButtonBinding
    {
        int channel = -1;          ///< 绑定的通道索引（0-based，-1 表示未绑定）
        int pressedValue = 0;      ///< 按下时的目标通道值
        int releasedValue = 0;     ///< 释放时的目标通道值
        int threshold = 0;         ///< 匹配目标值的容差阈值
        qint64 debounceMs = 50;    ///< 状态切换去抖窗口（毫秒）
    };

    RCSDKChannelMapper();

    /// 用新的通道数组和时间戳更新快照
    void replaceSnapshot(const QVector<int>& values, qint64 timestampMs, quint64 version);

    /// 明确使当前快照与按钮候选状态失效
    void invalidate();

    /// 获取指定逻辑轴当前的归一化值（范围：[-32768, 32767]，失效或未绑定返回 0）
    int axisValue(int logicalAxis, qint64 nowMs) const;

    /// 获取指定逻辑按钮当前的去抖逻辑状态（true 为按下，false 为释放）
    bool buttonValue(int logicalButton, qint64 nowMs) const;

    /// 获取当前快照的原始通道数据备份
    QVector<int> rawValues() const;

    /// 配置指定逻辑轴的绑定参数
    void setAxisBinding(int logicalAxis, const AxisBinding& binding);

    /// 获取指定逻辑轴的绑定参数
    AxisBinding axisBinding(int logicalAxis) const;

    /// 配置指定逻辑按钮的绑定参数
    void setButtonBinding(int logicalButton, const ButtonBinding& binding);

    /// 获取指定逻辑按钮的绑定参数
    ButtonBinding buttonBinding(int logicalButton) const;

    /// 设置快照超时时间阈值（毫秒）
    void setTimeoutMs(qint64 timeoutMs) { _timeoutMs = timeoutMs; }

    /// 获取快照超时时间阈值（毫秒）
    qint64 timeoutMs() const { return _timeoutMs; }

    /// 获取当前快照元数据
    Snapshot currentSnapshot() const { return _snapshot; }

private:
    struct ButtonDebounceState
    {
        bool stableValue = false;       ///< 当前经过确认的稳定输出状态
        bool candidateValue = false;    ///< 当前正在观察的候选状态
        qint64 candidateSinceMs = 0;    ///< 候选状态首次出现的起始时间戳
    };

    Snapshot _snapshot;
    qint64 _timeoutMs = DefaultSnapshotTimeoutMs;
    std::array<AxisBinding, MaxLogicalAxes> _axisBindings{};
    std::array<ButtonBinding, MaxLogicalButtons> _buttonBindings{};
    mutable std::array<ButtonDebounceState, MaxLogicalButtons> _buttonStates{};
};
