cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(WindowsAddon VERSION 1.7 LANGUAGES C CXX)

option(WINDOWSADDON_BUILD_APP "Build the WindowsAddon desktop application" ON)
option(WINDOWSADDON_USE_HIDAPI "Enable HIDAPI support" ON)
option(WINDOWSADDON_USE_OPENCV "Enable OpenCV-based image recognition" OFF)
option(WINDOWSADDON_USE_BSEC "Enable an externally supplied Bosch BSEC library (64-bit Windows only)" OFF)
option(WINDOWSADDON_BUILD_EXTERNAL_TESTS "Build tests that require SQLite and Boost" ${WINDOWSADDON_BUILD_APP})
option(WINDOWSADDON_BUILD_FUZZERS "Build libFuzzer targets for network and protocol parsers" OFF)
option(WINDOWSADDON_WARNINGS_AS_ERRORS "Treat warnings from WindowsAddon code as errors" OFF)
option(WINDOWSADDON_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(WINDOWSADDON_ENABLE_COVERAGE "Enable coverage instrumentation" OFF)

set(WINDOWSADDON_BSEC_LIBRARY "" CACHE FILEPATH
    "Path to the externally supplied 64-bit BSECLibrary64.lib")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

if(WINDOWSADDON_ENABLE_SANITIZERS AND WINDOWSADDON_ENABLE_COVERAGE)
    message(FATAL_ERROR "Sanitizers and coverage cannot be enabled in the same build")
endif()

if((WINDOWSADDON_ENABLE_SANITIZERS OR WINDOWSADDON_ENABLE_COVERAGE) AND
   NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "Sanitizers and coverage require GCC or Clang")
endif()

if(WINDOWSADDON_BUILD_FUZZERS AND
   (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT WINDOWSADDON_ENABLE_SANITIZERS))
    message(FATAL_ERROR
        "Fuzzers require Clang and -DWINDOWSADDON_ENABLE_SANITIZERS=ON")
endif()

# LLVM's Windows libFuzzer runtime is built against the static release CRT.
# Keep every target in a Windows fuzz build ABI-compatible with that runtime.
if(WINDOWSADDON_BUILD_FUZZERS AND WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    add_compile_definitions(_ITERATOR_DEBUG_LEVEL=0)
endif()

if(WINDOWSADDON_BUILD_APP OR (BUILD_TESTING AND WINDOWSADDON_BUILD_EXTERNAL_TESTS))
    find_package(unofficial-sqlite3 CONFIG QUIET)
    if(TARGET unofficial::sqlite3::sqlite3)
        set(WINDOWSADDON_SQLITE_TARGET unofficial::sqlite3::sqlite3)
    else()
        find_package(SQLite3 REQUIRED)
        set(WINDOWSADDON_SQLITE_TARGET SQLite::SQLite3)
    endif()
endif()

if(WINDOWSADDON_BUILD_APP)
    find_package(Boost 1.74 REQUIRED COMPONENTS system program_options)
elseif(BUILD_TESTING AND WINDOWSADDON_BUILD_EXTERNAL_TESTS)
    find_package(Boost 1.74 REQUIRED)
endif()

function(windowsaddon_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
            $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        )
        if(WINDOWSADDON_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(WINDOWSADDON_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

function(windowsaddon_enable_instrumentation target)
    if(WINDOWSADDON_ENABLE_SANITIZERS)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    elseif(WINDOWSADDON_ENABLE_COVERAGE)
        target_compile_options(${target} PRIVATE --coverage -O0 -g)
        target_link_options(${target} PRIVATE --coverage)
    endif()
endfunction()

function(windowsaddon_configure_own_target target)
    windowsaddon_enable_warnings(${target})
    windowsaddon_enable_instrumentation(${target})
endfunction()

# libs/ and a few sources are carried over verbatim from upstream, where these
# names are spelled with the WindowsHelper prefix. Forwarding here keeps those
# files byte-identical so future syncs stay a plain copy.
function(windowshelper_configure_own_target target)
    windowsaddon_configure_own_target(${target})
endfunction()

function(windowsaddon_allow_legacy_gui_warnings target)
    if(MSVC)
        # wxWidgets event handlers intentionally have fixed callback signatures,
        # and the legacy GUI still contains some side-effect-only locals/shadowing.
        target_compile_options(${target} PRIVATE /wd4100 /wd4189 /wd4456 /wd4458)
    endif()
endfunction()

add_subdirectory(libs)

if(BUILD_TESTING OR WINDOWSADDON_BUILD_APP OR WINDOWSADDON_BUILD_FUZZERS)
    add_library(windowsaddon_protocols STATIC
        src/CanCodecs.cpp
        src/MacroKeyFrame.cpp
        src/ModbusClient.cpp
        src/ModbusItemRender.cpp
        src/SensorDataParser.cpp
        src/TcpMessageParser.cpp
    )
    target_include_directories(windowsaddon_protocols PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(windowsaddon_protocols PUBLIC windowshelper_protocol)
    windowsaddon_configure_own_target(windowsaddon_protocols)

    add_library(windowsaddon_automation STATIC
        src/automation/CommandTextResolver.cpp
        src/automation/ScriptCommandResolver.cpp
        src/automation/ScriptLaunchService.cpp
        src/DirectoryBackupCore.cpp
        src/DirectoryUsage.cpp
        src/SettingsIniDocument.cpp
        src/CryptoPriceParser.cpp
        src/SettingsSchema.cpp
        src/StringToCEscaper.cpp
        src/TimeTrackerLogic.cpp
    )
    target_include_directories(windowsaddon_automation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    windowsaddon_configure_own_target(windowsaddon_automation)

    add_library(windowsaddon_platform STATIC
        src/platform/StandardFileSystem.cpp
        src/platform/SystemClock.cpp
        src/platform/SystemCommandRunner.cpp
    )
    target_include_directories(windowsaddon_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(windowsaddon_platform PRIVATE
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN;NOMINMAX>
    )
    windowsaddon_configure_own_target(windowsaddon_platform)

    add_library(WindowsAddon::Protocols ALIAS windowsaddon_protocols)
    add_library(WindowsAddon::Automation ALIAS windowsaddon_automation)
    add_library(WindowsAddon::Platform ALIAS windowsaddon_platform)
endif()

if(WINDOWSADDON_BUILD_APP OR (BUILD_TESTING AND WINDOWSADDON_BUILD_EXTERNAL_TESTS))
    add_library(windowsaddon_persistence STATIC
        src/TimeTracker.cpp
        src/TimeTrackerStorage.cpp
    )
    target_include_directories(windowsaddon_persistence PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(windowsaddon_persistence PUBLIC ${WINDOWSADDON_SQLITE_TARGET})
    windowsaddon_configure_own_target(windowsaddon_persistence)
    add_library(WindowsAddon::Persistence ALIAS windowsaddon_persistence)
endif()

if(BUILD_TESTING)
    add_library(windowsaddon_test_framework STATIC tests/TestFramework.cpp)
    target_include_directories(windowsaddon_test_framework PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    windowsaddon_configure_own_target(windowsaddon_test_framework)

    add_executable(WindowsAddonCoreTests
        tests/BitfieldTests.cpp
        tests/CanCodecTests.cpp
        tests/CommandTextResolverTests.cpp
        tests/DirectoryBackupTests.cpp
        tests/ModbusEvolutionTests.cpp
        tests/ModbusClientTests.cpp
        tests/ObserverTests.cpp
        tests/PresentationModelTests.cpp
        tests/ScriptLaunchServiceTests.cpp
        tests/SingletonTests.cpp
        tests/IsoTpTests.cpp
        tests/MeasurementTests.cpp
        tests/SensorDataParserTests.cpp
        tests/SettingsIniDocumentTests.cpp
        tests/Sha256Tests.cpp
        tests/StringEscaperTests.cpp
        tests/TcpMessageParserTests.cpp
    )
    target_include_directories(WindowsAddonCoreTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    target_link_libraries(WindowsAddonCoreTests PRIVATE
        WindowsAddon::Protocols
        WindowsAddon::Automation
        WindowsAddon::Platform
        windowsaddon_test_framework
    )
    windowsaddon_configure_own_target(WindowsAddonCoreTests)
    add_test(NAME WindowsAddon.Core COMMAND WindowsAddonCoreTests)

    if(WINDOWSADDON_BUILD_EXTERNAL_TESTS)
        add_executable(WindowsAddonExternalTests
            tests/TimeTrackerTests.cpp
            tests/TimeTrackerStorageTests.cpp
            tests/XmlConfigurationTests.cpp
            src/XmlConfigurations.cpp
        )
        target_include_directories(WindowsAddonExternalTests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        )
        target_link_libraries(WindowsAddonExternalTests PRIVATE
            WindowsAddon::Persistence
            WindowsAddon::Automation
            windowsaddon_test_framework
            Boost::headers
        )
        windowsaddon_configure_own_target(WindowsAddonExternalTests)
        add_test(NAME WindowsAddon.External COMMAND WindowsAddonExternalTests)
    endif()
endif()

if(WINDOWSADDON_BUILD_FUZZERS)
    foreach(fuzzer IN ITEMS NetworkMessage ProtocolParsers)
        add_executable(WindowsAddon${fuzzer}Fuzzer "fuzz/${fuzzer}Fuzz.cpp")
        target_link_libraries(WindowsAddon${fuzzer}Fuzzer PRIVATE WindowsAddon::Protocols)
        target_compile_options(WindowsAddon${fuzzer}Fuzzer PRIVATE -fsanitize=fuzzer)
        target_link_options(WindowsAddon${fuzzer}Fuzzer PRIVATE -fsanitize=fuzzer)
        windowsaddon_configure_own_target(WindowsAddon${fuzzer}Fuzzer)
    endforeach()
endif()

if(NOT WINDOWSADDON_BUILD_APP)
    return()
endif()

# Prefer the config package supplied by vcpkg, but retain compatibility with
# distro-provided wxWidgets packages on Linux.
find_package(wxWidgets CONFIG QUIET)
if(wxWidgets_FOUND AND TARGET wx::core)
    set(WINDOWSADDON_WX_LIBRARIES
        wx::core
        wx::base
        wx::aui
        wx::stc
        wx::propgrid
    )
else()
    find_package(wxWidgets REQUIRED COMPONENTS core base aui stc propgrid)
    include("${wxWidgets_USE_FILE}")
    set(WINDOWSADDON_WX_LIBRARIES ${wxWidgets_LIBRARIES})
endif()

find_package(fmt CONFIG REQUIRED)
find_package(Threads REQUIRED)

find_package(lodepng CONFIG REQUIRED)
if(NOT TARGET lodepng)
    message(FATAL_ERROR "The lodepng package was found, but its 'lodepng' target is missing")
endif()

if(WINDOWSADDON_USE_HIDAPI)
    find_package(hidapi CONFIG REQUIRED)
    if(NOT TARGET hidapi::hidapi)
        message(FATAL_ERROR "The hidapi package was found, but its 'hidapi::hidapi' target is missing")
    endif()
endif()

if(WINDOWSADDON_USE_OPENCV)
    find_package(OpenCV CONFIG REQUIRED)
endif()

set(WINDOWSADDON_SOURCES
    src/Alarms.cpp
    src/BackupSettings.cpp
    src/BsecHandler.cpp
    src/CanDeviceFactory.cpp
    src/CanDeviceLawicel.cpp
    src/CanDeviceStm32.cpp
    src/CanEntryHandler.cpp
    src/CanFrameLog.cpp
    src/CanIsoTpEndpoint.cpp
    src/CanScriptHandler.cpp
    src/CanSerialPort.cpp
    src/CanXmlLoaders.cpp
    src/CmdExecutor.cpp
    src/CorsairHid.cpp
    src/CryptoPrice.cpp
    src/CustomMacro.cpp
    src/DatabaseImpl.cpp
    src/DatabaseLogic.cpp
    src/DefaultCommandsFile.cpp
    src/DidHandler.cpp
    src/DirectoryBackup.cpp
    src/ExceptionHandler.cpp
    src/GraphGenerator.cpp
    src/HotkeyRegistry.cpp
    src/IdlePowerSaver.cpp
    src/ImageRecognition.cpp
    src/KeyScanCodes.cpp
    src/Logger.cpp
    src/MacroActions.cpp
    src/MacroSettings.cpp
    src/MeasurementRepository.cpp
    src/ModbusFrameLog.cpp
    src/ModbusHandler.cpp
    src/ModbusJsonPersistence.cpp
    src/ModbusMasterSerialPort.cpp
    src/ModbusValueIo.cpp
    src/ModbusXmlLoaders.cpp
    src/PathSeparator.cpp
    src/PrintScreenSaver.cpp
    src/ScriptLauncher.cpp
    src/Sensors.cpp
    src/SerialPort.cpp
    src/SerialPortBase.cpp
    src/SerialTcpBackend.cpp
    src/Server.cpp
    src/Session.cpp
    src/Settings.cpp
    src/TcpMessageExecutor.cpp
    src/TerminalHotkey.cpp
    src/Utils.cpp
    src/WindowsAddon.cpp
    src/WorkingDays.cpp
    src/WorkingDaysCalendar.cpp
    src/XmlConfigurations.cpp
    src/platform/KeyboardInput.cpp
    src/platform/ScreenAutomation.cpp
    src/platform/StandardBackupFileSystem.cpp
    src/utils/AsyncSerial.cpp
    src/utils/TextStyleXml.cpp
    src/utils/WindowsCommand.cpp
    src/utils/XmlDocument.cpp
)

set(WINDOWSADDON_GUI_SOURCES
    src/gui/AlarmPanel.cpp
    src/gui/BackupProgressDialog.cpp
    src/gui/BitFieldEditorDialog.cpp
    src/gui/CanPanel/CanGrids.cpp
    src/gui/CanPanel/CanLogPanel.cpp
    src/gui/CanPanel/CanPanel.cpp
    src/gui/CanPanel/CanScriptPanel.cpp
    src/gui/CanPanel/CanSenderPanel.cpp
    src/gui/CanPanel/CanUdsRawDialog.cpp
    src/gui/CmdExecutorPanel.cpp
    src/gui/ConfigurationBackup.cpp
    src/gui/DebugPanel.cpp
    src/gui/DidPanel.cpp
    src/gui/EscaperPanel.cpp
    src/gui/FilePanel.cpp
    src/gui/GridBuilder.cpp
    src/gui/LogPanel.cpp
    src/gui/LogRecordingBar.cpp
    src/gui/MainFrame.cpp
    src/gui/MainFrameAccess.cpp
    src/gui/MainPanel.cpp
    src/gui/MenuCommand.cpp
    src/gui/ModbusDataPanel.cpp
    src/gui/ModbusDialogs.cpp
    src/gui/ModbusGraphView.cpp
    src/gui/ModbusMasterPanel.cpp
    src/gui/NotificationPresenter.cpp
    src/gui/Prompts.cpp
    src/gui/SettingsDialog.cpp
    src/gui/StatusLabel.cpp
    src/gui/TextStylePanel.cpp
    src/gui/TimeTrackerPanel.cpp
    src/gui/TrayIcon.cpp
    src/gui/WxClipboard.cpp
)

if(WIN32)
    list(APPEND WINDOWSADDON_SOURCES
        libs/enumser/enumser.cpp
        cmake/WindowsAddon.rc
    )
    # enumser is legacy third-party code stored in its original Windows codepage.
    set_source_files_properties(libs/enumser/enumser.cpp PROPERTIES
        COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/wd4828>"
    )
endif()

set(WINDOWSADDON_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${WINDOWSADDON_GENERATED_DIR}")

# commitid.h is regenerated on every build, not at configure time: the old
# execute_process block froze the id at whatever HEAD was when CMake last
# ran, so incremental builds carried a stale commit id until the next
# reconfigure. The script only rewrites the header when its content changes,
# so ordinary rebuilds recompile nothing.
set(WINDOWSADDON_COMMITID_ARGS
    -DCOMMITID_TEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/commitid.h.in
    -DCOMMITID_OUTPUT=${WINDOWSADDON_GENERATED_DIR}/commitid.h
    -DCOMMITID_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -DCOMMITID_FALLBACK_TAG=${PROJECT_VERSION}
    -DCOMMITID_PREFIX=WINDOWSADDON
)
# Once at configure time, so the header exists before the first build...
execute_process(COMMAND "${CMAKE_COMMAND}" ${WINDOWSADDON_COMMITID_ARGS}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCommitId.cmake")
# ...and again before every build of the targets that include it.
add_custom_target(windowsaddon_commitid
    COMMAND "${CMAKE_COMMAND}" ${WINDOWSADDON_COMMITID_ARGS}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCommitId.cmake"
    COMMENT "Refreshing commitid.h"
    VERBATIM
)

add_library(windowsaddon_gui STATIC ${WINDOWSADDON_GUI_SOURCES})
add_library(WindowsAddon::Gui ALIAS windowsaddon_gui)
add_executable(WindowsAddon WIN32 ${WINDOWSADDON_SOURCES})

set(WINDOWSADDON_APP_TARGETS WindowsAddon windowsaddon_gui)
foreach(target IN LISTS WINDOWSADDON_APP_TARGETS)
    add_dependencies(${target} windowsaddon_commitid)
    target_precompile_headers(${target} PRIVATE src/pch.hpp)
    # The generated dir sits before src/: the MSBuild pre-build event writes
    # its own commitid.h into src/, and with src/ first that stale copy
    # silently shadowed the one this build just generated.
    target_include_directories(${target} PRIVATE
        "${WINDOWSADDON_GENERATED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/interface"
    )
    target_compile_definitions(${target} PRIVATE
        WINDOWSADDON_CMAKE_BUILD
        WINDOWSHELPER_CMAKE_BUILD  # spelling used by the upstream-carried sources
        $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS;WIN32_LEAN_AND_MEAN;NOMINMAX;_WIN32_WINNT=0x0601>
        $<$<BOOL:${WINDOWSADDON_USE_HIDAPI}>:USE_HIDAPI>
        $<$<BOOL:${WINDOWSADDON_USE_OPENCV}>:USE_OPENCV>
        $<$<BOOL:${WINDOWSADDON_USE_BSEC}>:USE_BSEC>
    )
    windowsaddon_configure_own_target(${target})
    windowsaddon_allow_legacy_gui_warnings(${target})
endforeach()

set(WINDOWSADDON_APP_LIBRARIES
    WindowsAddon::Protocols
    WindowsAddon::Persistence
    WindowsAddon::Automation
    WindowsAddon::Platform
    ${WINDOWSADDON_WX_LIBRARIES}
    Boost::system
    Boost::program_options
    fmt::fmt
    Threads::Threads
    ${WINDOWSADDON_SQLITE_TARGET}
    lodepng
    ${CMAKE_DL_LIBS}
)

target_link_libraries(windowsaddon_gui PRIVATE ${WINDOWSADDON_APP_LIBRARIES})
target_link_libraries(WindowsAddon PRIVATE
    WindowsAddon::Gui
    ${WINDOWSADDON_APP_LIBRARIES}
)

if(WINDOWSADDON_USE_HIDAPI)
    target_link_libraries(windowsaddon_gui PRIVATE hidapi::hidapi)
    target_link_libraries(WindowsAddon PRIVATE hidapi::hidapi)
endif()

if(WINDOWSADDON_USE_OPENCV)
    target_include_directories(windowsaddon_gui PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(windowsaddon_gui PRIVATE ${OpenCV_LIBS})
    target_include_directories(WindowsAddon PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(WindowsAddon PRIVATE ${OpenCV_LIBS})
endif()

if(WINDOWSADDON_USE_BSEC)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "WINDOWSADDON_USE_BSEC requires a 64-bit Windows build")
    endif()
    if(NOT WINDOWSADDON_BSEC_LIBRARY OR NOT EXISTS "${WINDOWSADDON_BSEC_LIBRARY}")
        message(FATAL_ERROR
            "WINDOWSADDON_USE_BSEC requires -DWINDOWSADDON_BSEC_LIBRARY=<path-to-BSECLibrary64.lib>")
    endif()
    target_link_libraries(windowsaddon_gui PRIVATE "${WINDOWSADDON_BSEC_LIBRARY}")
    target_link_libraries(WindowsAddon PRIVATE "${WINDOWSADDON_BSEC_LIBRARY}")
endif()

if(WIN32)
    target_link_libraries(windowsaddon_gui PRIVATE
        Dbghelp
        PowrProf
        Shlwapi
        Urlmon
        Wlanapi
        Wtsapi32
    )
    target_link_libraries(WindowsAddon PRIVATE
        Dbghelp
        PowrProf
        Shlwapi
        Urlmon
        Wlanapi
        Wtsapi32
    )
endif()

install(TARGETS WindowsAddon RUNTIME DESTINATION bin)
