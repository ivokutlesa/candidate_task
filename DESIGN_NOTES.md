# Candidate design notes

## What I changed

- implemented the controller fault priority, heartbeat watchdog, fan hysteresis, and safe recovery behavior
- added `latest_heartbeat_refreshes_watchdog` to check that the newest heartbeat resets the watchdog window and `normal_fan_on_reports_ok` to check that normal fan-on operation reports an `OK` fault
- implemented the fixed-capacity Single Producer/Single Consumer ring buffer and the fragmented, CRLF-aware line assembler with overlong-line recovery
- made telemetry parsing strict and culture-independent and added cancellable connection retry, reconnection, heartbeat cleanup and serialized client writes

## What I did not finish

## Design trade-off

I serialized heartbeat and console writes with a semaphore rather than introducing a dedicated outgoing-message queue and writer task. This keeps connection ownership and reconnect cleanup simple and prevents stale queued commands from being sent after reconnecting, but a slow socket write can delay other senders and there is no command prioritization.

## Ring buffer concurrency (B1.6)

The ring buffer uses separate atomic read and write counters for its single consumer and single producer. The producer marks bytes as ready only after writing them, and the consumer marks slots as free only after reading them. This prevents reading unfinished data or overwriting unread data. 
On a real Cortex-M, I would verify that the chosen atomic types are lock-free. If an atomic type is not lock-free, its implementation might internally use a lock or library routine. That is dangerous inside an interrupt because it can leat to a deadlock. 
i.e. 
1. Main program acquires hidden lock
2. Interrupt occurs
3. Interrupt tries to acquire the same lock
4. Deadlock: main cannot resume until the interrupt finishes
`volatile` alone is not sufficient because it only forces memory access, it does not make shared data safely synchronized.

## Testing on a real MCU

I would run hardware-in-the-loop tests that send UART commands to the board, control the simulated temperature and interlock inputs, and verify the resulting fan output and telemetry. I would also test communication loss and high UART load while recording response times with MCU timestamps, then run the system for an extended period to detect race conditions, buffer overruns, or recovery failures.

## Limits of this simulation for a real safety system

This simulation demonstrates the required control behavior, but it does not guarantee safety when hardware or software fails. A real system would need to place the output in a safe state even if the PC, communication link, controller software, sensor, or output driver fails. It would also require suitable hardware protection, fault diagnostics, verified response times and testing software together with hardware (integration tests).

Parts of this simulation that would not be sufficient for a real protective safety system:
- The interlock is a software-controlled value received through communication, rather than an independent physical safety circuit.
- Temperature is simulated, so sensor failures, disconnection, calibration errors, and implausible readings are not detected.
- The fan is represented by a Boolean value; there is no feedback proving that the physical fan or output contactor actually changed state.
