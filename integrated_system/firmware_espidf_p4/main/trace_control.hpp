#pragma once

#include <cstdint>

class TraceControl {
public:
    void begin();
    bool guard();
    void stop();
    bool active() const;

private:
    enum class RecoveryState {
        kIdle,
        kBacking,
        kTurning,
    };

    bool left_edge_triggered() const;
    bool right_edge_triggered() const;
    void start_recovery(bool left_edge, bool right_edge);

    bool active_ = false;
    bool turn_left_ = false;
    RecoveryState state_ = RecoveryState::kIdle;
    uint32_t state_start_ms_ = 0;
};

extern TraceControl Tracer;
