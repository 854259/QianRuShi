#pragma once

#include <array>

class TraceControl {
public:
    void begin();
    void run();
    void stop();
    bool active() const;

private:
    using SensorData = std::array<bool, 5>;

    SensorData read_sensors() const;
    void execute_action(const SensorData &sensors);

    bool active_ = false;
};

extern TraceControl Tracer;
