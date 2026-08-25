#pragma once

#include <cstdint>
#include <utility>

namespace utils
{
    // !\brief The C++ type a protocol field is decoded as.
    //
    // CAN and Modbus each enumerate their own field types, and each enum names
    // the same eleven scalars plus its own extras - a string for Modbus, an
    // invalid marker for both.
    enum class ScalarType : uint8_t
    {
        // !\brief Not a scalar: the invalid markers, and Modbus's string.
        None,

        U8, I8, U16, I16, U32, I32, U64, I64, Float, Double,
    };

    // !\brief Calls `fn.operator()<T>()` for the type `scalar` stands for.
    //
    // The caller passes a generic lambda: `[&]<typename T>() { ... }`. Nothing
    // is called for ScalarType::None, which is what both callers' `default:`
    // arms already did.
    //
    // CanModels.hpp and IModbusEntry.hpp each held this switch, character for
    // character the same down to the alignment of the break statements, with
    // only the enumerator prefix differing. The awkward part - naming a
    // template operator() through a forwarded reference - is written once here,
    // and each protocol says only which scalar its own values stand for.
    template <typename F>
    void DispatchScalarType(ScalarType scalar, F&& fn)
    {
        switch(scalar)
        {
            case ScalarType::U8:     std::forward<F>(fn).template operator()<uint8_t>();  break;
            case ScalarType::I8:     std::forward<F>(fn).template operator()<int8_t>();   break;
            case ScalarType::U16:    std::forward<F>(fn).template operator()<uint16_t>(); break;
            case ScalarType::I16:    std::forward<F>(fn).template operator()<int16_t>();  break;
            case ScalarType::U32:    std::forward<F>(fn).template operator()<uint32_t>(); break;
            case ScalarType::I32:    std::forward<F>(fn).template operator()<int32_t>();  break;
            case ScalarType::U64:    std::forward<F>(fn).template operator()<uint64_t>(); break;
            case ScalarType::I64:    std::forward<F>(fn).template operator()<int64_t>();  break;
            case ScalarType::Float:  std::forward<F>(fn).template operator()<float>();    break;
            case ScalarType::Double: std::forward<F>(fn).template operator()<double>();   break;
            case ScalarType::None:   break;
        }
    }
}
