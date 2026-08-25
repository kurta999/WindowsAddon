#pragma once

#include "interface/IModbusEntry.hpp"
#include "ModbusWriteQueue.hpp"

#include <string>
#include <string_view>
#include <vector>

// !\brief The plain-text register value export and import.
//
// These were three ModbusEntryHandler methods - ClearValues, ExportValues and
// ImportValues - that walked the four register tables directly. None of them
// took the model lock, so each raced the polling worker that writes those same
// items; the visitor the GUI has to go through was introduced for exactly that
// reason and these three sat beside it. They also mixed three jobs: opening a
// file, deciding what the text says, and queueing writes.
//
// Split out, the text format is a pure function of a layout and can be tested
// without a serial port, and the handler is left holding the lock around a call
// rather than around a hundred lines of formatting and file I/O.
namespace modbus_values
{
// !\brief Clears every register's value, back to "nothing read yet".
void Clear(ModbusDeviceLayout& layout);

// !\brief The whole layout in the export format.
[[nodiscard]] std::string Format(const ModbusDeviceLayout& layout);

// !\brief Applies exported text back onto `layout`.
//
// !\return The writes that have to reach the device, in file order. They are
//          returned rather than queued so that this stays independent of the
//          handler, and so the caller can queue them after releasing the lock.
//
// Only coils and holding registers are applied: input status and input
// registers are read-only on the wire, which is why the original ignored them.
[[nodiscard]] std::vector<ModbusWrite> Apply(ModbusDeviceLayout& layout, std::string_view text);
}
