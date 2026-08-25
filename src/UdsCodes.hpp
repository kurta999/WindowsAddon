#pragma once

#include <cstdint>

// !\brief The UDS bytes this application compares against, named.
//
// They were written as literals at sixteen places across the DID read and write
// paths - 0x22, 0x62, 0x2E, 0x6E, 0x7F, 0x78 - so telling the two paths apart
// meant knowing which service byte belonged to which, and the 0x11..0x93 range
// test carried an explanatory comment repeated four times.
//
// The end-to-end ECU simulator had grown its own copy of the same namespace in
// its own spelling, and the two disagreed: 0x10 was named "request out of
// range" here and generalReject there. The simulator was right - ISO 14229 puts
// requestOutOfRange at 0x31 - so this file was wrong about a byte it compares
// against on every rejected DID. One set of names now, in one place.
namespace uds
{
    // !\brief Service identifiers, as they appear on the wire.
    inline constexpr std::uint8_t kDiagnosticSessionControl = 0x10;
    inline constexpr std::uint8_t kReadDataByIdentifier = 0x22;
    inline constexpr std::uint8_t kWriteDataByIdentifier = 0x2E;
    inline constexpr std::uint8_t kTesterPresent = 0x3E;

    // !\brief Diagnostic sessions.
    inline constexpr std::uint8_t kDefaultSession = 0x01;
    inline constexpr std::uint8_t kExtendedDiagnosticSession = 0x03;

    // !\brief A positive response echoes the request's service id plus this.
    inline constexpr std::uint8_t kPositiveResponseOffset = 0x40;

    inline constexpr std::uint8_t kReadPositiveResponse =
        static_cast<std::uint8_t>(kReadDataByIdentifier + kPositiveResponseOffset);     // 0x62
    inline constexpr std::uint8_t kWritePositiveResponse =
        static_cast<std::uint8_t>(kWriteDataByIdentifier + kPositiveResponseOffset);    // 0x6E
    inline constexpr std::uint8_t kSessionPositiveResponse =
        static_cast<std::uint8_t>(kDiagnosticSessionControl + kPositiveResponseOffset); // 0x50

    // !\brief A negative response: this byte, the service asked for, the reason.
    inline constexpr std::uint8_t kNegativeResponse = 0x7F;

    // !\brief Negative response codes, with the values ISO 14229 assigns them.
    //
    // kGeneralReject shares its value with kDiagnosticSessionControl and has
    // nothing to do with it - one is a service id in a request, the other a
    // reason in a negative response. Both were written as a bare 0x10 in
    // DidHandler.cpp.
    inline constexpr std::uint8_t kGeneralReject = 0x10;
    inline constexpr std::uint8_t kServiceNotSupported = 0x11;
    inline constexpr std::uint8_t kConditionsNotCorrect = 0x22;
    inline constexpr std::uint8_t kRequestOutOfRange = 0x31;
    inline constexpr std::uint8_t kSecurityAccessDenied = 0x33;

    // !\brief "Request correctly received, response pending" - the ECU is still
    // working and the requester is expected to keep waiting.
    inline constexpr std::uint8_t kResponsePending = 0x78;

    // !\brief The span the DID paths treat as "some negative response code".
    //
    // Note that it starts above kGeneralReject, so the one NRC this application
    // singles out is also the one its range test excludes. That is what the
    // code does; whether it was meant to is not recorded anywhere.
    inline constexpr std::uint8_t kFirstNrc = 0x11;
    inline constexpr std::uint8_t kLastNrc = 0x93;

    // !\brief Whether `code` falls in the range above.
    [[nodiscard]] constexpr bool IsNrc(std::uint8_t code)
    {
        return code >= kFirstNrc && code <= kLastNrc;
    }

    // !\brief What a response frame means to the code waiting for it.
    enum class ResponseKind
    {
        // !\brief The request succeeded.
        Positive,
        // !\brief NRC 0x78 - the ECU is still working; keep waiting.
        Pending,
        // !\brief NRC 0x10, generalReject. This application stops asking.
        Rejected,
        // !\brief Some other NRC in the kFirstNrc..kLastNrc range.
        NegativeOther,
        // !\brief Anything else, including a frame that is not a response at all.
        Unexpected,
    };

    // !\brief Classify a frame arriving inside the "response pending" wait loop
    // of a request that carried `service_id`.
    //
    // The DID read loop and the DID write loop each spelled this out as a chain
    // of four `else if`s over the same three bytes, differing only in which
    // service id they compared against.
    //
    // Two properties of that chain are preserved here deliberately, because
    // they are what the loops do today and changing them would be a behaviour
    // change rather than a refactor:
    //
    //   - byte 0 is not required to be kNegativeResponse before bytes 1 and 2
    //     are read as an NRC. A frame that is neither the positive response nor
    //     a negative response, but whose bytes 1 and 2 happen to match, is
    //     classified as if it were an NRC.
    //   - Rejected is tested before NegativeOther and without consulting the
    //     service id, so a generalReject attributed to some other service still
    //     reads as Rejected.
    [[nodiscard]] constexpr ResponseKind ClassifyPendingResponse(std::uint8_t service_id,
        std::uint8_t byte0, std::uint8_t byte1, std::uint8_t byte2)
    {
        const auto positive = static_cast<std::uint8_t>(service_id + kPositiveResponseOffset);

        if(byte0 == positive)
            return ResponseKind::Positive;
        if(byte1 == service_id && byte2 == kResponsePending)
            return ResponseKind::Pending;
        if(byte2 == kGeneralReject)
            return ResponseKind::Rejected;
        if(byte1 == service_id && IsNrc(byte2))
            return ResponseKind::NegativeOther;

        return ResponseKind::Unexpected;
    }
}
