This exercise simulates a supervisory C# program talking to a firmware-like C++ controller over TCP on `127.0.0.1`. The controller has a temperature input, a cooling-fan output and a simulated protective interlock. Parts of the C++ firmware and of the C# client are deliberately incomplete or wrong; fixing them is the assignment. This is not a product implementation or a safety-certified control system.

Read this file first, then `CANDIDATE_TASK.md`, which holds the exact requirements.

## Timebox

Plan for about **2–2.5 hours** of focused work, followed by a separate ~30-minute discussion with the interviewer.

| Step | Suggested time |
| --- | --- |
| Read the documents, build and run the starter | 15 min |
| Part A — controller logic (C++) | 25 min |
| Part B — serial receive buffer (C++) | 30 min |
| Part C — telemetry parser (C#) | 20 min |
| Part D — connection resilience (C#) | 30 min |
| Part E — design notes | 10 min |

A working, tested subset with good explanations is better than a large unfinished implementation. Partial solutions are acceptable; say in `DESIGN_NOTES.md` what you did not finish and why.

Parts A, B (ring buffer), C and E are expected from every candidate. Part B's line assembler and Part D are expected for a senior-level submission, but everyone is welcome to attempt them.

## Rules

- You may use official documentation and search engines. You must be able to explain every line you submit.
- Do not change the wire protocol, the test assertions or thresholds, the public interfaces in `firmware/include/`, or the console output format of `desktop/AutomationClient/Program.cs`. Adding new tests, files and private members is encouraged.

## Prerequisites

- **CMake 3.20 or later** and a C++17 compiler (Visual Studio 2022 with "Desktop development with C++", or GCC/Clang)
- **.NET 8 SDK** (`dotnet --list-sdks` should list an 8.x version; newer SDKs that also have 8.x installed are fine)
- **Python 3** (optional, for the transport smoke test)
- Windows 10/11, macOS or Linux

No microcontroller, USB/serial adapter, IDE plugins, Docker or NuGet packages are needed. Check your tools with:

```sh
cmake --version
dotnet --list-sdks
```

On Windows, if `cmake` is not on your PATH, run the commands below from the **Developer PowerShell for VS 2022**, which includes CMake.

## Repository layout

| Path | What it is | Do you edit it? |
| --- | --- | --- |
| `firmware/src/controller.cpp` | Portable control logic (Part A) | **Yes** |
| `firmware/include/ring_buffer.h`, `line_assembler.h` | Serial receive path (Part B) | **Yes** (implement the TODOs) |
| `firmware/src/main.cpp` | TCP transport that hosts the controller ("virtual firmware") | Normally no |
| `firmware/tests/*.cpp` | C++ unit tests | Add tests; don't change existing assertions |
| `desktop/AutomationClient/Protocol.cs` | Telemetry parser (Part C) | **Yes** |
| `desktop/AutomationClient/DeviceClient.cs` | TCP client, heartbeat, reconnect (Part D) | **Yes** |
| `desktop/AutomationClient/Program.cs` | Console UI | Normally no |
| `desktop/AutomationClient.Tests/` | C# unit tests | Add tests; don't change existing assertions |
| `tools/transport_smoke.py` | Checks the supplied TCP scaffold only | No |
| `DESIGN_NOTES.md` | Your explanation (Part E) | **Yes** |

## Build and run

Run all commands from the repository root (the folder containing this README). Use two terminals.

**Terminal 1 — build and start the virtual firmware**

```sh
cmake -S firmware -B build/firmware
cmake --build build/firmware --config Release
```

Start it:

- Windows (Visual Studio generator): `build\firmware\Release\virtual_controller.exe`
- macOS/Linux: `build/firmware/virtual_controller`

(With a single-configuration generator such as Ninja, the Windows executable is at `build\firmware\virtual_controller.exe`.)

It prints `Virtual controller listening on 127.0.0.1:5042` and keeps running until you press Ctrl+C. Rebuild and restart it after every C++ change.

**Terminal 2 — start the C# supervisor**

```sh
dotnet run --project desktop/AutomationClient
```

Type commands and press Enter:

| Console command | What it does |
| --- | --- |
| `temp 70` | Set the simulated sensor to 70 °C (0–100) |
| `interlock on` / `interlock off` | Assert / clear the simulated protective interlock |
| `heartbeat off` / `heartbeat on` | Pause / resume the client's `PING` heartbeat (simulates a frozen PC) |
| `fragment` | Toggle splitting each telemetry line across two TCP sends |
| `malformed` | Make the device send one corrupted telemetry line |
| `disconnect` | Make the device drop the TCP connection |
| `quit` (or Ctrl+C) | Exit the client |

Every ~250 ms the client prints the device state, for example:

```text
SEQ=17 TEMP=61.0C FAN=ON INTERLOCK=CLEAR FAULT=OK
```

The virtual firmware creates a **fresh controller for every TCP connection**, so after a reconnect the temperature is back at 25 °C and the fault starts as `COMM_TIMEOUT` until the first heartbeat arrives. That is intended.

If port 5042 is in use, pass another port to both programs, e.g. `virtual_controller 5050` and `dotnet run --project desktop/AutomationClient -- 5050`.

## Run the tests

```sh
ctest --test-dir build/firmware --output-on-failure -C Release
dotnet run --project desktop/AutomationClient.Tests
```

`ctest` runs two test programs, `controller_tests` (Part A) and `serial_rx_tests` (Part B). Each test program prints one `PASS`/`FAIL` line per test case and exits with code 0 only if everything passes.

Optionally, check the supplied TCP scaffold (this does not test your work):

- macOS/Linux: `python3 tools/transport_smoke.py build/firmware/virtual_controller`
- Windows: `py tools\transport_smoke.py build\firmware\Release\virtual_controller.exe`

### What the untouched starter does

Many tests are **designed to fail** before you start. With the starter code you should see:

| Check | Starter result |
| --- | --- |
| Firmware and client build | Succeed, no errors |
| `controller_tests` | 4/12 passed |
| `serial_rx_tests` | 1/12 passed (the concurrency test waits ~5 s before failing) |
| `AutomationClient.Tests` | 4/11 passed |
| Transport smoke test | PASS |
| Manual run | `temp 57` after `temp 70` turns the fan OFF (wrong); `interlock on` does not stop the fan; `heartbeat off` never causes `COMM_TIMEOUT`; after `disconnect` the client stops and does not reconnect |

If your starter results differ, contact us before you continue.

## Submitting

1. Make sure `DESIGN_NOTES.md` is filled in.
2. Delete the generated folders `build/`, `bin/` and `obj/` (the included `.gitignore` already ignores them if you use git).
3. Send the repository back as a single `.zip` file, or as a link to a private git repository you have shared with us. Committing as you go is welcome.

In the follow-up discussion you will be asked to run the tests and the demonstration in `CANDIDATE_TASK.md`, and to walk through your changes.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| `cmake` or `ctest` not found (Windows) | Use the Developer PowerShell for VS 2022, or install CMake from cmake.org |
| `ctest: unknown argument --test-dir` | Your CMake is older than 3.20; update it, or run `ctest -C Release --output-on-failure` from inside `build/firmware` |
| `Cannot bind localhost:5042` | Another program (or an old controller) uses the port; stop it or use another port |
| Client prints nothing after `Connected` | Make sure the controller is running in the other terminal |
| `dotnet` reports a missing 8.0 framework | Install the .NET 8 SDK alongside any newer one |
