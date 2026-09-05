<a id="english"></a>
<div align="center">

🇬🇧 **English** &nbsp;·&nbsp; 🇩🇪 [Deutsch](#deutsch)

# WindowsAddon

**A desktop toolbox for automotive, embedded and everyday development work — CAN/UDS, Modbus, macros and workflow automation in a single tray application.**

[![CI](https://github.com/kurta999/WindowsAddon/actions/workflows/ci.yml/badge.svg)](https://github.com/kurta999/WindowsAddon/actions/workflows/ci.yml)
[![CodeQL](https://github.com/kurta999/WindowsAddon/actions/workflows/codeql.yml/badge.svg)](https://github.com/kurta999/WindowsAddon/actions/workflows/codeql.yml)
[![Cppcheck](https://github.com/kurta999/WindowsAddon/actions/workflows/cppcheck.yml/badge.svg)](https://github.com/kurta999/WindowsAddon/actions/workflows/cppcheck.yml)
[![codecov](https://codecov.io/gh/kurta999/WindowsAddon/branch/main/graph/badge.svg)](https://codecov.io/gh/kurta999/WindowsAddon)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C.svg?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/23)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey.svg)](#building)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

[Features](#features) · [Screenshots](#screenshots) · [Building](#building) · [Tests](#tests) · [Architecture](docs/architecture.md) · [Contributing](#contributing)

<img src="github_screens/main_page.png" alt="WindowsAddon main window" width="820">

</div>

---

## What is this?

A personal open-source project aimed at improving daily computer usage, particularly for programming and testing workflows. It bundles utilities I built to reduce repetitive tasks and speed up daily work, and is shared freely in case others find it useful.

Everything runs from one wxWidgets application that lives in the tray: a CAN/ISO-TP transceiver with a UDS DID reader/writer, a Modbus RTU/TCP master with live graphs, a macro engine for a second keyboard, a work-time tracker backed by SQLite, and a handful of small quality-of-life tools.

The codebase is split into headless protocol, persistence, automation and platform libraries plus a wxWidgets GUI target. See [docs/architecture.md](docs/architecture.md) for module boundaries and the dependency-injection migration rules, and [docs/adr](docs/adr) for the architectural decision records.

> **Project status** — actively maintained solo side project, developed alongside a full-time job. The core is covered by unit, fuzz and sanitizer builds in CI, but not every GUI path gets retested after every change. Functionality takes priority over perfection; bug reports and pull requests are welcome.

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

**UDS DID Reader & Writer** — Supported DID types: `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`, `string`, `bytearray`. Strings and byte arrays are padded when their length is less than the predefined length, and truncated if longer. `DidList.xml` is read from the working directory; the repository does not ship a sample file.

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

**Modbus Master** — Registers can be configured in `Modbus.xml` or a multi-device JSON file selected through the optional `Device` key in the `[ModbusMaster]` section of `settings.ini`. JSON layouts can be switched and edited at runtime; layout/style changes are persisted with a backup. The GUI supports typed and scaled values, byte-order changes, conditional colors, custom RTU/TCP commands, live value graphs, and CSV communication-log export. Register types include `(u)int16_t`, `(u)int32_t`, `(u)int64_t`, `float` and `double`.

**Command Executor** — Commands are configured in `Cmds.xml` or directly in the "CMD Executor" panel. Each command is executed via Windows `CreateProcess` when its button is clicked. This is particularly useful for frequently used command-line calls — no more copy-pasting or remembering aliases. Each command supports up to 16 variable parameters, which can be edited before execution by middle-clicking the button. Button appearance (font, color, bold) and duplication are fully configurable via the GUI.

**File Explorer Opener** — Useful when working with VirtualBox or WSL with Samba-mounted directories. Map the guest OS network share in Windows (default drive letter is `Z:`, configurable via `SharedDriveLetter` in the `[App]` section of `settings.ini`). The command is received by the Sensors TCP server, so `[Sensors] Enable` must be `1`; the port is `[Sensors] TCP_Port` (default `2005`). Then run the following on the guest OS to open the current directory in Windows Explorer:

```bash
echo expw$(pwd) | netcat <host IP> 2005
```

Creating a shell alias for this command is recommended.

</details>

### Personal use

| Feature | Description |
| --- | --- |
| **CustomMacro** | Connect a second keyboard and bind macros to its keys, with full GUI macro editing and a macro recorder |
| **Sensors** | TCP backend for sensors with SQLite storage and an HTTP server for viewing graphs at `http://localhost:2005/graphs` |
| **Corsair G Keys backend** | Bind macros to Corsair G keys without installing iCUE |
| **AntiNumLock** | Re-enables NumLock immediately if it gets turned off |
| **CPU Power Saver** | Reduces CPU frequency after an idle period and restores it when load rises |
| **ScreenshotSaver** | Saves the current clipboard screenshot to a `.png` file |
| **DirectoryBackup** | Backup folders to one or more destinations from the tray menu, with checksum and compression support |
| **Filesystem browser** | Lists files and directories recursively, sorted by size — useful for identifying storage bloat |
| **Alarm Handler** | Define alarms in `Alarms.xml` and trigger them with a key on the secondary keyboard |
| **CryptoPrice** | Live ETH & BTC buy/sell prices from Coinbase on the main panel |

<details>
<summary><b>In-depth details</b></summary>

**CustomMacro** — Requires an external Nucleo L495ZG board with a UART-TTL to USB adapter for relaying key presses to the PC, and optionally a USB-A to Micro-USB adapter if the keyboard uses a USB-A port. The Nucleo acts as a USB host, receives key presses from the connected keyboard, and forwards them to the PC via UART. Keyboard hooking in Windows was considered but caused debugger crashes in MSVC; writing a custom kernel driver was the other option but was out of scope. Macros are configured in the settings page or directly in `settings.ini`, and can be bound to a global profile or per application. Key combinations are supported. Firmware for the Nucleo board: [UsbHost](https://github.com/kurta999/UsbHost). This feature also supports Corsair G keys without iCUE — see below.

**Sensors** — The backend is a simple TCP server that receives measurements from connected sensors. The average of measurements within a configured integration period is stored in an SQLite database. Graphs are generated from the last 30 measurements plus daily and weekly averages by default (configurable in `settings.ini`). The database updates every 10 minutes, or manually via the "Generate graphs" button. Graphs are accessible at `your_local_ip:2005/graphs` and can be viewed from any device on the network, including a phone. STM32 sensor source code: [AirQualitySensors](https://github.com/kurta999/AirQualitySensors).

**Backend for Corsair's G Keys** — iCUE's memory usage can sometimes grow to 500 MB, which is excessive for a background macro application. This feature provides an alternative: G key presses are received via a simple HID API and routed through the CustomMacro system, without iCUE installed. Supported devices: K95 RGB (18 G keys) and K95 RGB Platinum.

**AntiNumLock** — Prevents NumLock from being disabled; re-enables it immediately if turned off.

**CPU Power Saver** — Reduces CPU frequency after a configured idle period. For example, reducing an overclocked i7-10700K to 800–1200 MHz during idle can save 10–15 W per hour. Frequency is automatically restored when median CPU usage exceeds the configured threshold, and limiting resumes once load drops back below the minimum.

**ScreenshotSaver** — Press the configured screenshot key (F12 by default, on the secondary keyboard) to save the current clipboard image to the Screenshots folder as a `.png` file. The save path is configurable.

**DirectoryBackup** — Configure backup jobs in the settings page or in `settings.ini`. Configured backups appear in the tray menu — click one to start the backup. Supports an ignore list and SHA-256 checksums for integrity verification.

**Alarm Handler** — Define alarms in `Alarms.xml` and trigger them with a key on the secondary keyboard. A popup dialog prompts for a delay, after which the configured alarm action is executed.

**CryptoPrice** — Fetches live ETH & BTC buy/sell prices from Coinbase and displays them on the main panel. The shipped `settings.ini` polls every 5 seconds; set `CryptoPriceUpdate` in the `[App]` section to `0` to disable it.

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
<summary><b>Linux</b></summary>

**Main page**

![Main page in Linux build under Ubuntu](github_screens/main_page_linux.png)

</details>

## Building

The code requires **C++23**. Dependencies are declared in `vcpkg.json` and are installed automatically when the vcpkg toolchain is used. All project-specific CMake options use the upper-case `WINDOWSADDON_` prefix; CMake variable names are case-sensitive.

<details open>
<summary><b>Windows</b></summary>

1. Install Visual Studio 2022 or newer with the C++ desktop workload, CMake, and vcpkg.
2. Configure, build and test from the repository root. `VCPKG_ROOT` must point to the vcpkg installation:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The executable is generated as `build/Release/WindowsAddon.exe` with a Visual Studio generator, or `build/WindowsAddon.exe` with a single-configuration generator such as Ninja. The wxWidgets and other vcpkg DLLs are copied next to it.

To build and run only the dependency-free core unit tests:

```powershell
cmake -S . -B build-tests -DWINDOWSADDON_BUILD_APP=OFF -DBUILD_TESTING=ON
cmake --build build-tests --config Release --parallel
ctest --test-dir build-tests -C Release --output-on-failure
```

</details>

<details>
<summary><b>Linux</b></summary>

Use a recent compiler — GCC 13+ or Clang 19+. Clang 18 cannot compile the code because its libstdc++ configuration hides `std::expected`.

1. Install development packages for Boost (including `program_options`), wxWidgets (`core`, `base`, `aui`, `stc`, `propgrid`), fmt, SQLite, LodePNG and HIDAPI.
2. Run the following from the project root:

```bash
cmake -S . -B build            # add -G Ninja to use Ninja
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

To build without HIDAPI support, pass `-DWINDOWSADDON_USE_HIDAPI=OFF` to CMake.

</details>

<details>
<summary><b>Visual Studio solution (MSBuild)</b></summary>

The Visual Studio solution remains available as `WindowsAddon.sln` for MSBuild-based development. Run `vcpkg integrate install` first; the project defaults to wxWidgets 3.3 library names. Set the `WxLibraryVersion` MSBuild property to `32` when using a manual wxWidgets 3.2 installation.

For its `Release_Bsec|x64` configuration, set the `BSEC_LIBRARY_DIR` environment or MSBuild property to the directory containing your separately obtained `BSECLibrary64.lib`.

Available build configurations:

- **x86**: Debug, Release, Static Release, Release_Bsec (with Bosch BSEC library)
- **x64**: Debug, Release, Static Release, Release_Bsec (with Bosch BSEC library)

When debugging under Visual Studio (x64 Debug, Release or Release_Bsec), create symlinks to the config files:

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
| `-DWINDOWSADDON_BUILD_APP=OFF` | Skips the wxWidgets application and builds only the headless libraries and their tests |
| `-DWINDOWSADDON_BUILD_EXTERNAL_TESTS=OFF` | Excludes the SQLite/Boost-backed tests; default when the application is disabled |
| `-DWINDOWSADDON_WARNINGS_AS_ERRORS=ON` | Enables `/WX` (MSVC) or `-Werror` (GCC/Clang) for repository-owned targets |
| `-DWINDOWSADDON_ENABLE_SANITIZERS=ON` | Enables ASan and UBSan for GCC/Clang core builds |
| `-DWINDOWSADDON_ENABLE_COVERAGE=ON` | Enables GCC/Clang coverage instrumentation |
| `-DWINDOWSADDON_USE_HIDAPI=OFF` | Builds without HID access |
| `-DWINDOWSADDON_USE_OPENCV=ON` | Enables OpenCV support; OpenCV must be installed separately, it is not part of `vcpkg.json` |
| `-DWINDOWSADDON_USE_BSEC=ON` | Enables Bosch BSEC for 64-bit Windows. Supply the proprietary binary separately with `-DWINDOWSADDON_BSEC_LIBRARY="C:/path/to/BSECLibrary64.lib"` |
| `-DWINDOWSADDON_BUILD_FUZZERS=ON` | Builds the Clang/libFuzzer network and protocol parser targets; requires `-DWINDOWSADDON_ENABLE_SANITIZERS=ON` |
| `-DBUILD_TESTING=OFF` | Skips the unit-test targets |

</details>

## Tests

| CTest name | Needs | What it covers |
| --- | --- | --- |
| `WindowsAddon.Core` | nothing | the dependency-free logic: CAN codecs, ISO-TP, the Modbus client and presentation models, sensor and TCP parsers, the settings document, backup core, script launching, string and macro helpers |
| `WindowsAddon.External` | SQLite, Boost | the XML configuration loaders and the time tracker's storage |

Both suites use the small in-repo test framework in `tests/TestFramework.hpp`; no GoogleTest or Catch is required. Two libFuzzer targets (`WindowsAddonNetworkMessageFuzzer`, `WindowsAddonProtocolParsersFuzzer`) cover the TCP message parser and the CAN, ISO-TP, Modbus and sensor parsers.

There are currently no automated end-to-end or GUI tests; the wxWidgets application is only compiled in CI, not exercised.

## Continuous integration

GitHub Actions builds the dependency-free core on Windows with MSVC and on Ubuntu with both GCC and Clang. All three builds treat project warnings as errors. A separate Clang job runs the core tests under ASan and UBSan, and a libFuzzer smoke job builds both fuzz targets and runs a short campaign against each. The full Windows Release job restores a vcpkg binary cache, builds the desktop application, runs both test suites, and publishes the executable and its DLLs as a workflow artifact.

CodeQL performs a weekly and per-change C/C++ analysis of the full Windows application. The coverage job always publishes `coverage.xml` as a workflow artifact and makes a non-blocking OIDC upload to Codecov; connect the repository in Codecov to activate the coverage badge.

Cppcheck runs on every change as a second, cheaper static analysis pass. It needs none of the project dependencies, so the job is a plain `apt-get install cppcheck` followed by a two-minute scan of `src/`, `libs/`, `tests/` and `fuzz/`; `--library=wxwidgets` is what lets it parse the event-table macros in the GUI sources, without which those files are silently skipped. Only `error`-severity findings block the build — everything else is advisory, with the counts in the job summary and the full `cppcheck-report.txt` published as an artifact. Findings that were checked and rejected live in [`.cppcheck-suppressions`](.cppcheck-suppressions) with the reason beside them, so that file stays a record of decisions rather than a backlog.

## Configuration files

The application writes a `settings.ini` with defaults on first run when none exists. The tracked [`settings.ini`](settings.ini) documents the keys with inline comments.

| File | Purpose |
| --- | --- |
| `settings.ini` | Main configuration: enabled features, intervals, macros, backup jobs |
| `Cmds.xml` | Command Executor button definitions |
| `Modbus.xml` | Modbus register layout (or a JSON device file selected in `settings.ini`) |
| `TxList.xml` / `RxList.xml` | CAN frames to send and expected frames to receive |
| `FrameMapping.xml` | CAN frame and field mapping, required for CAN scripts |
| `DataSender.xml` | Periodic data sender definitions |
| `Alarms.xml` | Alarm definitions for the Alarm Handler |
| `DidList.xml` | UDS DID definitions for the DID reader/writer (not shipped, create as needed) |

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

Pull requests, bug reports and suggestions are welcome. Open an issue or a pull request on GitHub; the build and test commands above are what CI runs, so a change that passes them locally is ready for review.

If the project is useful to you and you are able, a small donation via PayPal (`nmsstulaj@gmail.com`) is appreciated.

## License

Released under the [MIT License](LICENSE.md).

---
---

<a id="deutsch"></a>
<div align="center">

🇬🇧 [English](#english) &nbsp;·&nbsp; 🇩🇪 **Deutsch**

# WindowsAddon

**Eine Desktop-Werkzeugkiste für Automotive-, Embedded- und Alltagsentwicklung — CAN/UDS, Modbus, Makros und Workflow-Automatisierung in einer einzigen Tray-Anwendung.**

[![CI](https://github.com/kurta999/WindowsAddon/actions/workflows/ci.yml/badge.svg)](https://github.com/kurta999/WindowsAddon/actions/workflows/ci.yml)
[![CodeQL](https://github.com/kurta999/WindowsAddon/actions/workflows/codeql.yml/badge.svg)](https://github.com/kurta999/WindowsAddon/actions/workflows/codeql.yml)
[![Cppcheck](https://github.com/kurta999/WindowsAddon/actions/workflows/cppcheck.yml/badge.svg)](https://github.com/kurta999/WindowsAddon/actions/workflows/cppcheck.yml)
[![codecov](https://codecov.io/gh/kurta999/WindowsAddon/branch/main/graph/badge.svg)](https://codecov.io/gh/kurta999/WindowsAddon)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C.svg?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/23)
[![Plattform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey.svg)](#de-build)
[![Lizenz: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

[Funktionen](#de-funktionen) · [Screenshots](#de-screenshots) · [Build](#de-build) · [Tests](#de-tests) · [Architektur](docs/architecture.md) · [Mitwirken](#de-mitwirken)

<img src="github_screens/main_page.png" alt="WindowsAddon Hauptfenster" width="820">

</div>

---

<a id="de-was-ist-das"></a>
## Was ist das?

Ein privates Open-Source-Projekt, das die tägliche Arbeit am Computer erleichtern soll, vor allem beim Programmieren und Testen. Es bündelt Werkzeuge, die ich gebaut habe, um wiederkehrende Aufgaben zu reduzieren und schneller zu arbeiten, und wird frei geteilt, falls es anderen nützt.

Alles läuft in einer wxWidgets-Anwendung, die im Tray lebt: ein CAN/ISO-TP-Transceiver mit UDS-DID-Reader/-Writer, ein Modbus-RTU/TCP-Master mit Live-Graphen, eine Makro-Engine für eine zweite Tastatur, eine Arbeitszeiterfassung auf SQLite-Basis und eine Handvoll kleiner Komfort-Werkzeuge.

Der Code ist in GUI-freie Bibliotheken für Protokolle, Persistenz, Automatisierung und Plattform sowie ein wxWidgets-GUI-Target aufgeteilt. Modulgrenzen und die Regeln für die Migration auf Dependency Injection stehen in [docs/architecture.md](docs/architecture.md), die Architekturentscheidungen in [docs/adr](docs/adr) (englisch).

> **Projektstatus** — aktiv gepflegtes Einzelprojekt neben einem Vollzeitjob. Der Kern ist in der CI durch Unit-, Fuzz- und Sanitizer-Builds abgedeckt, aber nicht jeder GUI-Pfad wird nach jeder Änderung neu getestet. Funktionalität geht vor Perfektion; Fehlermeldungen und Pull Requests sind willkommen.

Eine unfertige Qt-Portierung dieser Anwendung gibt es unter [QustomKeyboard](https://github.com/kurta999/QustomKeyboard). Eine vollständige Portierung auf Qt ist nicht geplant.

<a id="de-inhalt"></a>
## Inhaltsverzeichnis

- [Funktionen](#de-funktionen)
- [Screenshots](#de-screenshots)
- [Build](#de-build)
- [Tests](#de-tests)
- [Continuous Integration](#de-ci)
- [Konfigurationsdateien](#de-konfiguration)
- [Fortgeschrittene Themen](#de-fortgeschritten)
- [Abhängigkeiten](#de-abhaengigkeiten)
- [Hardware](#de-hardware)
- [Mitwirken](#de-mitwirken)
- [Lizenz](#de-lizenz)

<a id="de-funktionen"></a>
## Funktionen

### Automotive-Entwicklung

| Funktion | Beschreibung |
| --- | --- |
| **CAN-USB-Transceiver** | Sendet und empfängt Standard-, Extended- und ISO-TP-(ISO 15765-2)-CAN-Frames über USB, mit Frame-Log, Suche und Bit/Byte-Editor |
| **CAN-Skript-Handler** | Führt Testskripte aus, die bestimmte Frames setzen und automatisch auf den Bus senden |
| **UDS-DID-Reader & -Writer** | Liest und schreibt UDS-DIDs aus der GUI. DIDs werden in `DidList.xml` definiert und können optional lokal gecacht werden |

<details>
<summary><b>Details</b></summary>

**CAN-USB-Transceiver** — Benötigt einen [LAWICEL CAN USB](https://www.canusb.com/products/canusb/ "Lawicel CAN USB Homepage")-Adapter oder ein NUCLEO-G474RE-Board mit UART-TTL-zu-USB-Adapter und einem Waveshare SN65HVD230 3,3-V-CAN-Transceiver (oder gleichwertige Hardware, die TTL-Signale in CAN umsetzt). Unterstützt Standard-, Extended- und ISO-TP-(ISO 15765-2)-CAN-Frames — nützlich zum Senden und Empfangen von UDS-Frames. Enthält Frame-Log, Suche und einen Bit/Byte-Editor zur einfachen Frame-Bearbeitung in der GUI. Firmware für das Nucleo-Board: [CanUsbTransceiver](https://github.com/kurta999/CanUsbTransceiver). Die Standard-Baudrate ist 500 kbit/s; eine Änderung ist noch nicht implementiert.

**CAN-Skript-Handler** — Führt Testskripte aus, indem bestimmte Frames gesetzt und automatisch auf den Bus gesendet werden. Die Skriptsprache ist unter [Fortgeschrittene Themen](#de-fortgeschritten) dokumentiert.

**UDS-DID-Reader & -Writer** — Unterstützte DID-Typen: `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`, `string`, `bytearray`. Strings und Byte-Arrays werden aufgefüllt, wenn sie kürzer als die vordefinierte Länge sind, und abgeschnitten, wenn sie länger sind. `DidList.xml` wird aus dem Arbeitsverzeichnis gelesen; das Repository liefert keine Beispieldatei mit.

</details>

### Allgemeine Entwicklung

| Funktion | Beschreibung |
| --- | --- |
| **Modbus-Master** | Modbus-RTU/TCP-Master für Coils, Discrete Inputs, Holding- und Input-Register, mit XML- oder Multi-Device-JSON-Layouts, gruppierten Lesezugriffen, Registerbearbeitung zur Laufzeit, Byte-Order-Auswahl, Skalierung, bedingten Farben, eigenen Frames und Live-Graphen |
| **Command Executor** | Bindet Befehle mit Parameterunterstützung an GUI-Buttons, ausgeführt per Klick |
| **TerminalHotkey** | Systemweiter Terminal-Hotkey wie unter Linux. Startbar aus dem Windows Explorer mit dem aktuellen Pfad oder vom Desktop |
| **File Explorer Opener** | Öffnet ein Explorer-Fenster, wenn ein bestimmtes TCP-Paket an die Anwendung gesendet wird |
| **TimeTracker** | Erfasst Arbeitszeiten per GUI oder über eine eigene Tastaturtaste; Einträge sind editierbar und werden in SQLite gespeichert |

<details>
<summary><b>Details</b></summary>

**Modbus-Master** — Register werden in `Modbus.xml` oder einer Multi-Device-JSON-Datei konfiguriert, die über den optionalen Schlüssel `Device` im Abschnitt `[ModbusMaster]` der `settings.ini` gewählt wird. JSON-Layouts lassen sich zur Laufzeit umschalten und bearbeiten; Layout-/Stil-Änderungen werden mit Backup gespeichert. Die GUI unterstützt typisierte und skalierte Werte, Byte-Order-Wechsel, bedingte Farben, eigene RTU/TCP-Befehle, Live-Graphen und den CSV-Export des Kommunikationslogs. Registertypen: `(u)int16_t`, `(u)int32_t`, `(u)int64_t`, `float` und `double`.

**Command Executor** — Befehle werden in `Cmds.xml` oder direkt im Panel „CMD Executor" konfiguriert. Jeder Befehl wird beim Klick auf seinen Button per Windows-`CreateProcess` ausgeführt. Das ist besonders praktisch für häufig genutzte Kommandozeilenaufrufe — kein Kopieren und kein Merken von Aliassen mehr. Jeder Befehl unterstützt bis zu 16 variable Parameter, die vor der Ausführung per Mittelklick auf den Button bearbeitet werden können. Aussehen der Buttons (Schrift, Farbe, fett) und Duplizieren sind vollständig über die GUI konfigurierbar.

**File Explorer Opener** — Nützlich mit VirtualBox oder WSL und per Samba gemounteten Verzeichnissen. Die Netzwerkfreigabe des Gastsystems in Windows einbinden (Standard-Laufwerksbuchstabe `Z:`, konfigurierbar über `SharedDriveLetter` im Abschnitt `[App]` der `settings.ini`). Der Befehl wird vom Sensors-TCP-Server entgegengenommen, daher muss `[Sensors] Enable` auf `1` stehen; der Port ist `[Sensors] TCP_Port` (Standard `2005`). Danach im Gastsystem Folgendes ausführen, um das aktuelle Verzeichnis im Windows Explorer zu öffnen:

```bash
echo expw$(pwd) | netcat <Host-IP> 2005
```

Ein Shell-Alias für diesen Befehl ist empfehlenswert.

</details>

### Privater Gebrauch

| Funktion | Beschreibung |
| --- | --- |
| **CustomMacro** | Zweite Tastatur anschließen und Makros an ihre Tasten binden, mit vollständiger Makro-Bearbeitung in der GUI und Makro-Recorder |
| **Sensors** | TCP-Backend für Sensoren mit SQLite-Speicher und HTTP-Server für Graphen unter `http://localhost:2005/graphs` |
| **Corsair-G-Tasten-Backend** | Makros an Corsair-G-Tasten binden, ohne iCUE zu installieren |
| **AntiNumLock** | Schaltet NumLock sofort wieder ein, wenn es ausgeschaltet wird |
| **CPU Power Saver** | Senkt die CPU-Frequenz nach einer Leerlaufzeit und stellt sie bei steigender Last wieder her |
| **ScreenshotSaver** | Speichert den aktuellen Screenshot aus der Zwischenablage als `.png` |
| **DirectoryBackup** | Sichert Ordner aus dem Tray-Menü an ein oder mehrere Ziele, mit Prüfsummen- und Kompressionsunterstützung |
| **Dateisystem-Browser** | Listet Dateien und Verzeichnisse rekursiv nach Größe sortiert — praktisch, um Speicherfresser zu finden |
| **Alarm-Handler** | Alarme in `Alarms.xml` definieren und mit einer Taste der Zweittastatur auslösen |
| **CryptoPrice** | Aktuelle ETH- und BTC-Kauf-/Verkaufspreise von Coinbase auf dem Hauptpanel |

<details>
<summary><b>Details</b></summary>

**CustomMacro** — Benötigt ein externes Nucleo-L495ZG-Board mit UART-TTL-zu-USB-Adapter, das Tastendrücke an den PC weiterleitet, und optional einen USB-A-zu-Micro-USB-Adapter, falls die Tastatur USB-A verwendet. Das Nucleo arbeitet als USB-Host, empfängt die Tastendrücke der angeschlossenen Tastatur und leitet sie per UART an den PC weiter. Keyboard-Hooking unter Windows wurde erwogen, führte aber zu Debugger-Abstürzen in MSVC; ein eigener Kerneltreiber wäre die Alternative gewesen, lag aber außerhalb des Rahmens. Makros werden auf der Einstellungsseite oder direkt in der `settings.ini` konfiguriert und können an ein globales Profil oder pro Anwendung gebunden werden. Tastenkombinationen werden unterstützt. Firmware für das Nucleo-Board: [UsbHost](https://github.com/kurta999/UsbHost). Diese Funktion unterstützt auch Corsair-G-Tasten ohne iCUE — siehe unten.

**Sensors** — Das Backend ist ein einfacher TCP-Server, der Messwerte von angeschlossenen Sensoren empfängt. Der Mittelwert der Messungen innerhalb eines konfigurierten Integrationszeitraums wird in einer SQLite-Datenbank gespeichert. Graphen werden standardmäßig aus den letzten 30 Messungen plus Tages- und Wochenmittelwerten erzeugt (konfigurierbar in der `settings.ini`). Die Datenbank wird alle 10 Minuten aktualisiert, oder manuell über den Button „Generate graphs". Die Graphen sind unter `deine_lokale_ip:2005/graphs` erreichbar und können von jedem Gerät im Netzwerk angesehen werden, auch vom Handy. Quellcode der STM32-Sensoren: [AirQualitySensors](https://github.com/kurta999/AirQualitySensors).

**Backend für Corsairs G-Tasten** — Der Speicherverbrauch von iCUE wächst manchmal auf 500 MB, was für eine Makro-Anwendung im Hintergrund zu viel ist. Diese Funktion bietet eine Alternative: G-Tastendrücke werden über eine einfache HID-API empfangen und durch das CustomMacro-System geleitet, ganz ohne iCUE. Unterstützte Geräte: K95 RGB (18 G-Tasten) und K95 RGB Platinum.

**AntiNumLock** — Verhindert, dass NumLock deaktiviert wird; schaltet es sofort wieder ein.

**CPU Power Saver** — Senkt die CPU-Frequenz nach einer konfigurierten Leerlaufzeit. Ein übertakteter i7-10700K auf 800–1200 MHz im Leerlauf spart beispielsweise 10–15 W pro Stunde. Die Frequenz wird automatisch wiederhergestellt, wenn die mittlere CPU-Auslastung den konfigurierten Schwellwert überschreitet; die Begrenzung setzt wieder ein, sobald die Last unter das Minimum fällt.

**ScreenshotSaver** — Die konfigurierte Screenshot-Taste (standardmäßig F12 auf der Zweittastatur) speichert das aktuelle Bild aus der Zwischenablage als `.png` im Ordner „Screenshots". Der Speicherpfad ist konfigurierbar.

**DirectoryBackup** — Backup-Jobs werden auf der Einstellungsseite oder in der `settings.ini` konfiguriert. Konfigurierte Backups erscheinen im Tray-Menü — ein Klick startet das Backup. Unterstützt eine Ignorierliste und SHA-256-Prüfsummen zur Integritätsprüfung.

**Alarm-Handler** — Alarme in `Alarms.xml` definieren und mit einer Taste der Zweittastatur auslösen. Ein Dialog fragt nach einer Verzögerung, nach deren Ablauf die konfigurierte Alarmaktion ausgeführt wird.

**CryptoPrice** — Holt aktuelle ETH- und BTC-Kauf-/Verkaufspreise von Coinbase und zeigt sie auf dem Hauptpanel an. Die mitgelieferte `settings.ini` fragt alle 5 Sekunden ab; `CryptoPriceUpdate` im Abschnitt `[App]` auf `0` setzen, um die Funktion abzuschalten.

</details>

<details>
<summary><b>Makro-Syntax-Referenz</b></summary>

Makros werden pro Taste definiert. `BIND_NAME` muss der erste Eintrag einer Makro-Definition sein.

| Anweisung | Wirkung |
| --- | --- |
| `BIND_NAME[Name]` | Setzt den Makro-Namen. Muss der erste Eintrag sein |
| `KEY_TYPE[Text]` | Tippt den Text, indem Tasten nacheinander gedrückt und losgelassen werden |
| `KEY_SEQ[CTRL+C]` | Drückt alle angegebenen Tasten nacheinander und lässt sie los — ideal für Tastenkürzel |
| `DELAY[Zeit in ms]` | Wartet die angegebene Anzahl Millisekunden |
| `DELAY[min ms - max ms]` | Wartet eine zufällige Dauer zwischen min und max Millisekunden |
| `MOUSE_MOVE[x,y]` | Bewegt die Maus zu den angegebenen Koordinaten |
| `MOUSE_INTERPOLATE[x,y]` | Bewegt die Maus interpoliert zu den angegebenen Koordinaten |
| `MOUSE_PRESS[Taste]` | Drückt die angegebene Maustaste |
| `MOUSE_RELEASE[Taste]` | Lässt die angegebene Maustaste los |
| `MOUSE_CLICK[Taste]` | Klickt (drückt und lässt los) die angegebene Maustaste |
| `BASH[Befehl]` | Führt den/die Befehl(e) in einem Terminalfenster aus (Terminal bleibt sichtbar) |
| `CMD[Befehl]` | Führt den/die Befehl(e) ohne sichtbares Terminalfenster aus |
| `CMD_XML[Seite+Befehlsname]` | Führt einen vordefinierten Befehl aus `Cmds.xml` aus |
| `CMD_FG[app.exe,Fenstertitel]` | Holt das angegebene Anwendungsfenster in den Vordergrund |
| `CMD_IMG[Bildpfad,Offset x,Offset y]` | Sucht das Bild auf dem Bildschirm und klickt es an, wenn es gefunden wird |

Beispiele:

```ini
G1 = BIND_NAME[uint8_t] KEY_TYPE[uint8_t]
G4 = BIND_NAME[reddit CPP button] CMD_FG[chrome.exe,C++] CMD_IMG[test_image.png,3,3]
```

</details>

<a id="de-screenshots"></a>
## Screenshots

<img src="github_screens/can_usb.png" alt="CAN-USB-Transceiver" width="820">

<sub>CAN-USB-Transceiver</sub>

<details>
<summary><b>Vollständige Galerie (Windows)</b></summary>

**Sensoren**

![Temperaturgraph der letzten Woche](github_screens/sensors_js_graph.png)

**CAN-USB Frame-Mapping**

![CAN-USB Frame-Mapping](github_screens/can_bit_editor.png)

**CAN-USB UDS-Frame-Versand über ISO-TP**

![UDS-Frame-Versand über ISO-TP](github_screens/iso_tp_message.png)

**UDS-DID-Handler (Lesen & Schreiben)**

![UDS-DID-Handler](github_screens/did_readerwriter.png)

**Arbeitszeiterfassung**

![Arbeitszeiterfassung](github_screens/timesheet.png)

**Command Executor**

![Befehl per Button-Klick ausführen](github_screens/cmd_executor.png)

**Command-Executor-Parameter**

![Befehl mit eigenen Parametern ausführen](github_screens/cmd_executor_params.png)

**Dateisystem-Browser**

![Dateisystem-Browser](github_screens/file_browser.png)

**Konfiguration**

![Konfiguration](github_screens/config_main_page.png)

**Makro-Editor**

![Makro-Editor Beispiel 1](github_screens/macro_editor_1.png)
![Makro-Editor Beispiel 2](github_screens/macro_editor_2.png)
![Makro-Editor Makro hinzufügen](github_screens/macro_add.png)

**Backup**

![Backup-Seite](github_screens/backup_config.png)
![Laufendes Backup](github_screens/backup_progress.png)

**Log**

![Log](github_screens/log.png)

</details>

<details>
<summary><b>Linux</b></summary>

**Hauptseite**

![Hauptseite im Linux-Build unter Ubuntu](github_screens/main_page_linux.png)

</details>

<a id="de-build"></a>
## Build

Der Code benötigt **C++23**. Abhängigkeiten sind in `vcpkg.json` deklariert und werden automatisch installiert, wenn die vcpkg-Toolchain verwendet wird. Alle projektspezifischen CMake-Optionen verwenden das großgeschriebene Präfix `WINDOWSADDON_`; CMake-Variablennamen unterscheiden Groß- und Kleinschreibung.

<details open>
<summary><b>Windows</b></summary>

1. Visual Studio 2022 oder neuer mit dem C++-Desktop-Workload, CMake und vcpkg installieren.
2. Aus dem Repository-Stammverzeichnis konfigurieren, bauen und testen. `VCPKG_ROOT` muss auf die vcpkg-Installation zeigen:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Die ausführbare Datei entsteht mit einem Visual-Studio-Generator als `build/Release/WindowsAddon.exe`, mit einem Ein-Konfigurations-Generator wie Ninja als `build/WindowsAddon.exe`. Die wxWidgets- und übrigen vcpkg-DLLs werden daneben kopiert.

Nur die abhängigkeitsfreien Kern-Unit-Tests bauen und ausführen:

```powershell
cmake -S . -B build-tests -DWINDOWSADDON_BUILD_APP=OFF -DBUILD_TESTING=ON
cmake --build build-tests --config Release --parallel
ctest --test-dir build-tests -C Release --output-on-failure
```

</details>

<details>
<summary><b>Linux</b></summary>

Einen aktuellen Compiler verwenden — GCC 13+ oder Clang 19+. Clang 18 kann den Code nicht übersetzen, weil seine libstdc++-Konfiguration `std::expected` verbirgt.

1. Entwicklungspakete für Boost (inklusive `program_options`), wxWidgets (`core`, `base`, `aui`, `stc`, `propgrid`), fmt, SQLite, LodePNG und HIDAPI installieren.
2. Im Projektstammverzeichnis ausführen:

```bash
cmake -S . -B build            # -G Ninja anhängen, um Ninja zu verwenden
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Für einen Build ohne HIDAPI `-DWINDOWSADDON_USE_HIDAPI=OFF` an CMake übergeben.

</details>

<details>
<summary><b>Visual-Studio-Solution (MSBuild)</b></summary>

Die Visual-Studio-Solution steht weiterhin als `WindowsAddon.sln` für die MSBuild-basierte Entwicklung bereit. Zuerst `vcpkg integrate install` ausführen; das Projekt verwendet standardmäßig die Bibliotheksnamen von wxWidgets 3.3. Bei einer manuellen wxWidgets-3.2-Installation die MSBuild-Eigenschaft `WxLibraryVersion` auf `32` setzen.

Für die Konfiguration `Release_Bsec|x64` die Umgebungs- oder MSBuild-Eigenschaft `BSEC_LIBRARY_DIR` auf das Verzeichnis mit der separat beschafften `BSECLibrary64.lib` setzen.

Verfügbare Build-Konfigurationen:

- **x86**: Debug, Release, Static Release, Release_Bsec (mit Bosch-BSEC-Bibliothek)
- **x64**: Debug, Release, Static Release, Release_Bsec (mit Bosch-BSEC-Bibliothek)

Zum Debuggen in Visual Studio (x64 Debug, Release oder Release_Bsec) Symlinks auf die Konfigurationsdateien anlegen:

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
<summary><b>CMake-Optionen</b></summary>

| Option | Wirkung |
| --- | --- |
| `-DWINDOWSADDON_BUILD_APP=OFF` | Überspringt die wxWidgets-Anwendung und baut nur die GUI-freien Bibliotheken und ihre Tests |
| `-DWINDOWSADDON_BUILD_EXTERNAL_TESTS=OFF` | Schließt die Tests aus, die SQLite/Boost benötigen; Standard, wenn die Anwendung deaktiviert ist |
| `-DWINDOWSADDON_WARNINGS_AS_ERRORS=ON` | Aktiviert `/WX` (MSVC) bzw. `-Werror` (GCC/Clang) für die projekteigenen Targets |
| `-DWINDOWSADDON_ENABLE_SANITIZERS=ON` | Aktiviert ASan und UBSan für GCC/Clang-Kern-Builds |
| `-DWINDOWSADDON_ENABLE_COVERAGE=ON` | Aktiviert die Coverage-Instrumentierung für GCC/Clang |
| `-DWINDOWSADDON_USE_HIDAPI=OFF` | Baut ohne HID-Zugriff |
| `-DWINDOWSADDON_USE_OPENCV=ON` | Aktiviert OpenCV-Unterstützung; OpenCV muss separat installiert werden, es ist nicht Teil der `vcpkg.json` |
| `-DWINDOWSADDON_USE_BSEC=ON` | Aktiviert Bosch BSEC für 64-Bit-Windows. Die proprietäre Binärdatei separat über `-DWINDOWSADDON_BSEC_LIBRARY="C:/pfad/zu/BSECLibrary64.lib"` angeben |
| `-DWINDOWSADDON_BUILD_FUZZERS=ON` | Baut die Clang/libFuzzer-Targets für Netzwerk- und Protokoll-Parser; benötigt `-DWINDOWSADDON_ENABLE_SANITIZERS=ON` |
| `-DBUILD_TESTING=OFF` | Überspringt die Unit-Test-Targets |

</details>

<a id="de-tests"></a>
## Tests

| CTest-Name | Benötigt | Abdeckung |
| --- | --- | --- |
| `WindowsAddon.Core` | nichts | die abhängigkeitsfreie Logik: CAN-Codecs, ISO-TP, Modbus-Client und Präsentationsmodelle, Sensor- und TCP-Parser, das Settings-Dokument, Backup-Kern, Skriptstart, String- und Makro-Helfer |
| `WindowsAddon.External` | SQLite, Boost | die XML-Konfigurationslader und den Speicher der Arbeitszeiterfassung |

Beide Suiten verwenden das kleine, im Repository enthaltene Test-Framework in `tests/TestFramework.hpp`; GoogleTest oder Catch werden nicht benötigt. Zwei libFuzzer-Targets (`WindowsAddonNetworkMessageFuzzer`, `WindowsAddonProtocolParsersFuzzer`) decken den TCP-Nachrichtenparser sowie die CAN-, ISO-TP-, Modbus- und Sensor-Parser ab.

Automatisierte End-to-End- oder GUI-Tests gibt es derzeit nicht; die wxWidgets-Anwendung wird in der CI nur kompiliert, nicht ausgeführt.

<a id="de-ci"></a>
## Continuous Integration

GitHub Actions baut den abhängigkeitsfreien Kern unter Windows mit MSVC und unter Ubuntu mit GCC und Clang. Alle drei Builds behandeln Projektwarnungen als Fehler. Ein separater Clang-Job führt die Kern-Tests unter ASan und UBSan aus, und ein libFuzzer-Smoke-Job baut beide Fuzz-Targets und lässt jeweils eine kurze Kampagne laufen. Der vollständige Windows-Release-Job stellt einen vcpkg-Binärcache wieder her, baut die Desktop-Anwendung, führt beide Test-Suiten aus und veröffentlicht die ausführbare Datei samt DLLs als Workflow-Artefakt.

CodeQL analysiert die vollständige Windows-Anwendung wöchentlich und bei jeder Änderung. Der Coverage-Job veröffentlicht immer `coverage.xml` als Workflow-Artefakt und lädt es nicht-blockierend per OIDC zu Codecov hoch; das Repository in Codecov verbinden, um das Coverage-Badge zu aktivieren.

Cppcheck läuft bei jeder Änderung als zweite, günstigere statische Analyse. Es braucht keine der Projektabhängigkeiten, der Job ist daher ein schlichtes `apt-get install cppcheck` gefolgt von einem zweiminütigen Scan von `src/`, `libs/`, `tests/` und `fuzz/`; `--library=wxwidgets` sorgt dafür, dass die Event-Table-Makros in den GUI-Quellen geparst werden, sonst würden diese Dateien stillschweigend übersprungen. Nur Befunde der Stufe `error` lassen den Build fehlschlagen — alles andere ist informativ, mit den Zählern in der Job-Zusammenfassung und dem vollständigen `cppcheck-report.txt` als Artefakt. Geprüfte und verworfene Befunde stehen mit Begründung in [`.cppcheck-suppressions`](.cppcheck-suppressions), damit die Datei ein Protokoll von Entscheidungen bleibt und keine Aufgabenliste.

<a id="de-konfiguration"></a>
## Konfigurationsdateien

Die Anwendung schreibt beim ersten Start eine `settings.ini` mit Standardwerten, wenn keine existiert. Die versionierte [`settings.ini`](settings.ini) dokumentiert die Schlüssel mit Inline-Kommentaren.

| Datei | Zweck |
| --- | --- |
| `settings.ini` | Hauptkonfiguration: aktivierte Funktionen, Intervalle, Makros, Backup-Jobs |
| `Cmds.xml` | Button-Definitionen des Command Executors |
| `Modbus.xml` | Modbus-Registerlayout (oder eine in `settings.ini` gewählte JSON-Gerätedatei) |
| `TxList.xml` / `RxList.xml` | Zu sendende CAN-Frames und erwartete Empfangs-Frames |
| `FrameMapping.xml` | CAN-Frame- und Feld-Mapping, erforderlich für CAN-Skripte |
| `DataSender.xml` | Definitionen für den periodischen Datensender |
| `Alarms.xml` | Alarmdefinitionen für den Alarm-Handler |
| `DidList.xml` | UDS-DID-Definitionen für den DID-Reader/-Writer (nicht mitgeliefert, bei Bedarf anlegen) |

<a id="de-fortgeschritten"></a>
## Fortgeschrittene Themen

<details>
<summary><b>ECU-Simulation</b></summary>

Ein einfaches Steuergerät lässt sich durch direkte Änderung des Quellcodes simulieren. Für flexiblere Simulationen besser Skripte schreiben. Folgenden Ausschnitt in die Funktion `OnFrameReceived` in `CanEntryHandler.cpp` einfügen:

```c
if(data_len == 8 && data[0] == 0x03 && data[1] == 0x22 && (data[2] == 0xF1 || data[2] == 0xF0))  /* Datenlänge: 8, 22 = READ DID, DID-ID LSB: AA */
{
    uint32_t send_id = frame_id;
    if(frame_id == 0xAAA)
        send_id = 0xBBB;
    else if(frame_id == 0xCCC)
        send_id = 0xDDD;

    uint8_t byte_buffer[8] = { 0x04, 0x62 };  /* Datenlänge: 4, 0x62 = DID-Lesen (Service 22) erfolgreich */
    memcpy(&byte_buffer[2], &data[2], 6);  /* Quell-DID aus dem Empfangspuffer kopieren */
    m_CanTransport.Send(send_id, byte_buffer);
}
```

</details>

<details>
<summary><b>Skripte für den CAN-Bus</b></summary>

Skripte werden im Reiter „Script" des CAN-Panels ausgeführt. CAN-Frames und ihre Felder müssen in `FrameMapping.xml` gemappt sein, sonst funktionieren Skripte nicht. Die Skriptunterstützung ist noch früh im Stadium; Fehler sind möglich.

| Befehl | Wirkung |
| --- | --- |
| `WaitForFrame <Frame-Name> <Timeout ms>` | Wartet, bis ein bestimmter Frame mit den angegebenen Daten auf dem Bus erscheint |
| `SetFrameFieldRaw <Frame-Name> <rohe CAN-Daten>` | Setzt ein Frame-Feld im rohen Byte-Format |
| `SetFrameField <Feldname> <Wert>` | Setzt den Wert eines CAN-Frame-Felds über seinen Namen (Feldname, nicht Frame-Name) |
| `SendFrame <Frame-Name>` | Sendet den benannten CAN-Frame; er muss in `FrameMapping.xml` gemappt sein |
| `Sleep <Verzögerung in ms>` | Pausiert die Skriptausführung für die angegebene Dauer |

```c
// Warten, bis eine DID-Leseanfrage (Service 22) für DID 2000 auf dem Bus erscheint
SetFrameFieldRaw XXX_to_DTOOL 0x03222000AAAAAAA
WaitForFrame DTOOL_TO_XXX 120000

// VehicleHasClutch im Frame VEHICLE_INFO auf 1 setzen
SetFrameField VehicleHasClutch 0x1
SendFrame VEHICLE_INFO
Sleep 500
```

</details>

<a id="de-abhaengigkeiten"></a>
## Abhängigkeiten

**Erforderliche externe Abhängigkeiten** — [Boost](https://www.boost.org/), [wxWidgets](https://www.wxwidgets.org/), [fmt](https://fmt.dev/), [SQLite](https://www.sqlite.org/), [LodePNG](https://lodev.org/lodepng/), [HIDAPI](https://github.com/libusb/hidapi).

<details>
<summary><b>Mitgelieferte und optionale Bibliotheken</b></summary>

- [lodepng](https://lodev.org/lodepng/ "lodepng Homepage")
- [sqlite3](https://www.sqlite.org/index.html "sqlite3 Homepage")
- [enumser](http://www.naughter.com/enumser.html "enumser Homepage")
- [sha256](https://github.com/B-Con/crypto-algorithms "sha256 Homepage")
- [AsyncSerial](https://github.com/fedetft/serial-port "AsyncSerial Homepage")
- [Chart.js](https://www.chartjs.org/ "Chart.js Homepage")
- [bitfield](https://github.com/openxc/bitfield-c "bitfield Homepage")
- [isotp](https://github.com/lishen2/isotp-c "iso-tp Homepage")
- [BSEC](https://www.bosch-sensortec.com/software-tools/software/bsec/ "Bosch BSEC Homepage") — proprietär, separat zu beschaffen
- [opencv](https://opencv.org/ "OpenCV Homepage") — optional

</details>

<a id="de-hardware"></a>
## Hardware

Einige Funktionen benötigen externe Hardware. Die Firmware der Boards wird in eigenen Repositories gepflegt:

| Funktion | Hardware | Firmware |
| --- | --- | --- |
| Zweittastatur | Nucleo L495ZG + UART-TTL-zu-USB-Adapter | [UsbHost](https://github.com/kurta999/UsbHost) |
| Sensoren | STM32-Sensorboard | [AirQualitySensors](https://github.com/kurta999/AirQualitySensors) |
| CAN | NUCLEO-G474RE + SN65HVD230-Transceiver | [CanUsbTransceiver](https://github.com/kurta999/CanUsbTransceiver) |
| CAN (Alternative) | [Lawicel CAN USB](https://www.canusb.com/products/canusb/) | — |

<a id="de-mitwirken"></a>
## Mitwirken

Pull Requests, Fehlermeldungen und Vorschläge sind willkommen. Ein Issue oder einen Pull Request auf GitHub eröffnen; die Build- und Testbefehle oben sind genau das, was die CI ausführt — eine Änderung, die lokal durchläuft, ist reif für ein Review.

Wenn dir das Projekt nützt und du kannst, freue ich mich über eine kleine Spende per PayPal (`nmsstulaj@gmail.com`).

<a id="de-lizenz"></a>
## Lizenz

Veröffentlicht unter der [MIT-Lizenz](LICENSE.md).

<div align="center">

🇬🇧 [English](#english) &nbsp;·&nbsp; 🇩🇪 **Deutsch** &nbsp;·&nbsp; [↑ nach oben](#english)

</div>
