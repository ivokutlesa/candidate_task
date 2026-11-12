#include "controller.h"

void Controller::SetTemperature(double temperature_c) {
    temperature_c_ = temperature_c;
}

void Controller::SetInterlock(bool active) {
    interlock_active_ = active;
}

void Controller::OnHeartbeat(std::uint64_t monotonic_ms) {
    heartbeat_seen_ = true;
    last_heartbeat_ms_ = monotonic_ms;
}

void Controller::Tick(std::uint64_t monotonic_ms) {
    // TODO (candidate): Replace this intentionally simplistic controller.
    // Requirements (see CANDIDATE_TASK.md Part A for the full list):
    //   - Fault priority: INTERLOCK > COMM_TIMEOUT > OK.
    //   - Active interlock: fan OFF, fault INTERLOCK (also before the first
    //     heartbeat and while the heartbeat is stale).
    //   - No heartbeat yet OR >1500 ms since last heartbeat: fan OFF,
    //     fault COMM_TIMEOUT.
    //   - Otherwise: fault OK; fan turns ON at >=60.0 C and stays ON until
    //     temperature falls to <=55.0 C (hysteresis).
    //   - After a forced OFF ends, control resumes from fan OFF: the fan only
    //     turns ON again once the temperature is >=60.0 C.
    //   - Use monotonic_ms rather than sleeping or wall-clock timestamps.

    if (interlock_active_) {
        fan_on_ = false;
        fault_ = "INTERLOCK";
    } else if (!heartbeat_seen_ || (monotonic_ms - last_heartbeat_ms_ > 1500)) {
        fan_on_ = false;
        fault_ = "COMM_TIMEOUT";
    } else {
        fault_ = "OK";
        if (fan_on_) {
            if (temperature_c_ <= 55.0) {
                fan_on_ = false;
            }
        } else {
            if (temperature_c_ >= 60.0) {
                fan_on_ = true;
            }
        }
    }
}

ControllerStatus Controller::Snapshot() const {
    return {temperature_c_, fan_on_, interlock_active_, fault_};
}
