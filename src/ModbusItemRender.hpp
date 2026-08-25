#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "interface/IModbusEntry.hpp"

// !\brief How one Modbus register should appear in a grid cell.
//
// Deciding this - which of the six numeric formats applies, how many decimals a
// scaled value gets, whether a conditional-colour rule overrides the item's own
// colours - is domain logic, not drawing. It lived inside the wxWidgets panel,
// which meant it could only run on the UI thread with the model unlocked, and
// could not be tested at all.
//
// Producing an owning value here lets the handler compute it while it holds its
// model lock and hand the result to the GUI, which then only has to paint it.
struct ModbusCellRender
{
    std::string text;
    std::optional<uint32_t> color;
    std::optional<uint32_t> background_color;
};

// !\brief Render `item`'s current value.
[[nodiscard]] ModbusCellRender RenderModbusItem(const ModbusItem& item);
