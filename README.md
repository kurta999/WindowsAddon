<div align="center">

# WindowsAddon

**A desktop toolbox for automotive, embedded and everyday development work — CAN/UDS, Modbus, macros and workflow automation in a single tray application.**

[![CI](https://github.com/kurta999/WindowsAddon/actions/workflows/ci.yml/badge.svg)](https://github.com/kurta999/WindowsAddon/actions/workflows/ci.yml)
[![CodeQL](https://github.com/kurta999/WindowsAddon/actions/workflows/codeql.yml/badge.svg)](https://github.com/kurta999/WindowsAddon/actions/workflows/codeql.yml)
[![codecov](https://codecov.io/gh/kurta999/WindowsAddon/branch/master/graph/badge.svg)](https://codecov.io/gh/kurta999/WindowsAddon)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C.svg?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/23)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey.svg)](#building)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

[Features](#features) · [Screenshots](#screenshots) · [Building](#building) · [Tests](#tests) · [Architecture](docs/architecture.md) · [Contributing](CONTRIBUTING.md)

<img src="github_screens/main_page.png" alt="WindowsAddon main window" width="820">

</div>

---

## What is this?

A personal open-source project aimed at improving daily computer usage, particularly for programming and testing workflows. It bundles utilities I built to reduce repetitive tasks and speed up daily work, and is shared freely in case others find it useful.

Everything runs from one wxWidgets application that lives in the tray: a CAN/ISO-TP transceiver with a UDS DID reader/writer, a Modbus RTU/TCP master with live graphs, a macro engine for a second keyboard, a work-time tracker backed by SQLite, and a handful of small quality-of-life tools.

The codebase is split into headless protocol, persistence, automation and platform libraries plus a wxWidgets GUI target. See [docs/architecture.md](docs/architecture.md) for module boundaries and the dependency-injection migration rules, and [docs/adr](docs/adr) for the architectural decision records.

> **Project status** — actively maintained solo side project, developed alongside a full-time job. The core is covered by unit, end-to-end and sanitizer builds in CI, but not every GUI path gets retested after every change. Functionality takes priority over perfection; bug reports and pull requests are welcome.

An unfinished Qt port of this application is available at [QustomKeyboard](https://github.com/kurta999/QustomKeyboard). A full port to Qt is not planned.

## Table of contents

- [Features](#features)
- [Screenshots](#screenshots)
- [Building](#building)
- [Tests](#tests)
- [Continuous integration](#continuous-integration)
- [Configuration files](#configuration-files)
- [Advanced topics](#advanced-topics)
- [Dependencies](#dependencies)
- [Hardware](#hardware)
- [Contributing](#contributing)
- [License](#license)

## Features

### Automotive development

| Feature | Description |
| --- | --- |
| **CAN-USB Transceiver** | Send and receive standard, extended and ISO-TP (ISO 15765-2) CAN frames over USB, with frame logging, search and a bit/byte editor |
| **CAN Script handler** | Execute test scripts that set specific frames and send them to the bus automatically |
| **UDS DID Reader & Writer** | Read and write UDS DIDs from the GUI. DIDs are defined in `DidList.xml` and can optionally be cached locally |

<details>
<summary><b>In-depth details</b></summary>

**CAN-USB Transceiver** — Requires a [LAWICEL CAN USB](https://www.canusb.com/products/canusb/ "Lawicel CAN USB's Homepage") adapter, or a NUCLEO-G474RE board with a UART-TTL to USB adapter and a Waveshare SN65HVD230 3.3V CAN transceiver (or any equivalent hardware that converts TTL signals to CAN). Supports standard, extended, and ISO-TP (ISO 15765-2) CAN frames — useful for sending and receiving UDS frames. Includes frame logging, search, and a bit/byte editor for easy frame manipulation via GUI. Firmware for the Nucleo board: [CanUsbTransceiver](https://github.com/kurta999/CanUsbTransceiver). The default baud rate is 500 kbit/s; changing it is not yet implemented.

**CAN Script handler** — Execute test scripts by setting specific frames and sending them to the bus automatically. The script language is documented under [Advanced topics](#advanced-topics).

**UDS DID Reader & Writer** — Supported DID types: `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`, `string`, `bytearray`. Strings and byte arrays are padded when their length is less than the predefined length, and truncated if longer.

</details>

### General development

| Feature | Description |
| --- | --- |
| **Modbus Master** | Modbus RTU/TCP master for coils, discrete inputs, holding and input registers, with XML or multi-device JSON layouts, sparse grouped reads, runtime register editing, byte-order selection, scaling, conditional colors, custom frames and live graphs |
| **Command Executor** | Bind commands to GUI buttons with parameter support, executed on button click |
| **TerminalHotkey** | System-wide terminal hotkey, similar to Linux. Launchable from Windows Explorer using the current path, or from the Desktop |
| **File Explorer Opener** | Open a file explorer window by sending a specific TCP packet to the application |
| **TimeTracker** | Track work hours via GUI or a dedicated keyboard key; entries are editable and stored in SQLite |

<details>
<summary><b>In-depth details</b></summary>

**Modbus Master** — Registers can be configured in `Modbus.xml` or a multi-device JSON file selected through `[ModbusMaster] Device` in `settings.ini`. JSON layouts can be switched and edited at runtime; layout/style changes are persisted with a backup. The GUI supports typed and scaled values, byte-order changes, conditional colors, custom RTU/TCP commands, live value graphs, and CSV communication-log export. Register types include `(u)int16_t`, `(u)int32_t`, `(u)int64_t`, `float` and `double`.

**Command Executor** — Commands are configured in `Cmds.xml` or directly in the "CMD Executor" panel. Each command is executed via Windows `CreateProcess` when its button is clicked. This is particularly useful for frequently used command-line calls — no more copy-pasting or remembering aliases. Each command supports up to 16 variable parameters, which can be edited before execution by middle-clicking the button. Button appearance (font, color, bold) and duplication are fully configurable via the GUI.

**File Explorer Opener** — Useful when working with VirtualBox or WSL with Samba-mounted directories. Map the guest OS network share in Windows (default drive letter is `Z:`, configurable via `SharedDriveLetter` in `settings.ini`), enable `TCP_Backend` in `settings.ini`, then run the following on the guest OS to open the current directory in Windows Explorer:

```bash
echo expw$(pwd) | netcat <host IP> <TCP_Backend port>
```

Creating a shell alias for this command is recommended.

</details>

### Personal use

| Feature | Description |
| --- | --- |
| **CustomMacro** | Connect a second keyboard and bind macros to its keys, with full GUI macro editing and a macro recorder |
| **Sensors** | TCP backend for sensors with SQLite storage and an HTTP server for viewing graphs at `http://localhost:2005/graphs` |
| **Corsair G Keys backend** | Bind macros to Corsair G keys without installing iCUE |
| **AntiLock** | Prevents Windows from locking due to inactivity |
| **AntiNumLock** | Re-enables NumLock immediately if it gets turned off |
| **CPU Power Saver** | Reduces CPU frequency after an idle period and restores it when load rises |
| **ScreenshotSaver** | Saves the current clipboard screenshot to a `.png` file |
| **DirectoryBackup** | Backup folders to one or more destinations from the tray menu, with checksum and compression support |
| **Filesystem browser** | Lists files and directories recursively, sorted by size — useful for identifying storage bloat |
| **Alarm Handler** | Define alarms in `Alarms.xml` and trigger them with a key on the secondary keyboard |
| **CryptoPrice** | Live ETH & BTC buy/sell prices from Coinbase on the main panel (disabled by default) |

<details>
<summary><b>In-depth details</b></summary>

**CustomMacro** — Requires an external Nucleo L495ZG board with a UART-TTL to USB adapter for relaying key presses to the PC, and optionally a USB-A to Micro-USB adapter if the keyboard uses a USB-A port. The Nucleo acts as a USB host, receives key presses from the connected keyboard, and forwards them to the PC via UART. Keyboard hooking in Windows was considered but caused debugger crashes in MSVC; writing a custom kernel driver was the other option but was out of scope. Macros are configured in the settings page or directly in `settings.ini`, and can be bound to a global profile or per application. Key combinations are supported. Firmware for the Nucleo board: [UsbHost](https://github.com/kurta999/UsbHost). This feature also supports Corsair G keys without iCUE — see below.

**Sensors** — The backend is a simple TCP server that receives measurements from connected sensors. The average of measurements within a configured integration period is stored in an SQLite database. Graphs are generated from the last 30 measurements plus daily and weekly averages by default (configurable in `settings.ini`). The database updates every 10 minutes, or manually via the "Generate graphs" button. Graphs are accessible at `your_local_ip:2005/graphs` and can be viewed from any device on the network, including a phone. STM32 sensor source code: [AirQualitySensors](https://github.com/kurta999/AirQualitySensors).

**Backend for Corsair's G Keys** — iCUE's memory usage can sometimes grow to 500 MB, which is excessive for a background macro application. This feature provides an alternative: G key presses are received via a simple HID API and routed through the CustomMacro system, without iCUE installed. Supported devices: K95 RGB (18 G keys) and K95 RGB Platinum.

**AntiLock** — Prevents Windows from locking due to inactivity by periodically pressing SCROLL LOCK and moving the mouse at a configured interval. Useful for workstations where idle timeout cannot be disabled or where activity is monitored.

**AntiNumLock** — Prevents NumLock from being disabled; re-enables it immediately if turned off.

**CPU Power Saver** — Reduces CPU frequency after a configured idle period. For example, reducing an overclocked i7-10700K to 800–1200 MHz during idle can save 10–15 W per hour. Frequency is automatically restored when median CPU usage exceeds the configured threshold, and limiting resumes once load drops back below the minimum.

**ScreenshotSaver** — Press the configured screenshot key (F12 by default, on the secondary keyboard) to save the current clipboard image to the Screenshots folder as a `.png` file. The save path is configurable.

**DirectoryBackup** — Configure backup jobs in the settings page or in `settings.ini`. Configured backups appear in the tray menu — click one to start the backup. Supports an ignore list and SHA-256 checksums for integrity verification.

**Alarm Handler** — Define alarms in `Alarms.xml` and trigger them with a key on the secondary keyboard. A popup dialog prompts for a delay, after which the configured alarm action is executed.

**CryptoPrice** — Fetches live ETH & BTC buy/sell prices from Coinbase and displays them on the main panel. Disabled by default; enable by setting a non-zero `CryptoPriceUpdate` interval in `settings.ini`.

</details>

<details>
<summary><b>Macro syntax reference</b></summary>

Macros are defined per key. `BIND_NAME` must be the first entry in a macro definition.

| Directive | Effect |
| --- | --- |
| `BIND_NAME[binding name]` | Sets the macro name. Must be the first entry |
| `KEY_TYPE[text]` | Types the given text by pressing and releasing keys in sequence |
| `KEY_SEQ[CTRL+C]` | Presses all given keys in sequence and releases them — ideal for shortcuts |
| `DELAY[time in ms]` | Waits for the given number of milliseconds |
| `DELAY[min ms - max ms]` | Waits for a random duration between min and max milliseconds |
| `MOUSE_MOVE[x,y]` | Moves the mouse to the given coordinates |
| `MOUSE_INTERPOLATE[x,y]` | Moves the mouse to the given coordinates with interpolation |
| `MOUSE_PRESS[key]` | Presses the given mouse button |
| `MOUSE_RELEASE[key]` | Releases the given mouse button |
| `MOUSE_CLICK[key]` | Clicks (presses and releases) the given mouse button |
| `BASH[key]` | Executes the given command(s) in a terminal window (terminal remains visible) |
| `CMD[key]` | Executes the given command(s) without showing a terminal window |
| `CMD_XML[PageName+CommandName]` | Executes a predefined command from `Cmds.xml` |
| `CMD_FG[app_name.exe,Window title]` | Brings the specified application window to the foreground |
| `CMD_IMG[path_to_image,offset x,offset y]` | Scans the screen for the given image and clicks it if found |

Examples:

```ini
G1 = BIND_NAME[uint8_t] KEY_TYPE[uint8_t]
G4 = BIND_NAME[reddit CPP button] CMD_FG[chrome.exe,C++] CMD_IMG[test_image.png,3,3]
```

</details>

## Screenshots

<img src="github_screens/can_usb.png" alt="CAN-USB Transceiver" width="820">

<sub>CAN-USB Transceiver</sub>

<img src="github_screens/modbus_master.png" alt="Modbus Master" width="820">

<sub>Modbus Master</sub>

<details>
<summary><b>Full gallery (Windows)</b></summary>

**Sensors**

![Temperature graph for last week](github_screens/sensors_js_graph.png)

**CAN-USB Frame mapping**

![CAN-USB Frame mapping](github_screens/can_bit_editor.png)

**CAN-USB UDS frame sending over ISO-TP**

![UDS Frame sending over ISO-TP protocol](github_screens/iso_tp_message.png)

**UDS DID Handler (reading & writing)**

![UDS DID Handler](github_screens/did_readerwriter.png)

**Worktime Tracker**

![Worktime tracker](github_screens/timesheet.png)

**Command executor**

![Execute command by clicking its button](github_screens/cmd_executor.png)

**Command executor parameters**

![Execute command with custom parameters](github_screens/cmd_executor_params.png)

**Filesystem browser**

![Filesystem browser](github_screens/file_browser.png)

**Configuration**

![Configuration](github_screens/config_main_page.png)

**Macro editor**

![Macro editor sample 1](github_screens/macro_editor_1.png)
![Macro editor sample 2](github_screens/macro_editor_2.png)
![Macro editor add macro](github_screens/macro_add.png)

**Backup**

![Backup page](github_screens/backup_config.png)
![Backup in progress](github_screens/backup_progress.png)

**Log**

![Log](github_screens/log.png)

</details>

<details>
<summary><b>Full gallery (Linux)</b></summary>

**Main page**

![Main page in Linux build under Ubuntu](github_screens/main_page_linux.png)

**MTA → SA-MP Map Converter**

![MTA to SA-MP Map Converter in Linux build under Ubuntu](github_screens/map_converter_linux.png)

</details>

## Building

The code requires **C++23**. Dependencies are declared in `vcpkg.json` and are installed automatically when the vcpkg toolchain is used.

<details open>
<summary><b>Windows</b></summary>

1. Install Visual Studio 2022 or newer with the C++ desktop workload, CMake, and vcpkg.
2. Configure, build and test from the repository root. `VCPKG_ROOT` must point to the vcpkg installation:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The executable is generated as `build/Release/WindowsAddon.exe` with a Visual Studio generator, or `build/WindowsAddon.exe` with a single-configuration generator such as Ninja.

To build and run only the dependency-free core unit tests:

```powershell
cmake -S . -B build-tests -DWindowsAddon_BUILD_APP=OFF -DBUILD_TESTING=ON
cmake --build build-tests --config Release --parallel
ctest --test-dir build-tests -C Release --output-on-failure
```

</details>

<details>
<summary><b>Linux</b></summary>

Use a recent compiler — GCC 13+ or Clang 17+ recommended.

1. Install development packages for Boost, wxWidgets, fmt, SQLite, LodePNG and HIDAPI.
2. Run the following from the project root:

```bash
cmake -S . -B build            # add -G Ninja to use Ninja
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

To build without HIDAPI support, pass `-DWindowsAddon_USE_HIDAPI=OFF` to CMake.

</details>

<details>
<summary><b>Visual Studio solution (MSBuild)</b></summary>

The Visual Studio solution remains available as `WindowsAddon.sln` for MSBuild-based development. Run `vcpkg integrate install` first; the project defaults to wxWidgets 3.3 library names. Set the `WxLibraryVersion` MSBuild property to `32` when using a manual wxWidgets 3.2 installation.

For its `Release_Bsec|x64` configuration, set the `BSEC_LIBRARY_DIR` environment or MSBuild property to the directory containing your separately obtained `BSECLibrary64.lib`.

Available build configurations:

- **x86**: Debug, Release, Static Release, Release_BSec (with Bosch BSec library)
- **x64**: Debug, Release, Static Release, Release_BSec (with Bosch BSec library)

When debugging under Visual Studio (x64 Debug, Release or Release_BSec), create symlinks to the config files:

```powershell
mklink "TxList.xml" ..\..\TxList.xml
mklink "RxList.xml" ..\..\RxList.xml
mklink "FrameMapping.xml" ..\..\FrameMapping.xml
mklink "Modbus.xml" ..\..\Modbus.xml
mklink "DataSender.xml" ..\..\DataSender.xml
mklink "settings.ini" ..\..\settings.ini
```

</details>

<details>
<summary><b>CMake options</b></summary>

| Option | Effect |
| --- | --- |
| `-DWindowsAddon_BUILD_EXTERNAL_TESTS=OFF` | Excludes the SQLite/Boost-backed tests; default when the application is disabled |
| `-DWindowsAddon_WARNINGS_AS_ERRORS=ON` | Enables `/WX` (MSVC) or `-Werror` (GCC/Clang) for repository-owned targets |
| `-DWindowsAddon_ENABLE_SANITIZERS=ON` | Enables ASan and UBSan for GCC/Clang core builds |
| `-DWindowsAddon_ENABLE_COVERAGE=ON` | Enables GCC/Clang coverage instrumentation |
| `-DWindowsAddon_USE_HIDAPI=OFF` | Builds without HID access |
| `-DWindowsAddon_USE_OPENCV=ON` | Enables OpenCV support; requires the `opencv4` vcpkg port |
| `-DWindowsAddon_USE_BSEC=ON` | Enables Bosch BSEC for 64-bit Windows. Supply the proprietary binary separately with `-DWindowsAddon_BSEC_LIBRARY="C:/path/to/BSECLibrary64.lib"` |
| `-DWindowsAddon_BUILD_FUZZERS=ON` | Builds the Clang/libFuzzer network and protocol parser targets; requires `-DWindowsAddon_ENABLE_SANITIZERS=ON` |
| `-DBUILD_TESTING=OFF` | Skips the unit-test target |

</details>

## Tests

| CTest name | Needs | What it covers |
| --- | --- | --- |
| `WindowsAddon.Core` | nothing | the dependency-free logic: protocol parsers, codecs, the settings schema, string and script helpers |
| `WindowsAddon.External` | SQLite, Boost | the JSON configuration format and the time tracker's storage |
| `WindowsAddon.Modbus` | Boost, GTest | the Modbus protocol layer, register grouping and the presentation models |
| `WindowsAddon.EndToEnd` | the application build | the transports, handlers and the TCP backend driven over real sockets against simulated hardware; see [tests/e2e/README.md](tests/e2e/README.md) |
| `WindowsAddon.App` | the application build | unit tests for code that cannot leave the application build: alarms, the command board, the XML loaders, the measurement database, backups |

The two application suites are shuffled on every run, because they share one process with singletons, the working directory and the loopback interface.

## Continuous integration

GitHub Actions builds the dependency-free core on Windows with MSVC and on Ubuntu with both GCC and Clang. All three builds treat project warnings as errors. A separate Clang job runs ASan and UBSan over that core, and a second one builds the whole application on Ubuntu and runs the end-to-end and application suites under ASan, UBSan and LeakSanitizer — that is where the worker threads, sockets and locks are actually checked. The full Windows Release job restores a vcpkg binary cache, builds the desktop application, runs all tests, and publishes the executable as a workflow artifact.

CodeQL performs a weekly and per-change C/C++ analysis of the full Windows application. The coverage job always publishes `coverage.xml` as a workflow artifact and makes a non-blocking OIDC upload to Codecov; connect the repository in Codecov to activate the coverage badge.

## Configuration files

The application writes a `settings.ini` with defaults on first run when none exists — [`settings.example.ini`](settings.example.ini) documents every key.

| File | Purpose |
| --- | --- |
| `settings.ini` | Main configuration: enabled features, intervals, macros, backup jobs |
| `Cmds.xml` | Command Executor button definitions |
| `Modbus.xml` | Modbus register layout (or a JSON device file selected in `settings.ini`) |
| `TxList.xml` / `RxList.xml` | CAN frames to send and expected frames to receive |
| `FrameMapping.xml` | CAN frame and field mapping, required for CAN scripts |
| `DataSender.xml` | Periodic data sender definitions |
| `Alarms.xml` | Alarm definitions for the Alarm Handler |

## Advanced topics

<details>
<summary><b>ECU simulation</b></summary>

A basic ECU can be simulated by modifying the source directly. For more flexible simulation, write scripts instead. Add the following snippet to the `OnFrameReceived` function in `CanEntryHandler.cpp`:

```c
if(data_len == 8 && data[0] == 0x03 && data[1] == 0x22 && (data[2] == 0xF1 || data[2] == 0xF0))  /* Data length: 8, 22 = READ DID, DID ID LSB: AA */
{
    uint32_t send_id = frame_id;
    if(frame_id == 0xAAA)
        send_id = 0xBBB;
    else if(frame_id == 0xCCC)
        send_id = 0xDDD;

    uint8_t byte_buffer[8] = { 0x04, 0x62 };  /* Data length: 4, 0x62 DID reading (code 22) was successful */
    memcpy(&byte_buffer[2], &data[2], 6);  /* Copy source DID from receive buffer */
    m_CanTransport.Send(send_id, byte_buffer);
}
```

</details>

<details>
<summary><b>Scripts for CAN bus</b></summary>

Scripts are executed from the Script tab in the CAN panel. CAN frames and their fields must be mapped in `FrameMapping.xml`, otherwise scripts will not work. Script support is in an early stage and bugs may occur.

| Command | Effect |
| --- | --- |
| `WaitForFrame <frame name> <timeout ms>` | Waits until a specific frame with the given data appears on the bus |
| `SetFrameFieldRaw <frame name> <raw CAN data>` | Sets a frame field in raw byte format |
| `SetFrameField <field name> <value>` | Sets a CAN frame field value by name (field name, not frame name) |
| `SendFrame <frame name>` | Sends the named CAN frame; it must be mapped in `FrameMapping.xml` |
| `Sleep <delay in ms>` | Pauses script execution for the given duration |

```c
// Wait until a DID read request (service 22) for DID 2000 appears on the bus
SetFrameFieldRaw XXX_to_DTOOL 0x03222000AAAAAAA
WaitForFrame DTOOL_TO_XXX 120000

// Set VehicleHasClutch to 1 in the VEHICLE_INFO frame
SetFrameField VehicleHasClutch 0x1
SendFrame VEHICLE_INFO
Sleep 500
```

</details>

## Dependencies

**Required external dependencies** — [Boost](https://www.boost.org/), [wxWidgets](https://www.wxwidgets.org/), [fmt](https://fmt.dev/), [SQLite](https://www.sqlite.org/), [LodePNG](https://lodev.org/lodepng/), [HIDAPI](https://github.com/libusb/hidapi).

<details>
<summary><b>Bundled and optional libraries</b></summary>

- [lodepng](https://lodev.org/lodepng/ "lodepng's Homepage")
- [sqlite3](https://www.sqlite.org/index.html "sqlite3's Homepage")
- [enumser](http://www.naughter.com/enumser.html "enumser's Homepage")
- [sha256](https://github.com/B-Con/crypto-algorithms "sha256's Homepage")
- [AsyncSerial](https://github.com/fedetft/serial-port "AsyncSerial's Homepage")
- [Chart.js](https://www.chartjs.org/ "Charts.js' Homepage")
- [bitfield](https://github.com/openxc/bitfield-c "bitfield's Homepage")
- [isotp](https://github.com/lishen2/isotp-c "iso-tp's Homepage")
- [BSEC](https://www.bosch-sensortec.com/software-tools/software/bsec/ "Bosch's BSEC Homepage") — proprietary, supplied separately
- [opencv](https://opencv.org/ "OpenCV's Homepage") — optional

</details>

## Hardware

Some features need external hardware. Firmware for the boards is maintained in separate repositories:

| Feature | Hardware | Firmware |
| --- | --- | --- |
| Second keyboard | Nucleo L495ZG + UART-TTL to USB adapter | [UsbHost](https://github.com/kurta999/UsbHost) |
| Sensors | STM32 sensor board | [AirQualitySensors](https://github.com/kurta999/AirQualitySensors) |
| CAN | NUCLEO-G474RE + SN65HVD230 transceiver | [CanUsbTransceiver](https://github.com/kurta999/CanUsbTransceiver) |
| CAN (alternative) | [Lawicel CAN USB](https://www.canusb.com/products/canusb/) | — |

## Contributing

Pull requests, bug reports and suggestions are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for how to build, test and submit changes, and [SECURITY.md](SECURITY.md) for reporting security issues.

If the project is useful to you and you are able, a small donation via PayPal (`nmsstulaj@gmail.com`) is appreciated.

## License

Released under the [MIT License](LICENSE.md).
