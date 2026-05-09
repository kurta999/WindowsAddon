A personal open-source project aimed at improving daily computer usage, particularly for programming and testing workflows. It bundles utilities I built to reduce repetitive tasks and speed up daily work, and is shared freely in case others find it useful.

Feel free to open a Pull Request if you want to use or reuse any part of the project, or if you have a question or suggestion. Bugs and anti-patterns may occur — this is a solo side project maintained alongside a full-time job, so not everything gets retested after every change. Functionality takes priority over perfection.

An unfinished Qt port of this application is available at https://github.com/kurta999/QustomKeyboard. A full port to Qt is not planned.

If the project is useful to you and you are able, please consider a small donation via PayPal: nmsstulaj@gmail.com

# Quick overview
### 1. For Automotive development:
1. **CAN-USB Transceiver** - Send and receive standard and ISO-TP CAN frames over CAN bus via USB — see details below
2. **CAN Script handler** - Execute test scripts by setting specific frames and sending them to the bus automatically
3. **UDS DID Reader & Writer** - Read and write UDS DIDs via GUI. DIDs must be defined in DidList.xml and can optionally be cached locally

### 2. For General development:
1. **Modbus Master** - Simple Modbus master for polling Modbus slave devices with GUI support. Coils, Discrete Input, Holding and Input registers supported over Serial and TCP/IP. Supported register types: (u)int16_t, (u)int32_t, (u)int64_t, float, double. Includes logging and error handling.
2. **Command Executor** - Bind commands to GUI buttons with parameter support, executed on button click — see details below
3. **TerminalHotkey** - System-wide terminal hotkey, similar to Linux. Can be launched from Windows Explorer using the current path, or from the Desktop
4. **File Explorer Opener** - Open a file explorer window by sending a specific TCP packet to this application
5. **TimeTracker** - Track work hours by starting and stopping a timer via GUI or a dedicated keyboard key. Work entries can be edited in the GUI and are stored in an SQLite database

### 3. For Personal use:
1. **CustomMacro** - Connect a second keyboard and bind macros to its keys, with full GUI support for macro editing and a macro recorder
2. **Sensors** - TCP backend for sensors with an SQLite database for storing measurements and an HTTP server for viewing graphs. Graphs are accessible at http://localhost:2005/graphs by default. STM32 sensor source code: https://github.com/kurta999/AirQualitySensors
3. **Backend for Corsair's G Keys** - Bind macros to Corsair G keys without installing iCUE
4. **AntiLock** - Prevents Windows from locking due to inactivity by periodically pressing SCROLL LOCK and moving the mouse. Useful for workstations where idle timeout cannot be disabled
5. **AntiNumLock** - Prevents NumLock from being disabled; re-enables it immediately if turned off
6. **CPU Power Saver** - Reduces CPU frequency after a configured idle period to save power. Automatically restores full frequency when load exceeds a configured threshold
7. **ScreenshotSaver** - Saves the current clipboard screenshot to a .png file
8. **DirectoryBackup** - Backup folders to one or more destinations from the tray menu, with checksum and compression support
9. **Filesystem browser** - Lists files and directories recursively, sorted by size from a given path — useful for identifying storage bloat
10. **Alarm Handler** - Define alarms in Alarms.xml and trigger them with a key on the secondary keyboard. A popup dialog prompts for a delay, after which the configured alarm action is executed
11. **CryptoPrice** - Displays live ETH & BTC buy/sell prices from Coinbase on the main panel. Disabled by default; enable by setting a non-zero CryptoPriceUpdate interval in settings.ini

# In-depth feature details
### 1. For Automotive development:
1. **CAN-USB Transceiver** - Requires a [LAWICEL CAN USB](https://www.canusb.com/products/canusb/ "Lawicel CAN USB's Homepage") adapter, or a NUCLEO-G474RE board with a UART-TTL to USB adapter and a Waveshare SN65HVD230 3.3V CAN transceiver (or any equivalent hardware that converts TTL signals to CAN). Supports standard, extended, and ISO-TP (ISO 15765-2) CAN frames — useful for sending and receiving UDS frames. Includes frame logging, search, and a bit/byte editor for easy frame manipulation via GUI. Firmware for the Nucleo board: https://github.com/kurta999/CanUsbTransceiver. The default baud rate is 500 kbit/s; changing it is not yet implemented.

2. **CAN Script handler** - Execute test scripts by setting specific frames and sending them to the bus automatically

3. **UDS DID Reader & Writer** - Supported DID types: uint8_t, uint16_t, uint32_t, uint64_t, string, bytearray. Strings and byte arrays are padded when their length is less than the predefined length, and truncated if longer.

### 2. For General development:
1. **Modbus Master** - Registers must be configured in Modbus.xml. They can be edited on the fly in the GUI, and the communication log can be viewed and exported to a .csv file.

2. **Command Executor** - Commands are configured in Cmds.xml or directly in the "CMD Executor" panel. Each command is executed via Windows CreateProcess when its button is clicked. This is particularly useful for frequently used command-line calls — no more copy-pasting or remembering aliases. Each command supports up to 16 variable parameters, which can be edited before execution by middle-clicking the button. Button appearance (font, color, bold) and duplication are fully configurable via the GUI.

3. **File Explorer Opener** - Useful when working with VirtualBox or WSL with Samba-mounted directories. Map the guest OS network share in Windows (default drive letter is Z:, configurable via "SharedDriveLetter" in settings.ini), enable TCP_Backend in settings.ini, then run the following on the guest OS to open the current directory in Windows Explorer:
   ```
   echo expw$(pwd) | netcat <host IP> <TCP_Backend port>
   ```
   Creating a shell alias for this command is recommended.

### 3. For Personal use:

1. **CustomMacro** - Requires an external Nucleo L495ZG board with a UART-TTL to USB adapter for relaying key presses to the PC, and optionally a USB-A to Micro-USB adapter if the keyboard uses a USB-A port. The Nucleo acts as a USB host, receives key presses from the connected keyboard, and forwards them to the PC via UART. Keyboard hooking in Windows was considered but caused debugger crashes in MSVC; writing a custom kernel driver was the other option but was out of scope. Macros are configured in the settings page or directly in settings.ini, and can be bound to a global profile or per application. Key combinations are supported. Firmware for the Nucleo board: https://github.com/kurta999/UsbHost\  
This feature also supports Corsair G keys without iCUE — see the "Backend for Corsair's G Keys" section.\
\
<span style="color:red">Supported macro types:</span>\
**BIND_NAME[binding name]** = Sets the macro name. Must be the first entry in a macro definition  
**KEY_TYPE[text]** = Types the given text by pressing and releasing keys in sequence  
**KEY_SEQ[CTRL+C]** = Presses all given keys in sequence and releases them — ideal for keyboard shortcuts  
**DELAY[time in ms]** = Waits for the given number of milliseconds  
**DELAY[min ms - max ms]** = Waits for a random duration between min and max milliseconds  
**MOUSE_MOVE[x,y]** = Moves the mouse to the given coordinates  
**MOUSE_INTERPOLATE[x,y]** = Moves the mouse to the given coordinates with interpolation  
**MOUSE_PRESS[key]** = Presses the given mouse button  
**MOUSE_RELEASE[key]** = Releases the given mouse button  
**MOUSE_CLICK[key]** = Clicks (presses and releases) the given mouse button  
**BASH[key]** = Executes the given command(s) in a terminal window (terminal remains visible)  
**CMD[key]** = Executes the given command(s) without showing a terminal window  
**CMD_XML[PageName+CommandName]** = Executes a predefined command from Cmds.xml  
**CMD_FG[app_name.exe,Window title name]** = Brings the specified application window to the foreground  
**CMD_IMG[path_to_image,offset x,offset y]** = Scans the screen for the given image and clicks on it if found  
\
<span style="color:red">Examples:</span>\
G1 = BIND_NAME[uint8_t] KEY_TYPE[uint8_t]\
G4 = BIND_NAME[reddit CPP button] CMD_FG[chrome.exe,C++] CMD_IMG[test_image.png,3,3]

2. **Sensors** - The backend is a simple TCP server that receives measurements from connected sensors. The average of measurements within a configured integration period is stored in an SQLite database. Graphs are generated from the last 30 measurements plus daily and weekly averages by default (configurable in settings.ini). The database updates every 10 minutes, or manually via the "Generate graphs" button. Graphs are accessible at `your_local_ip:2005/graphs` and can be viewed from any device on the network, including a phone.

3. **Backend for Corsair's G Keys** - iCUE's memory usage can sometimes grow to 500 MB, which is excessive for a background macro application. This feature provides an alternative: G key presses are received via a simple HID API and routed through the CustomMacro system, without iCUE installed. Supported devices: K95 RGB (18 G keys) and K95 RGB Platinum.

4. **AntiLock** - Prevents Windows from locking due to inactivity by periodically pressing SCROLL LOCK and moving the mouse at a configured interval. Useful for workstations where idle timeout cannot be disabled or where activity is monitored.

5. **AntiNumLock** - Prevents NumLock from being disabled; re-enables it immediately if turned off.

6. **CPU Power Saver** - Reduces CPU frequency after a configured idle period to save power. For example, reducing an overclocked i7-10700K to 800–1200 MHz during idle can save 10–15W per hour. Frequency is automatically restored when median CPU usage exceeds the configured threshold, and limiting resumes once load drops back below the minimum.

7. **ScreenshotSaver** - Press the configured screenshot key (F12 by default, on the secondary keyboard) to save the current clipboard image to the Screenshots folder as a .png file. The save path is configurable.

8. **DirectoryBackup** - Configure backup jobs in the settings page or in settings.ini. Configured backups appear in the tray menu — click one to start the backup. Supports an ignore list and SHA-256 checksums for integrity verification.

9. **CryptoPrice** - Fetches live ETH & BTC buy/sell prices from Coinbase and displays them on the main panel. Disabled by default; enable by setting a non-zero CryptoPriceUpdate interval in settings.ini.

## Libraries
- [lodepng](https://lodev.org/lodepng/ "lodepng's Homepage")
- [sqlite3](https://www.sqlite.org/index.html "sqlite3's Homepage")
- [enumser](http://www.naughter.com/enumser.html "enumser's Homepage")
- [sha256](https://github.com/B-Con/crypto-algorithms "sha256's Homepage")
- [AsyncSerial](https://github.com/fedetft/serial-port "AsyncSerial's Homepage")
- [Chart.js](https://www.chartjs.org/ "Charts.js' Homepage")
- [bitfield](https://github.com/openxc/bitfield-c "bitfield's Homepage")
- [isotp](https://github.com/lishen2/isotp-c "iso-tp's Homepage")
- [BSEC](https://www.bosch-sensortec.com/software-tools/software/bsec/ "Bosch's BSEC Homepage")
- [opencv](https://opencv.org/ "OpenCV's Homepage")

Required external dependencies:
- [Boost](https://www.boost.org/ "Boost's Homepage")
- [wxWidgets](https://www.wxwidgets.org/ "wxWidgets' Homepage")
- [HIDAPI](https://github.com/libusb/hidapi "HIDAPI's Homepage")

Required external hardware:
- Second keyboard & Sensors: [AirQualitySensors](https://github.com/kurta999/AirQualitySensors "AirQualitySensors + UsbHost")
- Second keyboard only: [UsbHost](https://github.com/kurta999/UsbHost "UsbHost")
- CAN: [STM32 CAN USB](https://github.com/kurta999/CanUsbTransceiver "STM32 CAN USB") OR [Lawicel CAN USB](https://www.canusb.com/products/canusb/ "Lawicel CAN USB")

## Building

**Windows**

1. Install Visual Studio 2022 and vcpkg, then install the required libraries:
```
vcpkg install boost wxwidgets opencv sqlite3 lodepng hidapi
```

> **Note:** If you are concerned about keylogging due to the HIDAPI dependency, you can remove the CorsairHID component and HIDAPI from the project and recompile.

2. Open `CustomKeyboard.sln` and build the desired configuration.

Available build configurations:
- x86: Debug, Release, Static Release, Release_BSec (with Bosch BSec library)
- x64: Debug, Release, Static Release, Release_BSec (with Bosch BSec library)

CMake support for Windows is available but not actively maintained — use the `.sln` file instead. Example CMake command:
```
cmake .. -DCMAKE_PREFIX_PATH="C:\GIT_Local\CustomKeyboard\fmt-8.1.1\build;C:\Program Files\boost\boost_1_83_0\stage\lib\cmake" -DwxWidgets_ROOT_DIR=C:\wxWidgets-3.2.2 -DBoost_INCLUDE_DIR="C:\Program Files\boost\boost_1_83_0" -DBoost_LIBRARY_DIR="C:\Program Files\boost\boost_1_83_0\stage\lib" -DFMT_LIB_DIR=C:\GIT_Local\CustomKeyboard\fmt-8.1.1 -G "Visual Studio 17 2022"
```

When debugging under Visual Studio (x64 Debug, Release, or Release_BSec), create symlinks to the config files:
```
mklink "TxList.xml" ..\..\TxList.xml
mklink "RxList.xml" ..\..\RxList.xml
mklink "FrameMapping.xml" ..\..\FrameMapping.xml
mklink "Modbus.xml" ..\..\Modbus.xml
mklink "DataSender.xml" ..\..\DataSender.xml
mklink "settings.ini" ..\..\settings.ini
```

**Linux**

The project uses C++20 features — use a recent compiler (GCC 12+ or Clang 15+ recommended).

1. Install dependencies: Boost 1.83.0, wxWidgets 3.2.2, fmt 8.0.0, hidapi
2. Run the following commands from the project root:

To build without HIDAPI support, pass `-DUSE_HIDAPI=false` to CMake (or set it via CMake GUI).

With Make:
```
mkdir build
cd build
cmake ..
make -j$(nproc)
```

With Ninja:
```
mkdir build
cd build
cmake -GNinja ..
ninja
```

## Advanced topics
### ECU Simulation

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
        CanSerialPort::Get()->AddToTxQueue(send_id, sizeof(byte_buffer), byte_buffer);
    }
```

### Scripts for CAN bus

Scripts are executed from the Script tab in the CAN panel. CAN frames and their fields must be mapped in FrameMapping.xml, otherwise scripts will not work. Script support is in an early stage and bugs may occur.
```c
WaitForFrame <frame name> <timeout ms> - Waits until a specific frame with the given data appears on the CAN bus
SetFrameFieldRaw <frame name> <raw CAN data> - Sets a frame field in raw byte format
SetFrameField <field name> <value> - Sets a CAN frame field value by name (note: field name, not frame name)
SendFrame <frame name> - Sends the named CAN frame. The frame must be mapped in FrameMapping.xml
Sleep <delay in milliseconds> - Pauses script execution for the given duration
```

```c
// Wait until a DID read request (service 22) for DID 2000 appears on the bus
SetFrameFieldRaw XXX_to_DTOOL 0x03222000AAAAAAA
WaitForFrame DTOOL_TO_XXX 120000    

// Set VehicleHasClutch to 1 in the VEHICLE_INFO frame
SetFrameField VehicleHasClutch 0x1
SendFrame VEHICLE_INFO
Sleep 500
```

## Screenshots
**Main Page**

![Alt text](/github_screens/main_page.png?raw=true "Main page")

**Sensors**

![Alt text](/github_screens/sensors_js_graph.png?raw=true "Temperature graph for last week")

**CAN-USB Transceiver**

![Alt text](/github_screens/can_usb.png?raw=true "CAN-USB GUI")

**CAN-USB Frame mapping**

![Alt text](/github_screens/can_bit_editor.png?raw=true "CAN-USB Frame mapping")

**CAN-USB UDS Frame sending over ISO-TP protocol**

![Alt text](/github_screens/iso_tp_message.png?raw=true "UDS Frame sending over ISO-TP protocol")

**UDS DID Handler (Both writing & reading)**

![Alt text](/github_screens/did_readerwriter.png?raw=true "UDS DID Handler")

**Modbus Master**

![Alt text](/github_screens/modbus_master.png?raw=true "Modbus Master")

**Worktime Tracker**

![Alt text](/github_screens/timesheet.png?raw=true "Worktime tracker")

**Command executor**

![Alt text](/github_screens/cmd_executor.png?raw=true "Execute command by clicking its button")

**Command executor params**

![Alt text](/github_screens/cmd_executor_params.png?raw=true "Execute command with custom parameters")

**Filesystem browser**

![Alt text](/github_screens/file_browser.png?raw=true "Filesystem browser")

**Configuration**

![Alt text](/github_screens/config_main_page.png?raw=true "Configuration")

**Macro editor**

![Alt text](/github_screens/macro_editor_1.png?raw=true "Macro editor sample 1")
![Alt text](/github_screens/macro_editor_2.png?raw=true "Macro editor sample 2")
![Alt text](/github_screens/macro_add.png?raw=true "Macro editor add macro")

**Backup**

![Alt text](/github_screens/backup_config.png?raw=true "Backup page")
![Alt text](/github_screens/backup_progress.png?raw=true "Backup in progress")

**Log**

![Alt text](/github_screens/log.png?raw=true "Log")

### Linux
**Main Page**

![Alt text](/github_screens/main_page_linux.png?raw=true "Main page in linux build under Ubuntu")

**MTA -> SA-MP Map Converter**

![Alt text](/github_screens/map_converter_linux.png?raw=true "MTA -> SA-MP Map Converter in linux build under Ubuntu")
