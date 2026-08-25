#pragma once

#include "utils/HexBytes.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// !\brief The mini-script the UDS raw dialog sends: frames and delays.
//
// The parsing lived inside CanUdsRawDialog::HandleFrameSending, interleaved
// with the sending and the sleeping, so the syntax - lines split on newlines
// or semicolons, "DELAY:<ms>" directives, everything else hex with separators
// allowed - existed only as the shape of one GUI loop and had no tests.
// Parsing is this header now; the dialog keeps the sending and, deliberately,
// the sleeping - blocking between frames is what that dialog does.
namespace uds_raw
{
struct Step
{
    enum class Kind : std::uint8_t { Delay, Frame };
    Kind kind = Kind::Frame;
    // !\brief Milliseconds, for a Delay step.
    int delay_ms = 0;
    // !\brief The bytes to send, for a Frame step.
    std::vector<std::uint8_t> payload;
};

struct Skipped
{
    std::string line;
    // !\brief "empty" or "not hex" - the two reasons the dialog logged.
    std::string_view reason;
};

struct Script
{
    std::vector<Step> steps;
    std::vector<Skipped> skipped;
};

// !\brief Parse one script. Lines that parse as neither delay nor hex are
// collected in `skipped` and otherwise ignored, exactly as the dialog's loop
// logged and continued.
[[nodiscard]] inline Script Parse(std::string_view text, std::size_t max_frame_len)
{
    Script script;

    std::size_t begin = 0;
    while(begin <= text.size())
    {
        const std::size_t end = text.find_first_of("\n;", begin);
        std::string_view line = text.substr(begin,
            end == std::string_view::npos ? std::string_view::npos : end - begin);
        begin = end == std::string_view::npos ? text.size() + 1 : end + 1;

        /* The old split compressed adjacent separators; an all-separator gap
           produced no line at all rather than an "empty input" warning. */
        if(line.empty())
            continue;

        /* "DELAY" plus one separator character of any kind, then the number -
           the same shape the sscanf format accepted. */
        if(line.starts_with("DELAY") && line.size() > 6)
        {
            int delay = 0;
            bool numeric = false;
            for(std::size_t index = 6; index < line.size(); ++index)
            {
                const char c = line[index];
                if(c < '0' || c > '9')
                    break;
                delay = delay * 10 + (c - '0');
                numeric = true;
            }
            if(numeric)
            {
                script.steps.push_back({ Step::Kind::Delay, delay, {} });
                continue;
            }
        }

        const std::string digits = utils::StripHexSeparators(line);
        if(digits.empty())
        {
            script.skipped.push_back({ std::string(line), "empty" });
            continue;
        }

        auto payload = utils::ParseHexBytes(digits, max_frame_len);
        if(!payload)
        {
            script.skipped.push_back({ std::string(line), "not hex" });
            continue;
        }

        script.steps.push_back({ Step::Kind::Frame, 0, std::move(*payload) });
    }
    return script;
}
}
