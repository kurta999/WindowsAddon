#pragma once

// The dependency-free half of the old monolithic precompiled header:
// standard library, boost, platform C headers and third-party C libraries.
// It must never grow a wxWidgets, GUI or application-service include - that is
// what kept every service in this project bound to the GUI target.

/* Before any boost header: boost/system's snprintf shim expects the C stdio
   declarations to be visible, and used to get them by accident from the
   include order rather than by asking. */
#include <cstdio>
#include <cstring>

#include <boost/algorithm/string.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/algorithm/string_regex.hpp>
#include <boost/algorithm/hex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/tokenizer.hpp>
#include <boost/optional.hpp>
#include <boost/endian.hpp>

#include <assert.h>

#ifdef _WIN32
/* <Windows.h> first: every header below it needs its types. This block sat
   after them and only worked because <boost/asio.hpp> at the top of this
   file included <Windows.h> as a side effect. */
#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shlwapi.h>
#include <powrprof.h>
#include <tlhelp32.h>
#include "Wtsapi32.h"
#include <wlanapi.h>
#endif

#include <any>
#include <iostream>
#include <fstream>
#include <array>
#include <variant>
#include <string>
#include <bitset>
#include <memory>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>

#include <algorithm>
#include <bit>
#include <ranges>
#include <future> 
#include <tuple>
#include <set>
#include <thread>
#include <iterator>
#include <queue>
#include <deque>
#include <cstdint>
#include <stack>
#include <regex>

extern "C"
{
#ifdef USE_BSEC
	#include "bsec/bsec_interface.h"
#endif
}

/* `using namespace std::chrono_literals;` used to sit here, which put a
   using-directive into all 43 headless translation units whether they wanted
   one or not. The twelve files that write 100ms say so themselves. */
