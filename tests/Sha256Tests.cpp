#include "TestFramework.hpp"

extern "C"
{
#include <sha256/sha256.h>
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace
{
std::string Digest(std::string_view input, size_t chunk_size)
{
    SHA256_CTX context{};
    std::array<uint8_t, SHA256_BLOCK_SIZE> hash{};
    sha256_init(&context);

    for(size_t offset = 0; offset < input.size(); offset += chunk_size)
    {
        const size_t length = std::min(chunk_size, input.size() - offset);
        sha256_update(&context, reinterpret_cast<const uint8_t*>(input.data() + offset), length);
    }
    sha256_final(&context, hash.data());

    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(hash.size() * 2);
    for(const uint8_t byte : hash)
    {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0F]);
    }
    return result;
}
}

TEST_CASE(Sha256MatchesPublishedEmptyInputVector)
{
    EXPECT_EQ(Digest("", 1), std::string("e3b0c44298fc1c149afbf4c8996fb924"
                                         "27ae41e4649b934ca495991b7852b855"));
}

TEST_CASE(Sha256ProducesTheSameDigestAcrossStreamingChunks)
{
    constexpr std::string_view input = "The quick brown fox jumps over the lazy dog";
    const std::string expected = "d7a8fbb307d7809469ca9abcb0082e4f"
                                 "8d5651e46d3cdb762d02d0bf37c9e592";

    EXPECT_EQ(Digest(input, input.size()), expected);
    EXPECT_EQ(Digest(input, 3), expected);
}
