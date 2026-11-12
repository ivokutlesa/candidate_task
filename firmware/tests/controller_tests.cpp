#include "controller.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using TestFn = void (*)();

static void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static void initial_state_is_safe() {
    Controller c;
    c.SetTemperature(80.0);
    c.Tick(100);
    auto s = c.Snapshot();
    expect(!s.fan_on, "Fan must remain off until the first heartbeat");
    expect(s.fault == "COMM_TIMEOUT", "Initial fault must be COMM_TIMEOUT");
}

static void below_threshold_is_off() {
    Controller c;
    c.OnHeartbeat(100);
    c.SetTemperature(54.0);
    c.Tick(100);
    expect(!c.Snapshot().fan_on, "Fan should be off below threshold");
    expect(c.Snapshot().fault == "OK", "Healthy controller should report OK");
}

static void turns_on_at_upper_threshold() {
    Controller c;
    c.OnHeartbeat(100);
    c.SetTemperature(60.0);
    c.Tick(100);
    expect(c.Snapshot().fan_on, "Fan must switch on at 60 C");
}

static void hysteresis_holds_output() {
    Controller c;
    c.OnHeartbeat(0);
    c.SetTemperature(64.0);
    c.Tick(0);
    c.SetTemperature(57.0);
    c.Tick(100);
    expect(c.Snapshot().fan_on, "Fan must stay on at 57 C after being on");
    c.SetTemperature(55.0);
    c.Tick(200);
    expect(!c.Snapshot().fan_on, "Fan must switch off at 55 C");
}

static void interlock_forces_safe_state() {
    Controller c;
    c.OnHeartbeat(0);
    c.SetTemperature(80.0);
    c.Tick(0);
    c.SetInterlock(true);
    c.Tick(100);
    expect(!c.Snapshot().fan_on, "Interlock must force fan OFF");
    expect(c.Snapshot().fault == "INTERLOCK", "Interlock fault missing");
}

static void timeout_forces_safe_state() {
    Controller c;
    c.OnHeartbeat(100);
    c.SetTemperature(90.0);
    c.Tick(100);
    c.Tick(1601);
    expect(!c.Snapshot().fan_on, "Timed-out heartbeat must force fan OFF");
    expect(c.Snapshot().fault == "COMM_TIMEOUT", "Communication fault missing");
}

static void timeout_boundary_and_recovery() {
    Controller c;
    c.SetTemperature(70.0);
    c.OnHeartbeat(100);
    c.Tick(1600); // Exactly 1500 ms is still acceptable.
    expect(c.Snapshot().fan_on, "Heartbeat should be valid at exactly 1500 ms");
    c.Tick(1601);
    expect(!c.Snapshot().fan_on, "Heartbeat should expire after 1500 ms");
    c.OnHeartbeat(1602);
    c.Tick(1602);
    expect(c.Snapshot().fan_on, "A fresh heartbeat must permit recovery");
}

static void interlock_overrides_healthy_link() {
    Controller c;
    c.OnHeartbeat(10);
    c.SetInterlock(true);
    c.SetTemperature(70);
    c.Tick(10);
    expect(c.Snapshot().fault == "INTERLOCK", "Interlock priority incorrect");
    c.SetInterlock(false);
    c.Tick(11);
    expect(c.Snapshot().fan_on, "Must resume AUTO after interlock clears");
}

static void interlock_before_first_heartbeat() {
    Controller c;
    c.SetTemperature(80.0);
    c.SetInterlock(true);
    c.Tick(100);
    expect(!c.Snapshot().fan_on, "Interlock must keep fan OFF before first heartbeat");
    expect(c.Snapshot().fault == "INTERLOCK", "Interlock outranks COMM_TIMEOUT");
    c.SetInterlock(false);
    c.Tick(200);
    expect(c.Snapshot().fault == "COMM_TIMEOUT", "Without heartbeat the fault is COMM_TIMEOUT");
}

static void interlock_outranks_stale_heartbeat() {
    Controller c;
    c.OnHeartbeat(0);
    c.SetTemperature(80.0);
    c.SetInterlock(true);
    c.Tick(5000);
    expect(!c.Snapshot().fan_on, "Fan must be OFF");
    expect(c.Snapshot().fault == "INTERLOCK", "Interlock outranks a stale heartbeat");
}

static void interlock_release_resumes_from_off() {
    Controller c;
    c.OnHeartbeat(0);
    c.SetTemperature(70.0);
    c.Tick(0);
    c.SetInterlock(true);
    c.SetTemperature(57.0);
    c.Tick(100);
    c.SetInterlock(false);
    c.Tick(200);
    expect(!c.Snapshot().fan_on, "After interlock release at 57 C the fan must stay OFF");
    c.SetTemperature(60.0);
    c.Tick(300);
    expect(c.Snapshot().fan_on, "Fan must switch on again at 60 C");
}

static void timeout_recovery_resumes_from_off() {
    Controller c;
    c.OnHeartbeat(0);
    c.SetTemperature(70.0);
    c.Tick(0);
    c.SetTemperature(57.0);
    c.Tick(2000);
    expect(!c.Snapshot().fan_on, "Timed out: fan OFF");
    c.OnHeartbeat(2100);
    c.Tick(2100);
    expect(!c.Snapshot().fan_on, "After timeout recovery at 57 C the fan must stay OFF");
    expect(c.Snapshot().fault == "OK", "Fresh heartbeat restores OK");
}

static void latest_heartbeat_refreshes_watchdog() {
    Controller c;
    c.SetTemperature(70.0);
    c.OnHeartbeat(100);
    c.OnHeartbeat(1000);

    c.Tick(2500);  // Exactly 1500 ms after the latest heartbeat.
    auto s = c.Snapshot();

    expect(s.fan_on, "Latest heartbeat must refresh the watchdog");
    expect(s.fault == "OK", "Controller must remain healthy");
}

static void normal_fan_on_reports_ok() {
    Controller c;
    c.OnHeartbeat(100);
    c.SetTemperature(60.0);
    c.Tick(100);

    auto s = c.Snapshot();
    expect(s.fan_on, "Fan must turn ON at 60 C");
    expect(s.fault == "OK", "Normal operation must report OK");
}

int main() {
    std::vector<std::pair<std::string, TestFn>> tests = {
        {"initial_state_is_safe", initial_state_is_safe},
        {"below_threshold_is_off", below_threshold_is_off},
        {"turns_on_at_upper_threshold", turns_on_at_upper_threshold},
        {"hysteresis_holds_output", hysteresis_holds_output},
        {"interlock_forces_safe_state", interlock_forces_safe_state},
        {"timeout_forces_safe_state", timeout_forces_safe_state},
        {"timeout_boundary_and_recovery", timeout_boundary_and_recovery},
        {"interlock_overrides_healthy_link", interlock_overrides_healthy_link},
        {"interlock_before_first_heartbeat", interlock_before_first_heartbeat},
        {"interlock_outranks_stale_heartbeat", interlock_outranks_stale_heartbeat},
        {"interlock_release_resumes_from_off", interlock_release_resumes_from_off},
        {"timeout_recovery_resumes_from_off", timeout_recovery_resumes_from_off},
    };
    int failed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception& e) {
            std::cerr << "FAIL " << test.first << ": " << e.what() << '\n';
            ++failed;
        }
    }
    std::cout << (tests.size() - failed) << "/" << tests.size() << " passed\n";
    return failed == 0 ? 0 : 1;
}
