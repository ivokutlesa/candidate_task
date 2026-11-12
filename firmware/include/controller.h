#pragma once

#include <cstdint>
#include <string>

struct ControllerStatus {
    double temperature_c;
    bool fan_on;
    bool interlock_active;
    std::string fault; // OK, INTERLOCK, COMM_TIMEOUT
};

// This class is portable control logic. It intentionally knows nothing about
// sockets, threads, peripherals or an operating system.
class Controller {
public:
    void SetTemperature(double temperature_c);
    void SetInterlock(bool active);
    void OnHeartbeat(std::uint64_t monotonic_ms);
    void Tick(std::uint64_t monotonic_ms);
    ControllerStatus Snapshot() const;

private:
    double temperature_c_ = 25.0;
    bool fan_on_ = false;
    bool interlock_active_ = false;
    bool heartbeat_seen_ = false;
    std::uint64_t last_heartbeat_ms_ = 0;
    std::string fault_ = "COMM_TIMEOUT";
};
