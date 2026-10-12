/**
 * @file RCSDKChannelMapper.cc
 * @brief 云卓 G20 RCSDK 遥控通道映射器实现文件
 * @details 实现了原始通道快照缓存、轴范围缩放与中位归一化（映射至 [-32768, 32767]）、
 *          通道截断保护、快照失效与超时处理，以及带去抖时间窗口的按钮状态机。
 */

#include "RCSDKChannelMapper.h"

#include <algorithm>
#include <cmath>

RCSDKChannelMapper::RCSDKChannelMapper()
{
}

void RCSDKChannelMapper::replaceSnapshot(const QVector<int>& values, qint64 timestampMs, quint64 version)
{
    _snapshot.values = values;
    _snapshot.timestampMs = timestampMs;
    _snapshot.version = version;
    _snapshot.valid = !values.isEmpty() && values.size() <= MaxChannels;

    if (!_snapshot.valid) {
        _buttonStates.fill({});
        return;
    }

    // 每次收到新快照时，根据快照采集时间戳更新各逻辑按钮的去抖状态机
    for (size_t i = 0; i < MaxLogicalButtons; ++i) {
        const ButtonBinding& binding = _buttonBindings[i];
        ButtonDebounceState& state = _buttonStates[i];

        if (binding.channel < 0 || binding.channel >= _snapshot.values.size()) {
            state = {};
            continue;
        }

        const int raw = _snapshot.values[binding.channel];
        const int distPressed = std::abs(raw - binding.pressedValue);
        const int distReleased = std::abs(raw - binding.releasedValue);

        bool observed = false;
        if (distPressed <= binding.threshold) {
            observed = true;
        } else if (distReleased <= binding.threshold) {
            observed = false;
        } else {
            continue;
        }

        if (observed != state.candidateValue) {
            state.candidateValue = observed;
            state.candidateSinceMs = timestampMs;
            if (binding.debounceMs <= 0) {
                state.stableValue = observed;
            }
        } else {
            if (state.candidateSinceMs == 0) {
                state.candidateSinceMs = timestampMs;
            }
            if (timestampMs - state.candidateSinceMs >= binding.debounceMs) {
                state.stableValue = state.candidateValue;
            }
        }
    }
}

void RCSDKChannelMapper::invalidate()
{
    _snapshot.valid = false;
    _snapshot.values.clear();
    _buttonStates.fill({});
}

int RCSDKChannelMapper::axisValue(int logicalAxis, qint64 nowMs) const
{
    if (logicalAxis < 0 || logicalAxis >= MaxLogicalAxes) {
        return 0;
    }

    if (!_snapshot.valid) {
        return 0;
    }

    if (_timeoutMs > 0 && nowMs > 0 && _snapshot.timestampMs > 0 && (nowMs - _snapshot.timestampMs > _timeoutMs)) {
        return 0;
    }

    const AxisBinding& binding = _axisBindings[static_cast<size_t>(logicalAxis)];
    if (binding.channel < 0 || binding.channel >= _snapshot.values.size()) {
        return 0;
    }

    if (binding.maximum <= binding.minimum) {
        return 0;
    }

    const int raw = _snapshot.values[binding.channel];
    int clamped = std::clamp(raw, binding.minimum, binding.maximum);

    if (binding.reversed) {
        clamped = binding.maximum - (clamped - binding.minimum);
    }

    int mapped = 0;
    if (clamped == binding.center) {
        mapped = 0;
    } else if (clamped < binding.center) {
        const int span = binding.center - binding.minimum;
        if (span > 0) {
            const double ratio = static_cast<double>(binding.center - clamped) / span;
            mapped = static_cast<int>(std::round(-ratio * (-AxisMin)));
        }
    } else {
        const int span = binding.maximum - binding.center;
        if (span > 0) {
            const double ratio = static_cast<double>(clamped - binding.center) / span;
            mapped = static_cast<int>(std::round(ratio * AxisMax));
        }
    }

    return std::clamp(mapped, AxisMin, AxisMax);
}

bool RCSDKChannelMapper::buttonValue(int logicalButton, qint64 nowMs) const
{
    if (logicalButton < 0 || logicalButton >= MaxLogicalButtons) {
        return false;
    }

    const ButtonBinding& binding = _buttonBindings[static_cast<size_t>(logicalButton)];
    if (binding.channel < 0) {
        _buttonStates[static_cast<size_t>(logicalButton)] = {};
        return false;
    }

    const bool timedOut = (_timeoutMs > 0 && nowMs > 0 && _snapshot.timestampMs > 0 && (nowMs - _snapshot.timestampMs > _timeoutMs));
    if (!_snapshot.valid || timedOut || binding.channel >= _snapshot.values.size()) {
        _buttonStates[static_cast<size_t>(logicalButton)] = {};
        return false;
    }

    return _buttonStates[static_cast<size_t>(logicalButton)].stableValue;
}

QVector<int> RCSDKChannelMapper::rawValues() const
{
    return _snapshot.values;
}

void RCSDKChannelMapper::setAxisBinding(int logicalAxis, const AxisBinding& binding)
{
    if (logicalAxis >= 0 && logicalAxis < MaxLogicalAxes) {
        _axisBindings[static_cast<size_t>(logicalAxis)] = binding;
    }
}

RCSDKChannelMapper::AxisBinding RCSDKChannelMapper::axisBinding(int logicalAxis) const
{
    if (logicalAxis >= 0 && logicalAxis < MaxLogicalAxes) {
        return _axisBindings[static_cast<size_t>(logicalAxis)];
    }
    return {};
}

void RCSDKChannelMapper::setButtonBinding(int logicalButton, const ButtonBinding& binding)
{
    if (logicalButton >= 0 && logicalButton < MaxLogicalButtons) {
        _buttonBindings[static_cast<size_t>(logicalButton)] = binding;
        _buttonStates[static_cast<size_t>(logicalButton)] = {};
    }
}

RCSDKChannelMapper::ButtonBinding RCSDKChannelMapper::buttonBinding(int logicalButton) const
{
    if (logicalButton >= 0 && logicalButton < MaxLogicalButtons) {
        return _buttonBindings[static_cast<size_t>(logicalButton)];
    }
    return {};
}
