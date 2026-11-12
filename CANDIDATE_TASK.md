# Candidate assignment: Virtual temperature controller

## Scenario

A PC-based automation supervisor written in C# communicates with a simulated embedded controller implemented in C++. The device samples a virtual temperature and controls a fan output. The device must make safety-related control decisions **locally**, even if the PC becomes unresponsive.

This is a **simulation** of an interlock, not a real emergency-stop circuit or an implementation suitable for safety-critical deployment.

## Communication

- TCP server at `127.0.0.1:5042`, one client at a time.
- ASCII, newline-delimited messages terminated by `\n` (`\r\n` is accepted on input).
- TCP delivers a byte stream, not messages: one read may contain part of a line or several lines.
- Each new connection starts with a fresh controller (25 °C, no heartbeat yet, interlock clear).

### Telemetry (device → PC), every ~250 ms

```text
STATUS,<sequence>,<temperature_c>,<fan>,<interlock>,<fault>
STATUS,17,61.0,1,0,OK
```

A valid line has **exactly six comma-separated fields** and no whitespace anywhere:

| Field | Accepted format |
| --- | --- |
| 1 | Exactly `STATUS` (case-sensitive) |
| `sequence` | Decimal digits only (no sign), fitting in a signed 64-bit integer. Starts at 1 for each connection. |
| `temperature_c` | Digits, optionally followed by `.` and more digits (e.g. `61`, `61.0`, `61.25`). No sign, exponent or thousands separator. Value 0–100 inclusive. |
| `fan`, `interlock` | Exactly `0` or `1` |
| `fault` | Exactly `OK`, `INTERLOCK` or `COMM_TIMEOUT` (case-sensitive) |

Anything else is invalid telemetry. Numbers always use `.` as the decimal separator, whatever the PC's regional settings are.

### Commands (PC → device)

| Command | Effect | Reply |
| --- | --- | --- |
| `PING` | Heartbeat: refresh the device's communication watchdog | `PONG` |
| `TEMP,70.0` | Set the virtual sensor to 70 °C (0–100) | `ACK,TEMP` |
| `INTERLOCK,1` / `INTERLOCK,0` | Assert / clear the simulated protective interlock | `ACK,INTERLOCK` |
| `INJECT,FRAGMENT` | Toggle splitting outgoing telemetry across two TCP sends | `ACK,FRAGMENT,1` or `ACK,FRAGMENT,0` |
| `INJECT,MALFORMED` | Replace the next telemetry line with a corrupted one | `ACK,MALFORMED` |
| `INJECT,DISCONNECT` | Close the connection abruptly after the reply | `ACK,DISCONNECT` |

An out-of-range or unparsable temperature returns `ERR,TEMP_RANGE`; any other unrecognized command returns `ERR,UNKNOWN_COMMAND`. Replies and telemetry are interleaved on the same connection.

The supplied C# client sends `PING` every 400 ms (unless paused with `heartbeat off`).

## Part A — Controller logic (C++, required)

File: `firmware/src/controller.cpp`. Implement `Controller::Tick(monotonic_ms)`. The resulting fan state and fault must follow these rules:

1. **Fault priority:** `INTERLOCK` > `COMM_TIMEOUT` > `OK`. Whenever the fault is not `OK`, the fan is OFF.
2. **Interlock:** while the interlock is active, the fan is OFF and the fault is `INTERLOCK`, regardless of temperature or heartbeat state. This includes before the first heartbeat.
3. **Before the first heartbeat:** the fan is OFF and the fault is `COMM_TIMEOUT` (unless rule 2 applies).
4. **Watchdog:** if more than 1500 ms have passed since the most recent heartbeat, the fan is OFF and the fault is `COMM_TIMEOUT`. Exactly 1500 ms is **not** timed out.
5. **Normal control (fault `OK`):** the fan turns ON at 60.0 °C or higher and stays ON until the temperature is 55.0 °C or lower (hysteresis). Between 55 and 60 °C the fan keeps its previous state.
6. **Recovery:** when the interlock is cleared or a fresh heartbeat arrives after a timeout, normal control resumes **from fan OFF**. The fan only turns ON again once the temperature reaches 60.0 °C. Example: fan ON at 70 °C → interlock on → temperature drops to 57 °C → interlock off → the fan stays OFF.
7. `Tick` must not sleep or block. Use only the `monotonic_ms` value passed to it (never wall-clock time). `Tick` is called roughly every 20 ms by the transport.

The tests in `firmware/tests/controller_tests.cpp` cover these rules.

## Part B — Serial receive buffer (C++, embedded)

On a real microcontroller the PC link would be a UART. Received bytes arrive one at a time in an interrupt service routine (ISR) and are processed later by the main loop. Implement the two building blocks for that path. They are portable C++ that does not need hardware.

### B1. `RingBuffer<N>` in `firmware/include/ring_buffer.h` (required)

A fixed-capacity FIFO of bytes:

1. Holds exactly `N` bytes (`N` is a power of two, enforced by the supplied `static_assert`).
2. `Push` returns `false` when full, increments the overflow counter, and never overwrites unread data.
3. `Pop` returns `false` when empty. Bytes come out in the order they went in, including after the indices wrap around.
4. `Size()` returns the number of unread bytes; `Overflows()` the number of rejected bytes.
5. No dynamic allocation, exceptions, locks or blocking.
6. Safe for **one producer and one consumer running concurrently** (the ISR calls `Push`, the main loop calls `Pop`). In `DESIGN_NOTES.md`, briefly explain what makes your implementation safe for that, and what would change on a real Cortex-M (e.g. `volatile`, atomics, disabling interrupts).

### B2. `LineAssembler<MaxLen>` in `firmware/include/line_assembler.h` (senior level)

`Poll(rx)` pulls bytes from a `RingBuffer` and assembles command lines:

1. It consumes bytes until it completes a line or `rx` is empty. It stops **immediately after** a `\n`, so further bytes stay in `rx` for the next call.
2. On `\n` it returns `LineResult::Line`. `Line()` then returns the text without the `\n`, NUL-terminated, and `Length()` its length. A single `\r` directly before the `\n` is removed. A `\r` anywhere else is part of the line.
3. A partial line is kept between calls. `Poll` returns `LineResult::None` until the line is complete.
4. A line of up to `MaxLen` characters (after removing the trailing `\r`) is accepted. Longer lines are discarded up to and including their `\n`, and that `Poll` returns `LineResult::TooLong` once. The next line is handled normally.
5. Empty lines are returned as lines of length 0.
6. Fixed-size storage only; no dynamic allocation.

The tests in `firmware/tests/serial_rx_tests.cpp` cover B1 and B2. You do **not** need to use these classes in `main.cpp`. If you have time left, you may say in your notes how you would.

## Part C — Telemetry parser (C#, required)

File: `desktop/AutomationClient/Protocol.cs`. Improve `Protocol.TryParseStatus` so that it:

1. Accepts every telemetry line that matches the format table above and fills `DeviceStatus` accurately.
2. Rejects everything else by returning `false`. It must never throw, including for `null` or empty input.
3. Parses numbers with the invariant culture, so it works on machines whose regional settings use a decimal comma.
4. Keeps no state between calls, so an invalid line never affects the next valid one.

The tests in `desktop/AutomationClient.Tests/Program.cs` cover the main cases; the format table is the full specification.

## Part D — Connection resilience (C#, senior level)

File: `desktop/AutomationClient/DeviceClient.cs`. Improve `DeviceClient.RunAsync` so that it:

1. Keeps trying to connect if the device is not running at startup, and reconnects when the connection is dropped (`disconnect` command, or the controller being stopped and restarted).
2. Waits a bounded delay between attempts (e.g. 500 ms) that is cancelled immediately on shutdown. It must not busy-loop.
3. After reconnecting, resumes heartbeats on the new connection, without leaking heartbeat tasks or writing to an old stream.
4. Exits cleanly and promptly on `quit` or Ctrl+C, without unhandled exceptions.
5. Keeps writes safe while the heartbeat loop and console commands send at the same time.

You may refactor `DeviceClient` internally, but keep its public members, the protocol and the console output format.

## Part E — Design notes 

Fill in `DESIGN_NOTES.md`. Cover:

- what you changed, and anything you did not finish;
- one important design trade-off you made;
- your answer to B1.6 (concurrency of the ring buffer);
- how you would test a real STM32 (or similar MCU) implementation of this firmware;
- which parts of this simulation would **not** be sufficient for a real protective safety system.

## Acceptance demonstration

In the follow-up discussion, be ready to show:

1. All C++ and C# unit tests pass (or explain which ones do not, and why).
2. `temp 70` turns the fan ON, `temp 57` keeps it ON, `temp 55` turns it OFF.
3. With the fan ON at `temp 80`, `interlock on` turns it OFF with fault `INTERLOCK`; `interlock off` turns it back ON.
4. `heartbeat off` leads to fan OFF with fault `COMM_TIMEOUT` within about 2 seconds; `heartbeat on` restores normal operation.
5. `malformed` and `fragment` neither crash the client nor disturb the following telemetry.
6. Senior level: `disconnect`, and stopping and restarting the controller, lead to automatic reconnection; starting the client before the controller also works; `quit` and Ctrl+C exit promptly.
