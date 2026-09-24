#include "range_converter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

#include "byte_io.h"
#include "crc32.h"
#include "novatel/novatel_protocol.h"
#include "novatel/novatel_range.h"

namespace gnsslog {
namespace {

enum AsciiSource
{
    kAsciiSourceUnknown = 0,
    kAsciiSourceRangeA,
    kAsciiSourceObsVmA
};

void SetError(char* text, std::size_t size, const char* message)
{
    if (text == NULL || size == 0U) {
        return;
    }
    std::snprintf(text, size, "%s", message != NULL ? message : "conversion failed");
}

char* NextCsvToken(char** cursor)
{
    if (cursor == NULL || *cursor == NULL) {
        return NULL;
    }

    char* const start = *cursor;
    char* comma = std::strchr(start, ',');
    if (comma != NULL) {
        *comma = '\0';
        *cursor = comma + 1;
    } else {
        *cursor = NULL;
    }
    return start;
}

bool ParseUnsigned(const char* text, unsigned long max_value, int base, unsigned long* value)
{
    if (text == NULL || *text == '\0' || value == NULL) {
        return false;
    }
    char* end = NULL;
    const unsigned long parsed = std::strtoul(text, &end, base);
    if (end == text || *end != '\0' || parsed > max_value) {
        return false;
    }
    *value = parsed;
    return true;
}

bool ParseU8Dec(const char* text, std::uint8_t* value)
{
    unsigned long parsed = 0UL;
    if (!ParseUnsigned(text, 0xFFUL, 10, &parsed) || value == NULL) {
        return false;
    }
    *value = static_cast<std::uint8_t>(parsed);
    return true;
}

bool ParseU16Dec(const char* text, std::uint16_t* value)
{
    unsigned long parsed = 0UL;
    if (!ParseUnsigned(text, 0xFFFFUL, 10, &parsed) || value == NULL) {
        return false;
    }
    *value = static_cast<std::uint16_t>(parsed);
    return true;
}

bool ParseU32Dec(const char* text, std::uint32_t* value)
{
    unsigned long parsed = 0UL;
    if (!ParseUnsigned(text, 0xFFFFFFFFUL, 10, &parsed) || value == NULL) {
        return false;
    }
    *value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool ParseU16Hex(const char* text, std::uint16_t* value)
{
    unsigned long parsed = 0UL;
    if (!ParseUnsigned(text, 0xFFFFUL, 16, &parsed) || value == NULL) {
        return false;
    }
    *value = static_cast<std::uint16_t>(parsed);
    return true;
}

bool ParseU32Hex(const char* text, std::uint32_t* value)
{
    unsigned long parsed = 0UL;
    if (!ParseUnsigned(text, 0xFFFFFFFFUL, 16, &parsed) || value == NULL) {
        return false;
    }
    *value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool ParseDouble(const char* text, double* value)
{
    if (text == NULL || *text == '\0' || value == NULL) {
        return false;
    }
    char* end = NULL;
    const double parsed = std::strtod(text, &end);
    if (end == text || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    *value = parsed;
    return true;
}

bool ParseFloat(const char* text, float* value)
{
    double parsed = 0.0;
    if (!ParseDouble(text, &parsed) || value == NULL ||
        parsed < -static_cast<double>(std::numeric_limits<float>::max()) ||
        parsed > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    *value = static_cast<float>(parsed);
    return true;
}

bool EncodeIdlePercent(double percent, std::uint8_t* value)
{
    if (value == NULL || !std::isfinite(percent) || percent < 0.0 || percent > 100.0) {
        return false;
    }
    const double encoded = std::floor(percent * 2.0 + 0.5);
    if (encoded < 0.0 || encoded > 200.0) {
        return false;
    }
    *value = static_cast<std::uint8_t>(encoded);
    return true;
}

bool PortAddressFromAscii(const char* text, std::uint8_t* value)
{
    if (text == NULL || value == NULL) {
        return false;
    }
    struct Entry
    {
        const char* name;
        std::uint8_t value;
    };
    static const Entry table[] = {
        {"NO_PORTS", 0x00U}, {"NOPORT", 0x00U}, {"COM1", 0x20U},
        {"COM2", 0x40U}, {"COM3", 0x60U}, {"SPECIAL", 0xA0U},
        {"THISPORT", 0xC0U}, {"FILE", 0xE0U}, {"USB1", 0xA0U},
        {"USB2", 0xA0U}, {"USB3", 0xA0U}, {"AUX", 0xA0U},
        {"COM4", 0xA0U}, {"ETH1", 0xA0U}, {"IMU", 0xA0U},
        {"ICOM1", 0xA0U}
    };
    const std::size_t count = sizeof(table) / sizeof(table[0]);
    for (std::size_t i = 0U; i < count; ++i) {
        if (std::strcmp(text, table[i].name) == 0) {
            *value = table[i].value;
            return true;
        }
    }
    return false;
}

const char* PortAddressToAscii(std::uint8_t value)
{
    switch (value) {
    case 0x00U: return "NO_PORTS";
    case 0x20U: return "COM1";
    case 0x40U: return "COM2";
    case 0x60U: return "COM3";
    case 0xA0U: return "SPECIAL";
    case 0xC0U: return "THISPORT";
    case 0xE0U: return "FILE";
    default: return "SPECIAL";
    }
}

AsciiSource DetectAsciiSource(const char* line)
{
    if (line == NULL) {
        return kAsciiSourceUnknown;
    }
    if (std::strncmp(line, "#RANGEA,", 8U) == 0) {
        return kAsciiSourceRangeA;
    }
    if (std::strncmp(line, "#OBSVMA,", 8U) == 0) {
        return kAsciiSourceObsVmA;
    }
    return kAsciiSourceUnknown;
}

bool ParseRangeAHeader(char* header_text, novatel::BinaryHeader* header)
{
    if (header_text == NULL || header == NULL) {
        return false;
    }

    char* cursor = header_text;
    char* field = NextCsvToken(&cursor);
    if (field == NULL || std::strcmp(field, "RANGEA") != 0) {
        return false;
    }

    std::uint8_t port = 0U;
    field = NextCsvToken(&cursor);
    if (!PortAddressFromAscii(field, &port)) {
        return false;
    }

    std::uint16_t sequence = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU16Dec(field, &sequence)) {
        return false;
    }

    double idle_percent = 0.0;
    field = NextCsvToken(&cursor);
    if (!ParseDouble(field, &idle_percent)) {
        return false;
    }
    std::uint8_t idle = 0U;
    if (!EncodeIdlePercent(idle_percent, &idle)) {
        return false;
    }

    std::uint8_t time_status = 0U;
    field = NextCsvToken(&cursor);
    if (!novatel::TimeStatusFromAscii(field, &time_status)) {
        return false;
    }

    std::uint16_t week = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU16Dec(field, &week)) {
        return false;
    }

    double seconds = 0.0;
    field = NextCsvToken(&cursor);
    if (!ParseDouble(field, &seconds) || seconds < 0.0 || seconds >= 604800.0) {
        return false;
    }
    const double ms_double = std::floor(seconds * 1000.0 + 0.5);
    if (ms_double < 0.0 || ms_double > 604800000.0) {
        return false;
    }

    std::uint32_t receiver_status = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU32Hex(field, &receiver_status)) {
        return false;
    }

    std::uint16_t reserved = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU16Hex(field, &reserved)) {
        return false;
    }

    std::uint16_t sw_version = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU16Dec(field, &sw_version) || NextCsvToken(&cursor) != NULL) {
        return false;
    }

    std::memset(header, 0, sizeof(*header));
    header->message_id = novatel::kMessageIdRange;
    header->message_type = 0U;
    header->port_address = port;
    header->sequence = sequence;
    header->idle_time = idle;
    header->time_status = time_status;
    header->week = week;
    header->milliseconds = static_cast<std::uint32_t>(ms_double);
    header->receiver_status = receiver_status;
    header->reserved = reserved;
    header->receiver_sw_version = sw_version;
    return true;
}

bool ParseObsVmAHeader(char* header_text, novatel::BinaryHeader* header)
{
    if (header_text == NULL || header == NULL) {
        return false;
    }

    char* cursor = header_text;
    char* field = NextCsvToken(&cursor);
    if (field == NULL || std::strcmp(field, "OBSVMA") != 0) {
        return false;
    }

    std::uint8_t cpu_idle = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU8Dec(field, &cpu_idle) || cpu_idle > 100U) {
        return false;
    }

    field = NextCsvToken(&cursor);
    if (field == NULL || (std::strcmp(field, "GPS") != 0 && std::strcmp(field, "GPST") != 0)) {
        return false;
    }

    std::uint8_t time_status = 0U;
    field = NextCsvToken(&cursor);
    if (!novatel::TimeStatusFromAscii(field, &time_status)) {
        return false;
    }

    std::uint16_t week = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU16Dec(field, &week)) {
        return false;
    }

    std::uint32_t milliseconds = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU32Dec(field, &milliseconds) || milliseconds > 604800000U) {
        return false;
    }

    std::uint32_t ignored_u32 = 0U;
    field = NextCsvToken(&cursor);  // Unicore format version.
    if (!ParseU32Dec(field, &ignored_u32)) {
        return false;
    }
    field = NextCsvToken(&cursor);  // Reserved.
    if (!ParseU32Dec(field, &ignored_u32)) {
        return false;
    }

    std::uint8_t ignored_u8 = 0U;
    field = NextCsvToken(&cursor);  // Leap seconds.
    if (!ParseU8Dec(field, &ignored_u8)) {
        return false;
    }

    std::uint16_t ignored_u16 = 0U;
    field = NextCsvToken(&cursor);  // Output delay in milliseconds.
    if (!ParseU16Dec(field, &ignored_u16) || NextCsvToken(&cursor) != NULL) {
        return false;
    }

    std::memset(header, 0, sizeof(*header));
    header->message_id = novatel::kMessageIdRange;
    header->message_type = 0U;
    header->port_address = 0xC0U;  // THISPORT, recommended NovAtel binary header value.
    header->sequence = 0U;
    header->idle_time = static_cast<std::uint8_t>(cpu_idle * 2U);
    header->time_status = time_status;
    header->week = week;
    header->milliseconds = milliseconds;
    return true;
}

bool ParseRangeABody(char* body_text,
                     novatel::RangeMeasurement** measurements,
                     std::uint32_t* measurement_count)
{
    if (body_text == NULL || measurements == NULL || measurement_count == NULL) {
        return false;
    }

    char* cursor = body_text;
    char* field = NextCsvToken(&cursor);
    std::uint32_t count = 0U;
    if (!ParseU32Dec(field, &count)) {
        return false;
    }

    novatel::RangeMeasurement* data = NULL;
    if (count != 0U) {
        if (static_cast<std::size_t>(count) >
            (std::numeric_limits<std::size_t>::max() / sizeof(novatel::RangeMeasurement))) {
            return false;
        }
        data = static_cast<novatel::RangeMeasurement*>(
            std::calloc(static_cast<std::size_t>(count), sizeof(novatel::RangeMeasurement)));
        if (data == NULL) {
            return false;
        }
    }

    for (std::uint32_t i = 0U; i < count; ++i) {
        novatel::RangeMeasurement* const obs = &data[i];
        field = NextCsvToken(&cursor);
        if (!ParseU16Dec(field, &obs->prn_slot)) { std::free(data); return false; }
        field = NextCsvToken(&cursor);
        if (!ParseU16Dec(field, &obs->glofreq)) { std::free(data); return false; }
        field = NextCsvToken(&cursor);
        if (!ParseDouble(field, &obs->pseudorange_m)) { std::free(data); return false; }
        field = NextCsvToken(&cursor);
        if (!ParseFloat(field, &obs->pseudorange_std_m)) { std::free(data); return false; }
        field = NextCsvToken(&cursor);
        if (!ParseDouble(field, &obs->adr_cycles)) { std::free(data); return false; }
        field = NextCsvToken(&cursor);
        if (!ParseFloat(field, &obs->adr_std_cycles)) { std::free(data); return false; }
        field = NextCsvToken(&cursor);
        if (!ParseFloat(field, &obs->doppler_hz)) { std::free(data); return false; }
        field = NextCsvToken(&cursor);
        if (!ParseFloat(field, &obs->cn0_db_hz)) { std::free(data); return false; }
        field = NextCsvToken(&cursor);
        if (!ParseFloat(field, &obs->lock_time_s)) { std::free(data); return false; }
        field = NextCsvToken(&cursor);
        if (!ParseU32Hex(field, &obs->tracking_status)) { std::free(data); return false; }
    }

    if (NextCsvToken(&cursor) != NULL) {
        std::free(data);
        return false;
    }

    *measurements = data;
    *measurement_count = count;
    return true;
}

bool ParseObsVmABody(char* body_text,
                     novatel::RangeMeasurement** measurements,
                     std::uint32_t* measurement_count)
{
    if (body_text == NULL || measurements == NULL || measurement_count == NULL) {
        return false;
    }

    char* cursor = body_text;
    char* field = NextCsvToken(&cursor);
    std::uint32_t count = 0U;
    if (!ParseU32Dec(field, &count)) {
        return false;
    }

    novatel::RangeMeasurement* data = NULL;
    if (count != 0U) {
        if (static_cast<std::size_t>(count) >
            (std::numeric_limits<std::size_t>::max() / sizeof(novatel::RangeMeasurement))) {
            return false;
        }
        data = static_cast<novatel::RangeMeasurement*>(
            std::calloc(static_cast<std::size_t>(count), sizeof(novatel::RangeMeasurement)));
        if (data == NULL) {
            return false;
        }
    }

    for (std::uint32_t i = 0U; i < count; ++i) {
        novatel::RangeMeasurement* const obs = &data[i];
        field = NextCsvToken(&cursor);  // System Freq is already GLONASS frequency + 7.
        if (!ParseU16Dec(field, &obs->glofreq)) { std::free(data); return false; }
        field = NextCsvToken(&cursor);
        if (!ParseU16Dec(field, &obs->prn_slot)) { std::free(data); return false; }
        field = NextCsvToken(&cursor);
        if (!ParseDouble(field, &obs->pseudorange_m)) { std::free(data); return false; }
        field = NextCsvToken(&cursor);
        if (!ParseDouble(field, &obs->adr_cycles)) { std::free(data); return false; }

        std::uint16_t psr_std_x100 = 0U;
        field = NextCsvToken(&cursor);
        if (!ParseU16Dec(field, &psr_std_x100)) { std::free(data); return false; }
        obs->pseudorange_std_m = static_cast<float>(psr_std_x100) / 100.0F;

        std::uint16_t adr_std_x10000 = 0U;
        field = NextCsvToken(&cursor);
        if (!ParseU16Dec(field, &adr_std_x10000)) { std::free(data); return false; }
        obs->adr_std_cycles = static_cast<float>(adr_std_x10000) / 10000.0F;

        field = NextCsvToken(&cursor);
        if (!ParseFloat(field, &obs->doppler_hz)) { std::free(data); return false; }

        std::uint16_t cn0_x100 = 0U;
        field = NextCsvToken(&cursor);
        if (!ParseU16Dec(field, &cn0_x100)) { std::free(data); return false; }
        obs->cn0_db_hz = static_cast<float>(cn0_x100) / 100.0F;

        std::uint16_t ignored_reserved = 0U;
        field = NextCsvToken(&cursor);
        if (!ParseU16Dec(field, &ignored_reserved)) { std::free(data); return false; }

        field = NextCsvToken(&cursor);
        if (!ParseFloat(field, &obs->lock_time_s)) { std::free(data); return false; }

        field = NextCsvToken(&cursor);
        if (!ParseU32Hex(field, &obs->tracking_status)) { std::free(data); return false; }
    }

    if (NextCsvToken(&cursor) != NULL) {
        std::free(data);
        return false;
    }

    *measurements = data;
    *measurement_count = count;
    return true;
}

}  // namespace

bool IsSupportedRangeAsciiLine(const char* line)
{
    return DetectAsciiSource(line) != kAsciiSourceUnknown;
}

bool ConvertRangeAsciiLineToBinary(const char* line,
                                   std::uint8_t** record,
                                   std::size_t* record_size,
                                   char* error_text,
                                   std::size_t error_text_size)
{
    if (line == NULL || record == NULL || record_size == NULL) {
        SetError(error_text, error_text_size, "invalid conversion argument");
        return false;
    }
    *record = NULL;
    *record_size = 0U;

    const AsciiSource source = DetectAsciiSource(line);
    if (source == kAsciiSourceUnknown) {
        SetError(error_text, error_text_size, "unsupported ASCII record (expected RANGEA or OBSVMA)");
        return false;
    }

    const std::size_t input_length = std::strlen(line);
    char* mutable_line = static_cast<char*>(std::malloc(input_length + 1U));
    if (mutable_line == NULL) {
        SetError(error_text, error_text_size, "out of memory");
        return false;
    }
    std::memcpy(mutable_line, line, input_length + 1U);

    while (*mutable_line != '\0') {
        const std::size_t len = std::strlen(mutable_line);
        if (len == 0U || (mutable_line[len - 1U] != '\r' && mutable_line[len - 1U] != '\n')) {
            break;
        }
        mutable_line[len - 1U] = '\0';
    }

    char* const star = std::strrchr(mutable_line, '*');
    if (star != NULL) {
        *star = '\0';
    }
    char* const semicolon = std::strchr(mutable_line, ';');
    if (semicolon == NULL || mutable_line[0] != '#') {
        std::free(mutable_line);
        SetError(error_text, error_text_size, "malformed ASCII framing");
        return false;
    }
    *semicolon = '\0';

    char* const header_text = mutable_line + 1U;
    char* const body_text = semicolon + 1U;

    novatel::BinaryHeader header = {};
    bool header_ok = false;
    if (source == kAsciiSourceRangeA) {
        header_ok = ParseRangeAHeader(header_text, &header);
    } else {
        header_ok = ParseObsVmAHeader(header_text, &header);
    }
    if (!header_ok) {
        std::free(mutable_line);
        SetError(error_text, error_text_size, "invalid ASCII header");
        return false;
    }

    novatel::RangeMeasurement* measurements = NULL;
    std::uint32_t count = 0U;
    const bool body_ok = source == kAsciiSourceRangeA
        ? ParseRangeABody(body_text, &measurements, &count)
        : ParseObsVmABody(body_text, &measurements, &count);
    if (!body_ok) {
        std::free(mutable_line);
        SetError(error_text, error_text_size, "invalid RANGE/OBSVM body");
        return false;
    }

    const std::size_t payload_size = novatel::RangePayloadSize(count);
    if (payload_size == 0U || payload_size > 0xFFFFU) {
        std::free(measurements);
        std::free(mutable_line);
        SetError(error_text, error_text_size, "RANGE payload is too large");
        return false;
    }

    const std::size_t total_size = novatel::kBinaryHeaderSize + payload_size + novatel::kBinaryCrcSize;
    std::uint8_t* output = static_cast<std::uint8_t*>(std::malloc(total_size));
    if (output == NULL) {
        std::free(measurements);
        std::free(mutable_line);
        SetError(error_text, error_text_size, "out of memory");
        return false;
    }
    std::memset(output, 0, total_size);

    header.message_length = static_cast<std::uint16_t>(payload_size);
    std::size_t written_payload_size = 0U;
    const bool encoded = novatel::EncodeBinaryHeader(header, output, total_size) &&
        novatel::EncodeRangePayload(measurements,
                                    count,
                                    output + novatel::kBinaryHeaderSize,
                                    payload_size,
                                    &written_payload_size) &&
        written_payload_size == payload_size &&
        novatel::WriteBinaryRecordCrc(output,
                                      novatel::kBinaryHeaderSize + payload_size,
                                      total_size);

    std::free(measurements);
    std::free(mutable_line);

    if (!encoded) {
        std::free(output);
        SetError(error_text, error_text_size, "failed to encode RANGEB record");
        return false;
    }

    *record = output;
    *record_size = total_size;
    return true;
}

bool ConvertRangeBinaryRecordToAscii(const std::uint8_t* record,
                                     std::size_t record_size,
                                     char** line,
                                     std::size_t* line_size,
                                     char* error_text,
                                     std::size_t error_text_size)
{
    if (record == NULL || line == NULL || line_size == NULL) {
        SetError(error_text, error_text_size, "invalid conversion argument");
        return false;
    }
    *line = NULL;
    *line_size = 0U;

    novatel::BinaryHeader header = {};
    if (!novatel::DecodeBinaryHeader(record, record_size, &header) ||
        header.message_id != novatel::kMessageIdRange) {
        SetError(error_text, error_text_size, "binary record is not a standard RANGEB record");
        return false;
    }

    const std::size_t expected_size = novatel::kBinaryHeaderSize +
        static_cast<std::size_t>(header.message_length) + novatel::kBinaryCrcSize;
    if (record_size != expected_size || !novatel::ValidateBinaryRecordCrc(record, record_size)) {
        SetError(error_text, error_text_size, "invalid RANGEB length or CRC");
        return false;
    }

    const std::uint8_t* const payload = record + novatel::kBinaryHeaderSize;
    std::uint32_t count = 0U;
    if (!ReadU32LE(payload, header.message_length, 0U, &count)) {
        SetError(error_text, error_text_size, "invalid RANGE observation count");
        return false;
    }

    novatel::RangeMeasurement* measurements = NULL;
    if (count != 0U) {
        measurements = static_cast<novatel::RangeMeasurement*>(
            std::calloc(static_cast<std::size_t>(count), sizeof(novatel::RangeMeasurement)));
        if (measurements == NULL) {
            SetError(error_text, error_text_size, "out of memory");
            return false;
        }
    }

    std::uint32_t decoded_count = 0U;
    if (!novatel::DecodeRangePayload(payload,
                                     header.message_length,
                                     measurements,
                                     count,
                                     &decoded_count) || decoded_count != count) {
        std::free(measurements);
        SetError(error_text, error_text_size, "invalid RANGE payload");
        return false;
    }

    const char* const time_status = novatel::TimeStatusToAscii(header.time_status);
    if (time_status == NULL) {
        std::free(measurements);
        SetError(error_text, error_text_size, "unknown NovAtel time status");
        return false;
    }

    std::ostringstream out;
    out << "#RANGEA," << PortAddressToAscii(header.port_address)
        << ',' << header.sequence
        << ',' << std::fixed << std::setprecision(1)
        << (static_cast<double>(header.idle_time) / 2.0)
        << ',' << time_status
        << ',' << header.week
        << ',' << std::fixed << std::setprecision(3)
        << (static_cast<double>(header.milliseconds) / 1000.0)
        << ',' << std::hex << std::nouppercase << std::setfill('0') << std::setw(8)
        << header.receiver_status
        << ',' << std::setw(4) << header.reserved
        << std::dec << std::setfill(' ') << ',' << header.receiver_sw_version
        << ';' << count;

    for (std::uint32_t i = 0U; i < count; ++i) {
        const novatel::RangeMeasurement* const obs = &measurements[i];
        out << ',' << obs->prn_slot
            << ',' << obs->glofreq
            << ',' << std::setprecision(17) << std::defaultfloat << obs->pseudorange_m
            << ',' << std::setprecision(9) << obs->pseudorange_std_m
            << ',' << std::setprecision(17) << obs->adr_cycles
            << ',' << std::setprecision(9) << obs->adr_std_cycles
            << ',' << std::setprecision(9) << obs->doppler_hz
            << ',' << std::setprecision(9) << obs->cn0_db_hz
            << ',' << std::setprecision(9) << obs->lock_time_s
            << ',' << std::hex << std::nouppercase << std::setfill('0') << std::setw(8)
            << obs->tracking_status << std::dec << std::setfill(' ');
    }

    const std::string without_crc = out.str();
    const std::uint32_t crc = CalculateNovAtelCrc32(
        reinterpret_cast<const std::uint8_t*>(without_crc.data() + 1U),
        without_crc.size() - 1U);

    out << '*' << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << crc
        << "\r\n";
    const std::string result = out.str();

    char* output = static_cast<char*>(std::malloc(result.size() + 1U));
    if (output == NULL) {
        std::free(measurements);
        SetError(error_text, error_text_size, "out of memory");
        return false;
    }
    std::memcpy(output, result.c_str(), result.size() + 1U);
    std::free(measurements);

    *line = output;
    *line_size = result.size();
    return true;
}

void FreeConvertedBuffer(void* buffer)
{
    std::free(buffer);
}

}  // namespace gnsslog
