#pragma once

#include <cstdint>

#include "driver/mcpwm_prelude.h"

class AvoidControl {
public:
    void begin();
    void run();
    void stop();
    bool active() const;

private:
    enum class AvoidState {
        kForward,
        kTurning,
        kBacking,
        kUTurn,
    };

    void servo_write(int angle);
    int distance_cm();
    void update_sweep();
    AvoidState decide_turn_state();
    void handle_forward();
    void handle_turning();
    void handle_backing();
    void handle_u_turn();

    mcpwm_timer_handle_t servo_timer_ = nullptr;
    mcpwm_oper_handle_t servo_operator_ = nullptr;
    mcpwm_cmpr_handle_t servo_comparator_ = nullptr;
    mcpwm_gen_handle_t servo_generator_ = nullptr;

    bool active_ = false;
    int current_servo_angle_ = 90;
    bool last_turn_left_ = false;
    int sweep_step_ = 0;
    uint32_t last_sweep_ms_ = 0;
    uint32_t servo_ready_ms_ = 0;
    bool sweep_measure_pending_ = false;
    int distance_left_cm_ = 200;
    int distance_center_cm_ = 200;
    int distance_right_cm_ = 200;
    AvoidState state_ = AvoidState::kForward;
    uint32_t state_start_ms_ = 0;
    int last_turn_direction_ = 0;
};

extern AvoidControl Avoider;
