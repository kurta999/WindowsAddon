#pragma once

#include "utils/EnumNameTable.hpp"

#include "ICanEntry.hpp"
#include "CanModels.hpp"
#include <map>
#include <limits>

class XmlCanEntryLoader : public ICanEntryLoader
{
public:
    bool Load(const std::filesystem::path& path, std::vector<std::unique_ptr<CanTxEntry>>& e) override;
    bool Save(const std::filesystem::path& path, std::vector<std::unique_ptr<CanTxEntry>>& e) const override;
};

class XmlCanRxEntryLoader : public ICanRxEntryLoader
{
public:
    bool Load(const std::filesystem::path& path, std::unordered_map<uint32_t, std::string>& e, std::unordered_map<uint32_t, uint8_t>& loglevels) override;
    bool Save(const std::filesystem::path& path, std::unordered_map<uint32_t, std::string>& e, std::unordered_map<uint32_t, uint8_t>& loglevels) const override;
};

class XmlCanMappingLoader : public ICanMappingLoader
{
public:
    bool Load(const std::filesystem::path& path, CanMapping& mapping, CanFrameMetadataMap& metadata) override;
    bool Save(const std::filesystem::path& path, CanMapping& mapping, const CanFrameMetadataMap& metadata) const override;

    static CanBitfieldType      GetTypeFromString(std::string_view input);
    static std::string_view     GetStringFromType(CanBitfieldType type);
    static std::pair<int64_t, int64_t> GetMinMaxForType(CanBitfieldType type);

private:
    /* A constexpr table rather than a std::map built at static-init time: the
       lookup is twelve string compares either way, and this one costs no
       allocation and no ordering between translation units. */
    static constexpr utils::EnumNameTable m_CanBitfieldTypeMap{
        CBT_INVALID,
        std::array<utils::EnumName<CanBitfieldType>, 12>{{
            { CBT_INVALID, "invalid" },
            { CBT_BOOL,    "bool" },
            { CBT_UI8,     "uint8_t" },
            { CBT_I8,      "int8_t" },
            { CBT_UI16,    "uint16_t" },
            { CBT_I16,     "int16_t" },
            { CBT_UI32,    "uint32_t" },
            { CBT_I32,     "int32_t" },
            { CBT_UI64,    "uint64_t" },
            { CBT_I64,     "int64_t" },
            { CBT_FLOAT,   "float" },
            { CBT_DOUBLE,  "double" },
        }}
    };

    static inline std::map<CanBitfieldType, std::pair<int64_t, int64_t>> m_CanTypeSizes
    {
        {CBT_BOOL,    {0, 1}},
        {CBT_UI8,     {std::numeric_limits<uint8_t>::min(),  std::numeric_limits<uint8_t>::max()}},
        {CBT_I8,      {std::numeric_limits<int8_t>::min(),   std::numeric_limits<int8_t>::max()}},
        {CBT_UI16,    {std::numeric_limits<uint16_t>::min(), std::numeric_limits<uint16_t>::max()}},
        {CBT_I16,     {std::numeric_limits<int16_t>::min(),  std::numeric_limits<int16_t>::max()}},
        {CBT_UI32,    {std::numeric_limits<uint32_t>::min(), std::numeric_limits<uint32_t>::max()}},
        {CBT_I32,     {std::numeric_limits<int32_t>::min(),  std::numeric_limits<int32_t>::max()}},
        {CBT_I64,     {std::numeric_limits<int64_t>::min(),  std::numeric_limits<int64_t>::max()}},
        {CBT_INVALID, {0, 0}}
    };
};
