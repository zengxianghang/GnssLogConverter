#include "eph_ion_converter.h"

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

namespace gnsslog {
namespace {

enum FieldKind
{
    kFieldU8 = 0,
    kFieldU16,
    kFieldU32,
    kFieldI32,
    kFieldDouble,
    kFieldBool32,
    kFieldHex32
};

struct FieldSpec
{
    FieldKind kind;
    std::size_t offset;
};

struct MessageSpec
{
    const char* ascii_name;
    std::uint16_t message_id;
    std::uint16_t payload_size;
    const FieldSpec* fields;
    std::size_t field_count;
};

static const std::uint16_t kMessageIdGpsEphem = 7U;
static const std::uint16_t kMessageIdGloEphemeris = 723U;
static const std::uint16_t kMessageIdBd2Ephem = 1047U;
static const std::uint16_t kMessageIdGalEphemeris = 1122U;
static const std::uint16_t kMessageIdGalIono = 1127U;
static const std::uint16_t kMessageIdQzssEphemeris = 1336U;
static const std::uint16_t kMessageIdBd2IonUtc = 2010U;
static const std::uint16_t kMessageIdNavicEphemeris = 2123U;
static const std::uint16_t kMessageIdBdsBcnav1 = 2371U;
static const std::uint16_t kMessageIdBdsBcnav2 = 2372U;
static const std::uint16_t kMessageIdBdsBcnav3 = 2412U;

static const FieldSpec kGpsEphemFields[] = {
    {kFieldU32, 0U}, {kFieldDouble, 4U}, {kFieldU32, 12U},
    {kFieldU32, 16U}, {kFieldU32, 20U}, {kFieldU32, 24U},
    {kFieldU32, 28U}, {kFieldDouble, 32U}, {kFieldDouble, 40U},
    {kFieldDouble, 48U}, {kFieldDouble, 56U}, {kFieldDouble, 64U},
    {kFieldDouble, 72U}, {kFieldDouble, 80U}, {kFieldDouble, 88U},
    {kFieldDouble, 96U}, {kFieldDouble, 104U}, {kFieldDouble, 112U},
    {kFieldDouble, 120U}, {kFieldDouble, 128U}, {kFieldDouble, 136U},
    {kFieldDouble, 144U}, {kFieldDouble, 152U}, {kFieldU32, 160U},
    {kFieldDouble, 164U}, {kFieldDouble, 172U}, {kFieldDouble, 180U},
    {kFieldDouble, 188U}, {kFieldDouble, 196U}, {kFieldBool32, 204U},
    {kFieldDouble, 208U}, {kFieldDouble, 216U}
};

static const FieldSpec kGloEphemerisFields[] = {
    {kFieldU16, 0U}, {kFieldU16, 2U}, {kFieldU8, 4U}, {kFieldU8, 5U},
    {kFieldU16, 6U}, {kFieldU32, 8U}, {kFieldU32, 12U}, {kFieldU16, 16U},
    {kFieldU8, 18U}, {kFieldU8, 19U}, {kFieldU32, 20U}, {kFieldU32, 24U},
    {kFieldDouble, 28U}, {kFieldDouble, 36U}, {kFieldDouble, 44U},
    {kFieldDouble, 52U}, {kFieldDouble, 60U}, {kFieldDouble, 68U},
    {kFieldDouble, 76U}, {kFieldDouble, 84U}, {kFieldDouble, 92U},
    {kFieldDouble, 100U}, {kFieldDouble, 108U}, {kFieldDouble, 116U},
    {kFieldU32, 124U}, {kFieldU32, 128U}, {kFieldU32, 132U},
    {kFieldU32, 136U}, {kFieldU32, 140U}
};

static const FieldSpec kQzssEphemerisFields[] = {
    {kFieldU32, 0U}, {kFieldDouble, 4U}, {kFieldU32, 12U},
    {kFieldU32, 16U}, {kFieldU32, 20U}, {kFieldU32, 24U},
    {kFieldU32, 28U}, {kFieldDouble, 32U}, {kFieldDouble, 40U},
    {kFieldDouble, 48U}, {kFieldDouble, 56U}, {kFieldDouble, 64U},
    {kFieldDouble, 72U}, {kFieldDouble, 80U}, {kFieldDouble, 88U},
    {kFieldDouble, 96U}, {kFieldDouble, 104U}, {kFieldDouble, 112U},
    {kFieldDouble, 120U}, {kFieldDouble, 128U}, {kFieldDouble, 136U},
    {kFieldDouble, 144U}, {kFieldDouble, 152U}, {kFieldU32, 160U},
    {kFieldDouble, 164U}, {kFieldDouble, 172U}, {kFieldDouble, 180U},
    {kFieldDouble, 188U}, {kFieldDouble, 196U}, {kFieldBool32, 204U},
    {kFieldDouble, 208U}, {kFieldDouble, 216U},
    {kFieldU8, 224U}, {kFieldU8, 225U}, {kFieldU8, 226U}, {kFieldU8, 227U}
};

static const FieldSpec kGalEphemerisFields[] = {
    {kFieldU32, 0U}, {kFieldBool32, 4U}, {kFieldBool32, 8U},
    {kFieldU8, 12U}, {kFieldU8, 13U}, {kFieldU8, 14U}, {kFieldU8, 15U},
    {kFieldU8, 16U}, {kFieldU8, 17U}, {kFieldU8, 18U}, {kFieldU8, 19U},
    {kFieldU32, 20U}, {kFieldU32, 24U},
    {kFieldDouble, 28U}, {kFieldDouble, 36U}, {kFieldDouble, 44U},
    {kFieldDouble, 52U}, {kFieldDouble, 60U}, {kFieldDouble, 68U},
    {kFieldDouble, 76U}, {kFieldDouble, 84U}, {kFieldDouble, 92U},
    {kFieldDouble, 100U}, {kFieldDouble, 108U}, {kFieldDouble, 116U},
    {kFieldDouble, 124U}, {kFieldDouble, 132U}, {kFieldDouble, 140U},
    {kFieldU32, 148U}, {kFieldDouble, 152U}, {kFieldDouble, 160U},
    {kFieldDouble, 168U}, {kFieldU32, 176U}, {kFieldDouble, 180U},
    {kFieldDouble, 188U}, {kFieldDouble, 196U}, {kFieldDouble, 204U},
    {kFieldDouble, 212U}
};

static const FieldSpec kBd2EphemFields[] = {
    {kFieldU32, 0U}, {kFieldDouble, 4U}, {kFieldU32, 12U},
    {kFieldU32, 16U}, {kFieldU32, 20U}, {kFieldU32, 24U},
    {kFieldU32, 28U}, {kFieldDouble, 32U}, {kFieldDouble, 40U},
    {kFieldDouble, 48U}, {kFieldDouble, 56U}, {kFieldDouble, 64U},
    {kFieldDouble, 72U}, {kFieldDouble, 80U}, {kFieldDouble, 88U},
    {kFieldDouble, 96U}, {kFieldDouble, 104U}, {kFieldDouble, 112U},
    {kFieldDouble, 120U}, {kFieldDouble, 128U}, {kFieldDouble, 136U},
    {kFieldDouble, 144U}, {kFieldDouble, 152U}, {kFieldU32, 160U},
    {kFieldDouble, 164U}, {kFieldDouble, 172U}, {kFieldDouble, 180U},
    {kFieldDouble, 188U}, {kFieldDouble, 196U}, {kFieldDouble, 204U},
    {kFieldBool32, 212U}, {kFieldDouble, 216U}, {kFieldDouble, 224U}
};

static const FieldSpec kIonUtcFields[] = {
    {kFieldDouble, 0U}, {kFieldDouble, 8U}, {kFieldDouble, 16U},
    {kFieldDouble, 24U}, {kFieldDouble, 32U}, {kFieldDouble, 40U},
    {kFieldDouble, 48U}, {kFieldDouble, 56U}, {kFieldU32, 64U},
    {kFieldU32, 68U}, {kFieldDouble, 72U}, {kFieldDouble, 80U},
    {kFieldU32, 88U}, {kFieldU32, 92U}, {kFieldI32, 96U},
    {kFieldI32, 100U}, {kFieldU32, 104U}
};

static const FieldSpec kGalIonoFields[] = {
    {kFieldDouble, 0U}, {kFieldDouble, 8U}, {kFieldDouble, 16U},
    {kFieldU8, 24U}, {kFieldU8, 25U}, {kFieldU8, 26U},
    {kFieldU8, 27U}, {kFieldU8, 28U}
};

static const FieldSpec kNavicEphemerisFields[] = {
    {kFieldU32, 0U}, {kFieldU32, 4U}, {kFieldDouble, 8U},
    {kFieldDouble, 16U}, {kFieldDouble, 24U}, {kFieldU32, 32U},
    {kFieldU32, 36U}, {kFieldDouble, 40U}, {kFieldDouble, 48U},
    {kFieldU32, 56U}, {kFieldU32, 60U}, {kFieldU32, 64U},
    {kFieldU32, 68U}, {kFieldDouble, 72U}, {kFieldDouble, 80U},
    {kFieldDouble, 88U}, {kFieldDouble, 96U}, {kFieldDouble, 104U},
    {kFieldDouble, 112U}, {kFieldDouble, 120U}, {kFieldU32, 128U},
    {kFieldDouble, 132U}, {kFieldU32, 140U}, {kFieldDouble, 144U},
    {kFieldDouble, 152U}, {kFieldDouble, 160U}, {kFieldDouble, 168U},
    {kFieldDouble, 176U}, {kFieldDouble, 184U}, {kFieldU32, 192U},
    {kFieldU32, 196U}, {kFieldU32, 200U}
};

static const FieldSpec kBdsBcnav12Fields[] = {
    {kFieldU32, 0U}, {kFieldU32, 4U}, {kFieldHex32, 8U},
    {kFieldU32, 12U}, {kFieldU32, 16U}, {kFieldU32, 20U},
    {kFieldDouble, 24U}, {kFieldDouble, 32U}, {kFieldDouble, 40U},
    {kFieldDouble, 48U}, {kFieldDouble, 56U}, {kFieldDouble, 64U},
    {kFieldDouble, 72U}, {kFieldDouble, 80U}, {kFieldDouble, 88U},
    {kFieldDouble, 96U}, {kFieldDouble, 104U}, {kFieldDouble, 112U},
    {kFieldDouble, 120U}, {kFieldDouble, 128U}, {kFieldDouble, 136U},
    {kFieldDouble, 144U}, {kFieldDouble, 152U}, {kFieldU32, 160U},
    {kFieldU32, 164U}, {kFieldDouble, 168U}, {kFieldDouble, 176U},
    {kFieldDouble, 184U}, {kFieldDouble, 192U}, {kFieldDouble, 200U},
    {kFieldDouble, 208U}, {kFieldU32, 216U}
};

static const FieldSpec kBdsBcnav3Fields[] = {
    {kFieldU32, 0U}, {kFieldU32, 4U}, {kFieldHex32, 8U},
    {kFieldU32, 12U}, {kFieldU32, 16U}, {kFieldDouble, 20U},
    {kFieldDouble, 28U}, {kFieldDouble, 36U}, {kFieldDouble, 44U},
    {kFieldDouble, 52U}, {kFieldDouble, 60U}, {kFieldDouble, 68U},
    {kFieldDouble, 76U}, {kFieldDouble, 84U}, {kFieldDouble, 92U},
    {kFieldDouble, 100U}, {kFieldDouble, 108U}, {kFieldDouble, 116U},
    {kFieldDouble, 124U}, {kFieldDouble, 132U}, {kFieldDouble, 140U},
    {kFieldDouble, 148U}, {kFieldU32, 156U}, {kFieldDouble, 160U},
    {kFieldDouble, 168U}, {kFieldDouble, 176U}, {kFieldDouble, 184U},
    {kFieldU32, 192U}
};

static const FieldSpec kUnicoreIonFields[] = {
    {kFieldDouble, 0U}, {kFieldDouble, 8U}, {kFieldDouble, 16U},
    {kFieldDouble, 24U}, {kFieldDouble, 32U}, {kFieldDouble, 40U},
    {kFieldDouble, 48U}, {kFieldDouble, 56U}, {kFieldU16, 64U},
    {kFieldU16, 66U}, {kFieldU32, 68U}, {kFieldU32, 72U}
};

static const FieldSpec kUnicoreGalIonFields[] = {
    {kFieldDouble, 0U}, {kFieldDouble, 8U}, {kFieldDouble, 16U},
    {kFieldU8, 24U}, {kFieldU8, 25U}, {kFieldU8, 26U},
    {kFieldU8, 27U}, {kFieldU8, 28U}, {kFieldU32, 29U}
};

static const FieldSpec kIrnssEphemFields[] = {
    {kFieldU32, 0U}, {kFieldDouble, 4U}, {kFieldU32, 12U},
    {kFieldU32, 16U}, {kFieldU32, 20U}, {kFieldU32, 24U},
    {kFieldU32, 28U}, {kFieldDouble, 32U}, {kFieldDouble, 40U},
    {kFieldDouble, 48U}, {kFieldDouble, 56U}, {kFieldDouble, 64U},
    {kFieldDouble, 72U}, {kFieldDouble, 80U}, {kFieldDouble, 88U},
    {kFieldDouble, 96U}, {kFieldDouble, 104U}, {kFieldDouble, 112U},
    {kFieldDouble, 120U}, {kFieldDouble, 128U}, {kFieldDouble, 136U},
    {kFieldDouble, 144U}, {kFieldDouble, 152U}, {kFieldU32, 160U},
    {kFieldDouble, 164U}, {kFieldDouble, 172U}, {kFieldDouble, 180U},
    {kFieldDouble, 188U}, {kFieldDouble, 196U}, {kFieldU32, 204U},
    {kFieldDouble, 208U}, {kFieldDouble, 216U}
};

static const FieldSpec kBd3EphemFields[] = {
    {kFieldU8, 0U}, {kFieldU8, 1U}, {kFieldU8, 2U}, {kFieldU8, 3U},
    {kFieldU16, 4U}, {kFieldU16, 6U}, {kFieldU16, 8U}, {kFieldU16, 10U},
    {kFieldDouble, 12U}, {kFieldDouble, 20U}, {kFieldDouble, 28U},
    {kFieldDouble, 36U}, {kFieldDouble, 44U}, {kFieldDouble, 52U},
    {kFieldDouble, 60U}, {kFieldDouble, 68U}, {kFieldDouble, 76U},
    {kFieldDouble, 84U}, {kFieldDouble, 92U}, {kFieldDouble, 100U},
    {kFieldDouble, 108U}, {kFieldDouble, 116U}, {kFieldDouble, 124U},
    {kFieldDouble, 132U}, {kFieldDouble, 140U}, {kFieldDouble, 148U},
    {kFieldDouble, 156U}, {kFieldDouble, 164U}, {kFieldDouble, 172U},
    {kFieldDouble, 180U}, {kFieldDouble, 188U}, {kFieldDouble, 196U},
    {kFieldDouble, 204U}, {kFieldDouble, 212U}, {kFieldDouble, 220U},
    {kFieldDouble, 228U}, {kFieldDouble, 236U}, {kFieldI32, 244U},
    {kFieldU8, 248U}, {kFieldU8, 249U}, {kFieldU8, 250U}, {kFieldU8, 251U},
    {kFieldI32, 252U}, {kFieldI32, 256U}, {kFieldU32, 260U}
};

#define FIELD_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const MessageSpec kNativeMessages[] = {
    {"GPSEPHEMA", kMessageIdGpsEphem, 224U, kGpsEphemFields, FIELD_COUNT(kGpsEphemFields)},
    {"GLOEPHEMERISA", kMessageIdGloEphemeris, 144U, kGloEphemerisFields, FIELD_COUNT(kGloEphemerisFields)},
    {"QZSSEPHEMERISA", kMessageIdQzssEphemeris, 228U, kQzssEphemerisFields, FIELD_COUNT(kQzssEphemerisFields)},
    {"GALEPHEMERISA", kMessageIdGalEphemeris, 220U, kGalEphemerisFields, FIELD_COUNT(kGalEphemerisFields)},
    {"BD2EPHEMA", kMessageIdBd2Ephem, 232U, kBd2EphemFields, FIELD_COUNT(kBd2EphemFields)},
    {"IONUTCA", 8U, 108U, kIonUtcFields, FIELD_COUNT(kIonUtcFields)},
    {"BD2IONUTCA", kMessageIdBd2IonUtc, 108U, kIonUtcFields, FIELD_COUNT(kIonUtcFields)},
    {"GALIONOA", kMessageIdGalIono, 29U, kGalIonoFields, FIELD_COUNT(kGalIonoFields)},
    {"NAVICEPHEMERISA", kMessageIdNavicEphemeris, 204U, kNavicEphemerisFields, FIELD_COUNT(kNavicEphemerisFields)},
    {"BDSBCNAV1EPHEMERISA", kMessageIdBdsBcnav1, 220U, kBdsBcnav12Fields, FIELD_COUNT(kBdsBcnav12Fields)},
    {"BDSBCNAV2EPHEMERISA", kMessageIdBdsBcnav2, 220U, kBdsBcnav12Fields, FIELD_COUNT(kBdsBcnav12Fields)},
    {"BDSBCNAV3EPHEMERISA", kMessageIdBdsBcnav3, 196U, kBdsBcnav3Fields, FIELD_COUNT(kBdsBcnav3Fields)}
};

void SetError(char* text, std::size_t size, const char* message)
{
    if (text != NULL && size != 0U) {
        std::snprintf(text, size, "%s", message != NULL ? message : "conversion failed");
    }
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
    if (text == NULL || *text == '\0' || value == NULL || *text == '-' || *text == '+') {
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

bool ParseU8(const char* text, std::uint8_t* value)
{
    unsigned long parsed = 0UL;
    if (value == NULL || !ParseUnsigned(text, 0xFFUL, 10, &parsed)) return false;
    *value = static_cast<std::uint8_t>(parsed);
    return true;
}

bool ParseU16(const char* text, std::uint16_t* value)
{
    unsigned long parsed = 0UL;
    if (value == NULL || !ParseUnsigned(text, 0xFFFFUL, 10, &parsed)) return false;
    *value = static_cast<std::uint16_t>(parsed);
    return true;
}

bool ParseU32(const char* text, std::uint32_t* value)
{
    unsigned long parsed = 0UL;
    if (value == NULL || !ParseUnsigned(text, 0xFFFFFFFFUL, 10, &parsed)) return false;
    *value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool ParseHex32(const char* text, std::uint32_t* value)
{
    unsigned long parsed = 0UL;
    if (value == NULL || !ParseUnsigned(text, 0xFFFFFFFFUL, 16, &parsed)) return false;
    *value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool ParseHex16(const char* text, std::uint16_t* value)
{
    unsigned long parsed = 0UL;
    if (value == NULL || !ParseUnsigned(text, 0xFFFFUL, 16, &parsed)) return false;
    *value = static_cast<std::uint16_t>(parsed);
    return true;
}

bool ParseI32(const char* text, std::int32_t* value)
{
    if (text == NULL || *text == '\0' || value == NULL) return false;
    char* end = NULL;
    const long parsed = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' ||
        parsed < static_cast<long>(std::numeric_limits<std::int32_t>::min()) ||
        parsed > static_cast<long>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    *value = static_cast<std::int32_t>(parsed);
    return true;
}

bool ParseDoubleValue(const char* text, double* value)
{
    if (text == NULL || *text == '\0' || value == NULL) return false;
    char* end = NULL;
    const double parsed = std::strtod(text, &end);
    if (end == text || *end != '\0' || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
}

bool ParseBool32(const char* text, std::uint32_t* value)
{
    if (text == NULL || value == NULL) return false;
    if (std::strcmp(text, "TRUE") == 0 || std::strcmp(text, "1") == 0) {
        *value = 1U;
        return true;
    }
    if (std::strcmp(text, "FALSE") == 0 || std::strcmp(text, "0") == 0) {
        *value = 0U;
        return true;
    }
    return false;
}

bool EncodeIdlePercent(double percent, std::uint8_t* value)
{
    if (value == NULL || !std::isfinite(percent) || percent < 0.0 || percent > 100.0) return false;
    const double encoded = std::floor(percent * 2.0 + 0.5);
    if (encoded < 0.0 || encoded > 200.0) return false;
    *value = static_cast<std::uint8_t>(encoded);
    return true;
}

bool PortAddressFromAscii(const char* text, std::uint8_t* value)
{
    if (text == NULL || value == NULL) return false;
    struct Entry { const char* name; std::uint8_t value; };
    static const Entry table[] = {
        {"NO_PORTS", 0x00U}, {"NOPORT", 0x00U}, {"COM1", 0x20U},
        {"COM2", 0x40U}, {"COM3", 0x60U}, {"SPECIAL", 0xA0U},
        {"THISPORT", 0xC0U}, {"FILE", 0xE0U},
        {"USB1", 0xA0U}, {"USB2", 0xA0U}, {"USB3", 0xA0U},
        {"AUX", 0xA0U}, {"COM4", 0xA0U}, {"ETH1", 0xA0U},
        {"IMU", 0xA0U}, {"ICOM1", 0xA0U}, {"ICOM2", 0xA0U},
        {"ICOM3", 0xA0U}, {"ICOM4", 0xA0U}
    };
    for (std::size_t i = 0U; i < FIELD_COUNT(table); ++i) {
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

const MessageSpec* FindNativeByName(const char* name)
{
    if (name == NULL) return NULL;
    for (std::size_t i = 0U; i < FIELD_COUNT(kNativeMessages); ++i) {
        if (std::strcmp(name, kNativeMessages[i].ascii_name) == 0) return &kNativeMessages[i];
    }
    return NULL;
}

const MessageSpec* FindNativeById(std::uint16_t id)
{
    for (std::size_t i = 0U; i < FIELD_COUNT(kNativeMessages); ++i) {
        if (id == kNativeMessages[i].message_id) return &kNativeMessages[i];
    }
    return NULL;
}

bool IsUnicoreSourceName(const char* name)
{
    if (name == NULL) return false;
    static const char* const names[] = {
        "GPSEPHA", "GLOEPHA", "QZSSEPHA", "GALEPHA", "BDSEPHA",
        "GPSIONA", "BDSIONA", "GALIONA", "IRNSSEPHA", "BD3EPHA"
    };
    for (std::size_t i = 0U; i < FIELD_COUNT(names); ++i) {
        if (std::strcmp(name, names[i]) == 0) return true;
    }
    return false;
}

bool ExtractAsciiName(const char* line, char* name, std::size_t name_size)
{
    if (line == NULL || name == NULL || name_size < 2U || line[0] != '#') return false;
    const char* comma = std::strchr(line + 1, ',');
    if (comma == NULL) return false;
    const std::size_t length = static_cast<std::size_t>(comma - (line + 1));
    if (length == 0U || length >= name_size) return false;
    std::memcpy(name, line + 1, length);
    name[length] = '\0';
    return true;
}

bool ParseNovAtelAsciiHeader(char* text, const char* expected_name, novatel::BinaryHeader* header)
{
    if (text == NULL || expected_name == NULL || header == NULL) return false;
    char* cursor = text;
    char* field = NextCsvToken(&cursor);
    if (field == NULL || std::strcmp(field, expected_name) != 0) return false;

    std::uint8_t port = 0U;
    field = NextCsvToken(&cursor);
    if (!PortAddressFromAscii(field, &port)) return false;

    std::uint16_t sequence = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU16(field, &sequence)) return false;

    double idle_percent = 0.0;
    field = NextCsvToken(&cursor);
    if (!ParseDoubleValue(field, &idle_percent)) return false;
    std::uint8_t idle = 0U;
    if (!EncodeIdlePercent(idle_percent, &idle)) return false;

    std::uint8_t time_status = 0U;
    field = NextCsvToken(&cursor);
    if (!novatel::TimeStatusFromAscii(field, &time_status)) return false;

    std::uint16_t week = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU16(field, &week)) return false;

    double seconds = 0.0;
    field = NextCsvToken(&cursor);
    if (!ParseDoubleValue(field, &seconds) || seconds < 0.0 || seconds >= 604800.0) return false;
    const double milliseconds = std::floor(seconds * 1000.0 + 0.5);
    if (milliseconds < 0.0 || milliseconds > 604800000.0) return false;

    std::uint32_t receiver_status = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseHex32(field, &receiver_status)) return false;

    std::uint16_t reserved = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseHex16(field, &reserved)) return false;

    std::uint16_t sw_version = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU16(field, &sw_version) || NextCsvToken(&cursor) != NULL) return false;

    std::memset(header, 0, sizeof(*header));
    header->message_type = 0U;
    header->port_address = port;
    header->sequence = sequence;
    header->idle_time = idle;
    header->time_status = time_status;
    header->week = week;
    header->milliseconds = static_cast<std::uint32_t>(milliseconds);
    header->receiver_status = receiver_status;
    header->reserved = reserved;
    header->receiver_sw_version = sw_version;
    return true;
}

bool ParseUnicoreAsciiHeader(char* text, const char* expected_name, novatel::BinaryHeader* header)
{
    if (text == NULL || expected_name == NULL || header == NULL) return false;
    char* cursor = text;
    char* field = NextCsvToken(&cursor);
    if (field == NULL || std::strcmp(field, expected_name) != 0) return false;

    std::uint8_t cpu_idle = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU8(field, &cpu_idle) || cpu_idle > 100U) return false;

    field = NextCsvToken(&cursor);
    if (field == NULL || (std::strcmp(field, "GPS") != 0 && std::strcmp(field, "GPST") != 0)) return false;

    std::uint8_t time_status = 0U;
    field = NextCsvToken(&cursor);
    if (!novatel::TimeStatusFromAscii(field, &time_status)) return false;

    std::uint16_t week = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU16(field, &week)) return false;

    std::uint32_t milliseconds = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU32(field, &milliseconds) || milliseconds > 604800000U) return false;

    std::uint32_t ignored_u32 = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU32(field, &ignored_u32)) return false;
    field = NextCsvToken(&cursor);
    if (!ParseU32(field, &ignored_u32)) return false;

    std::uint8_t ignored_u8 = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU8(field, &ignored_u8)) return false;

    std::uint16_t ignored_u16 = 0U;
    field = NextCsvToken(&cursor);
    if (!ParseU16(field, &ignored_u16) || NextCsvToken(&cursor) != NULL) return false;

    std::memset(header, 0, sizeof(*header));
    header->message_type = 0U;
    header->port_address = 0xC0U;
    header->sequence = 0U;
    header->idle_time = static_cast<std::uint8_t>(cpu_idle * 2U);
    header->time_status = time_status;
    header->week = week;
    header->milliseconds = milliseconds;
    return true;
}

bool EncodeFields(char* body,
                  const FieldSpec* fields,
                  std::size_t field_count,
                  std::uint8_t* payload,
                  std::size_t payload_size)
{
    if (body == NULL || fields == NULL || payload == NULL) return false;
    char* cursor = body;
    for (std::size_t i = 0U; i < field_count; ++i) {
        char* const token = NextCsvToken(&cursor);
        if (token == NULL) return false;
        const FieldSpec& spec = fields[i];
        bool ok = false;
        if (spec.kind == kFieldU8) {
            std::uint8_t value = 0U;
            ok = ParseU8(token, &value) && WriteU8(payload, payload_size, spec.offset, value);
        } else if (spec.kind == kFieldU16) {
            std::uint16_t value = 0U;
            ok = ParseU16(token, &value) && WriteU16LE(payload, payload_size, spec.offset, value);
        } else if (spec.kind == kFieldU32) {
            std::uint32_t value = 0U;
            ok = ParseU32(token, &value) && WriteU32LE(payload, payload_size, spec.offset, value);
        } else if (spec.kind == kFieldI32) {
            std::int32_t value = 0;
            ok = ParseI32(token, &value) && WriteU32LE(payload, payload_size, spec.offset,
                                                    static_cast<std::uint32_t>(value));
        } else if (spec.kind == kFieldDouble) {
            double value = 0.0;
            ok = ParseDoubleValue(token, &value) && WriteDoubleLE(payload, payload_size, spec.offset, value);
        } else if (spec.kind == kFieldBool32) {
            std::uint32_t value = 0U;
            ok = ParseBool32(token, &value) && WriteU32LE(payload, payload_size, spec.offset, value);
        } else if (spec.kind == kFieldHex32) {
            std::uint32_t value = 0U;
            ok = ParseHex32(token, &value) && WriteU32LE(payload, payload_size, spec.offset, value);
        }
        if (!ok) return false;
    }
    return NextCsvToken(&cursor) == NULL;
}

bool AppendDecodedFields(std::ostringstream& out,
                         const FieldSpec* fields,
                         std::size_t field_count,
                         const std::uint8_t* payload,
                         std::size_t payload_size)
{
    if (fields == NULL || payload == NULL) return false;
    for (std::size_t i = 0U; i < field_count; ++i) {
        if (i != 0U) out << ',';
        const FieldSpec& spec = fields[i];
        if (spec.kind == kFieldU8) {
            std::uint8_t value = 0U;
            if (!ReadU8(payload, payload_size, spec.offset, &value)) return false;
            out << static_cast<unsigned int>(value);
        } else if (spec.kind == kFieldU16) {
            std::uint16_t value = 0U;
            if (!ReadU16LE(payload, payload_size, spec.offset, &value)) return false;
            out << value;
        } else if (spec.kind == kFieldU32 || spec.kind == kFieldBool32 || spec.kind == kFieldHex32) {
            std::uint32_t value = 0U;
            if (!ReadU32LE(payload, payload_size, spec.offset, &value)) return false;
            if (spec.kind == kFieldBool32) {
                out << (value != 0U ? "TRUE" : "FALSE");
            } else if (spec.kind == kFieldHex32) {
                out << std::hex << std::nouppercase << std::setfill('0') << std::setw(8)
                    << value << std::dec << std::setfill(' ');
            } else {
                out << value;
            }
        } else if (spec.kind == kFieldI32) {
            std::uint32_t raw = 0U;
            if (!ReadU32LE(payload, payload_size, spec.offset, &raw)) return false;
            out << static_cast<std::int32_t>(raw);
        } else if (spec.kind == kFieldDouble) {
            double value = 0.0;
            if (!ReadDoubleLE(payload, payload_size, spec.offset, &value)) return false;
            out << std::setprecision(17) << std::defaultfloat << value;
        } else {
            return false;
        }
    }
    return true;
}

bool BuildRecord(const novatel::BinaryHeader& source_header,
                 std::uint16_t message_id,
                 const std::uint8_t* payload,
                 std::uint16_t payload_size,
                 std::uint8_t** record,
                 std::size_t* record_size)
{
    if (payload == NULL || record == NULL || record_size == NULL) return false;
    const std::size_t total_size = novatel::kBinaryHeaderSize +
        static_cast<std::size_t>(payload_size) + novatel::kBinaryCrcSize;
    std::uint8_t* output = static_cast<std::uint8_t*>(std::malloc(total_size));
    if (output == NULL) return false;
    std::memset(output, 0, total_size);

    novatel::BinaryHeader header = source_header;
    header.message_id = message_id;
    header.message_length = payload_size;
    const bool ok = novatel::EncodeBinaryHeader(header, output, total_size) &&
        (std::memcpy(output + novatel::kBinaryHeaderSize, payload, payload_size) != NULL) &&
        novatel::WriteBinaryRecordCrc(output,
                                      novatel::kBinaryHeaderSize + payload_size,
                                      total_size);
    if (!ok) {
        std::free(output);
        return false;
    }
    *record = output;
    *record_size = total_size;
    return true;
}

bool DoubleToU32(double value, std::uint32_t* output)
{
    if (output == NULL || !std::isfinite(value) || value < 0.0 ||
        value > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) return false;
    const double rounded = std::floor(value + 0.5);
    if (std::fabs(value - rounded) > 1.0e-6) return false;
    *output = static_cast<std::uint32_t>(rounded);
    return true;
}

std::uint32_t NavicUraIndexFromVariance(double variance)
{
    if (!std::isfinite(variance) || variance < 0.0) return 15U;
    const double sigma = std::sqrt(variance);
    static const double limits[] = {
        2.40, 3.40, 4.85, 6.85, 9.65, 13.65, 24.0, 48.0,
        96.0, 192.0, 384.0, 768.0, 1536.0, 3072.0, 6144.0
    };
    for (std::uint32_t i = 0U; i < 15U; ++i) {
        if (sigma <= limits[i]) return i;
    }
    return 15U;
}

bool MapQzssFromUnicore(char* body, std::uint8_t* target)
{
    std::uint8_t source[224] = {};
    if (!EncodeFields(body, kGpsEphemFields, FIELD_COUNT(kGpsEphemFields), source, sizeof(source))) return false;
    std::uint32_t prn = 0U;
    if (!ReadU32LE(source, sizeof(source), 0U, &prn) || prn < 1U || prn > 10U) return false;
    std::memset(target, 0, 228U);
    std::memcpy(target, source, sizeof(source));
    return WriteU32LE(target, 228U, 0U, prn + 192U);
}

bool MapBd2FromUnicore(char* body, std::uint8_t* target)
{
    if (!EncodeFields(body, kBd2EphemFields, FIELD_COUNT(kBd2EphemFields), target, 232U)) return false;
    std::uint32_t prn = 0U;
    if (!ReadU32LE(target, 232U, 0U, &prn) || prn < 1U || prn > 63U) return false;
    return WriteU32LE(target, 232U, 0U, prn + 160U);
}

bool MapIonFromUnicore(char* body, std::uint8_t* target)
{
    std::uint8_t source[76] = {};
    if (!EncodeFields(body, kUnicoreIonFields, FIELD_COUNT(kUnicoreIonFields), source, sizeof(source))) return false;
    std::memset(target, 0, 108U);
    std::memcpy(target, source, 64U);
    return true;
}

bool MapGalIonFromUnicore(char* body, std::uint8_t* target)
{
    std::uint8_t source[33] = {};
    if (!EncodeFields(body, kUnicoreGalIonFields, FIELD_COUNT(kUnicoreGalIonFields), source, sizeof(source))) return false;
    std::memcpy(target, source, 29U);
    return true;
}

bool MapIrnssFromUnicore(char* body, std::uint8_t* target)
{
    std::uint8_t source[224] = {};
    if (!EncodeFields(body, kIrnssEphemFields, FIELD_COUNT(kIrnssEphemFields), source, sizeof(source))) return false;
    std::memset(target, 0, 204U);

    std::uint32_t prn = 0U, gps_week = 0U, l5_health = 0U, iodec = 0U;
    std::uint32_t s_health = 0U, reserved1 = 0U, reserved2 = 0U, flags = 0U;
    double toe = 0.0, semi_major = 0.0, delta_n = 0.0, m0 = 0.0, ecc = 0.0;
    double omega = 0.0, cuc = 0.0, cus = 0.0, crc = 0.0, crs = 0.0;
    double cic = 0.0, cis = 0.0, i0 = 0.0, idot = 0.0, omega0 = 0.0;
    double omega_dot = 0.0, toc = 0.0, tgd = 0.0, af0 = 0.0, af1 = 0.0, af2 = 0.0;
    double ura_variance = 0.0;

    if (!ReadU32LE(source, sizeof(source), 0U, &prn) ||
        !ReadU32LE(source, sizeof(source), 12U, &l5_health) ||
        !ReadU32LE(source, sizeof(source), 16U, &iodec) ||
        !ReadU32LE(source, sizeof(source), 20U, &s_health) ||
        !ReadU32LE(source, sizeof(source), 24U, &gps_week) || gps_week < 1024U ||
        !ReadU32LE(source, sizeof(source), 28U, &reserved1) ||
        !ReadDoubleLE(source, sizeof(source), 32U, &toe) ||
        !ReadDoubleLE(source, sizeof(source), 40U, &semi_major) || semi_major <= 0.0 ||
        !ReadDoubleLE(source, sizeof(source), 48U, &delta_n) ||
        !ReadDoubleLE(source, sizeof(source), 56U, &m0) ||
        !ReadDoubleLE(source, sizeof(source), 64U, &ecc) ||
        !ReadDoubleLE(source, sizeof(source), 72U, &omega) ||
        !ReadDoubleLE(source, sizeof(source), 80U, &cuc) ||
        !ReadDoubleLE(source, sizeof(source), 88U, &cus) ||
        !ReadDoubleLE(source, sizeof(source), 96U, &crc) ||
        !ReadDoubleLE(source, sizeof(source), 104U, &crs) ||
        !ReadDoubleLE(source, sizeof(source), 112U, &cic) ||
        !ReadDoubleLE(source, sizeof(source), 120U, &cis) ||
        !ReadDoubleLE(source, sizeof(source), 128U, &i0) ||
        !ReadDoubleLE(source, sizeof(source), 136U, &idot) ||
        !ReadDoubleLE(source, sizeof(source), 144U, &omega0) ||
        !ReadDoubleLE(source, sizeof(source), 152U, &omega_dot) ||
        !ReadU32LE(source, sizeof(source), 160U, &reserved2) ||
        !ReadDoubleLE(source, sizeof(source), 164U, &toc) ||
        !ReadDoubleLE(source, sizeof(source), 172U, &tgd) ||
        !ReadDoubleLE(source, sizeof(source), 180U, &af0) ||
        !ReadDoubleLE(source, sizeof(source), 188U, &af1) ||
        !ReadDoubleLE(source, sizeof(source), 196U, &af2) ||
        !ReadU32LE(source, sizeof(source), 204U, &flags) ||
        !ReadDoubleLE(source, sizeof(source), 216U, &ura_variance)) return false;

    std::uint32_t toe_u32 = 0U, toc_u32 = 0U;
    if (!DoubleToU32(toe, &toe_u32) || !DoubleToU32(toc, &toc_u32)) return false;

    return WriteU32LE(target, 204U, 0U, prn) &&
        WriteU32LE(target, 204U, 4U, gps_week - 1024U) &&
        WriteDoubleLE(target, 204U, 8U, af0) &&
        WriteDoubleLE(target, 204U, 16U, af1) &&
        WriteDoubleLE(target, 204U, 24U, af2) &&
        WriteU32LE(target, 204U, 32U, NavicUraIndexFromVariance(ura_variance)) &&
        WriteU32LE(target, 204U, 36U, toc_u32) &&
        WriteDoubleLE(target, 204U, 40U, tgd) &&
        WriteDoubleLE(target, 204U, 48U, delta_n) &&
        WriteU32LE(target, 204U, 56U, iodec) &&
        WriteU32LE(target, 204U, 60U, reserved1) &&
        WriteU32LE(target, 204U, 64U, l5_health) &&
        WriteU32LE(target, 204U, 68U, s_health) &&
        WriteDoubleLE(target, 204U, 72U, cuc) &&
        WriteDoubleLE(target, 204U, 80U, cus) &&
        WriteDoubleLE(target, 204U, 88U, cic) &&
        WriteDoubleLE(target, 204U, 96U, cis) &&
        WriteDoubleLE(target, 204U, 104U, crc) &&
        WriteDoubleLE(target, 204U, 112U, crs) &&
        WriteDoubleLE(target, 204U, 120U, idot) &&
        WriteU32LE(target, 204U, 128U, reserved2) &&
        WriteDoubleLE(target, 204U, 132U, m0) &&
        WriteU32LE(target, 204U, 140U, toe_u32) &&
        WriteDoubleLE(target, 204U, 144U, ecc) &&
        WriteDoubleLE(target, 204U, 152U, std::sqrt(semi_major)) &&
        WriteDoubleLE(target, 204U, 160U, omega0) &&
        WriteDoubleLE(target, 204U, 168U, omega) &&
        WriteDoubleLE(target, 204U, 176U, omega_dot) &&
        WriteDoubleLE(target, 204U, 184U, i0) &&
        WriteU32LE(target, 204U, 192U, 0U) &&
        WriteU32LE(target, 204U, 196U, flags & 1U) &&
        WriteU32LE(target, 204U, 200U, (flags >> 1U) & 1U);
}

bool MapBd3FromUnicore(char* body,
                       std::uint16_t* target_message_id,
                       std::uint16_t* target_size,
                       std::uint8_t* target,
                       std::size_t target_capacity)
{
    if (target_message_id == NULL || target_size == NULL || target == NULL || target_capacity < 220U) return false;
    std::uint8_t source[264] = {};
    if (!EncodeFields(body, kBd3EphemFields, FIELD_COUNT(kBd3EphemFields), source, sizeof(source))) return false;

    std::uint8_t prn = 0U, health = 0U, sat_type = 0U, sismai = 0U;
    std::uint16_t iode = 0U, iodc = 0U, gps_week = 0U;
    std::uint32_t freq_type = 0U;
    double toe = 0.0, toc = 0.0;
    if (!ReadU8(source, sizeof(source), 0U, &prn) ||
        !ReadU8(source, sizeof(source), 1U, &health) ||
        !ReadU8(source, sizeof(source), 2U, &sat_type) ||
        !ReadU8(source, sizeof(source), 3U, &sismai) ||
        !ReadU16LE(source, sizeof(source), 4U, &iode) ||
        !ReadU16LE(source, sizeof(source), 6U, &iodc) ||
        !ReadU16LE(source, sizeof(source), 8U, &gps_week) || gps_week < 1356U ||
        !ReadDoubleLE(source, sizeof(source), 20U, &toe) ||
        !ReadDoubleLE(source, sizeof(source), 164U, &toc) ||
        !ReadU32LE(source, sizeof(source), 260U, &freq_type) || freq_type > 2U) return false;

    std::uint32_t toe_u32 = 0U, toc_u32 = 0U;
    if (!DoubleToU32(toe, &toe_u32) || !DoubleToU32(toc, &toc_u32)) return false;

    const std::uint32_t status = (static_cast<std::uint32_t>(health) & 0x3U) |
        ((static_cast<std::uint32_t>(sismai) & 0x0FU) << 5U);
    const std::uint32_t bdt_week = static_cast<std::uint32_t>(gps_week) - 1356U;
    std::memset(target, 0, target_capacity);

    double delta_a = 0.0, adot = 0.0, delta_n = 0.0, ndot = 0.0, m0 = 0.0;
    double ecc = 0.0, omega = 0.0, cuc = 0.0, cus = 0.0, crc = 0.0, crs = 0.0;
    double cic = 0.0, cis = 0.0, i0 = 0.0, idot = 0.0, omega0 = 0.0, omega_dot = 0.0;
    double tgdb1cp = 0.0, tgdb2ap = 0.0, tgdb2bi = 0.0, iscb2ad = 0.0, iscb1cd = 0.0;
    double af0 = 0.0, af1 = 0.0, af2 = 0.0;
    if (!ReadDoubleLE(source, sizeof(source), 28U, &delta_a) ||
        !ReadDoubleLE(source, sizeof(source), 36U, &adot) ||
        !ReadDoubleLE(source, sizeof(source), 44U, &delta_n) ||
        !ReadDoubleLE(source, sizeof(source), 52U, &ndot) ||
        !ReadDoubleLE(source, sizeof(source), 60U, &m0) ||
        !ReadDoubleLE(source, sizeof(source), 68U, &ecc) ||
        !ReadDoubleLE(source, sizeof(source), 76U, &omega) ||
        !ReadDoubleLE(source, sizeof(source), 84U, &cuc) ||
        !ReadDoubleLE(source, sizeof(source), 92U, &cus) ||
        !ReadDoubleLE(source, sizeof(source), 100U, &crc) ||
        !ReadDoubleLE(source, sizeof(source), 108U, &crs) ||
        !ReadDoubleLE(source, sizeof(source), 116U, &cic) ||
        !ReadDoubleLE(source, sizeof(source), 124U, &cis) ||
        !ReadDoubleLE(source, sizeof(source), 132U, &i0) ||
        !ReadDoubleLE(source, sizeof(source), 140U, &idot) ||
        !ReadDoubleLE(source, sizeof(source), 148U, &omega0) ||
        !ReadDoubleLE(source, sizeof(source), 156U, &omega_dot) ||
        !ReadDoubleLE(source, sizeof(source), 172U, &tgdb1cp) ||
        !ReadDoubleLE(source, sizeof(source), 180U, &tgdb2ap) ||
        !ReadDoubleLE(source, sizeof(source), 188U, &tgdb2bi) ||
        !ReadDoubleLE(source, sizeof(source), 204U, &iscb2ad) ||
        !ReadDoubleLE(source, sizeof(source), 212U, &iscb1cd) ||
        !ReadDoubleLE(source, sizeof(source), 220U, &af0) ||
        !ReadDoubleLE(source, sizeof(source), 228U, &af1) ||
        !ReadDoubleLE(source, sizeof(source), 236U, &af2)) return false;

    if (freq_type == 2U) {
        *target_message_id = kMessageIdBdsBcnav3;
        *target_size = 196U;
        return WriteU32LE(target, 196U, 0U, prn) &&
            WriteU32LE(target, 196U, 4U, bdt_week) && WriteU32LE(target, 196U, 8U, status) &&
            WriteU32LE(target, 196U, 12U, toe_u32) && WriteU32LE(target, 196U, 16U, sat_type) &&
            WriteDoubleLE(target, 196U, 20U, delta_a) && WriteDoubleLE(target, 196U, 28U, adot) &&
            WriteDoubleLE(target, 196U, 36U, delta_n) && WriteDoubleLE(target, 196U, 44U, ndot) &&
            WriteDoubleLE(target, 196U, 52U, m0) && WriteDoubleLE(target, 196U, 60U, ecc) &&
            WriteDoubleLE(target, 196U, 68U, omega) && WriteDoubleLE(target, 196U, 76U, omega0) &&
            WriteDoubleLE(target, 196U, 84U, i0) && WriteDoubleLE(target, 196U, 92U, omega_dot) &&
            WriteDoubleLE(target, 196U, 100U, idot) && WriteDoubleLE(target, 196U, 108U, cis) &&
            WriteDoubleLE(target, 196U, 116U, cic) && WriteDoubleLE(target, 196U, 124U, crs) &&
            WriteDoubleLE(target, 196U, 132U, crc) && WriteDoubleLE(target, 196U, 140U, cus) &&
            WriteDoubleLE(target, 196U, 148U, cuc) && WriteU32LE(target, 196U, 156U, toc_u32) &&
            WriteDoubleLE(target, 196U, 160U, af0) && WriteDoubleLE(target, 196U, 168U, af1) &&
            WriteDoubleLE(target, 196U, 176U, af2) && WriteDoubleLE(target, 196U, 184U, tgdb2bi) &&
            WriteU32LE(target, 196U, 192U, 0U);
    }

    *target_message_id = freq_type == 0U ? kMessageIdBdsBcnav1 : kMessageIdBdsBcnav2;
    *target_size = 220U;
    const double isc = freq_type == 0U ? iscb1cd : iscb2ad;
    return WriteU32LE(target, 220U, 0U, prn) && WriteU32LE(target, 220U, 4U, bdt_week) &&
        WriteU32LE(target, 220U, 8U, status) && WriteU32LE(target, 220U, 12U, iode) &&
        WriteU32LE(target, 220U, 16U, toe_u32) && WriteU32LE(target, 220U, 20U, sat_type) &&
        WriteDoubleLE(target, 220U, 24U, delta_a) && WriteDoubleLE(target, 220U, 32U, adot) &&
        WriteDoubleLE(target, 220U, 40U, delta_n) && WriteDoubleLE(target, 220U, 48U, ndot) &&
        WriteDoubleLE(target, 220U, 56U, m0) && WriteDoubleLE(target, 220U, 64U, ecc) &&
        WriteDoubleLE(target, 220U, 72U, omega) && WriteDoubleLE(target, 220U, 80U, omega0) &&
        WriteDoubleLE(target, 220U, 88U, i0) && WriteDoubleLE(target, 220U, 96U, omega_dot) &&
        WriteDoubleLE(target, 220U, 104U, idot) && WriteDoubleLE(target, 220U, 112U, cis) &&
        WriteDoubleLE(target, 220U, 120U, cic) && WriteDoubleLE(target, 220U, 128U, crs) &&
        WriteDoubleLE(target, 220U, 136U, crc) && WriteDoubleLE(target, 220U, 144U, cus) &&
        WriteDoubleLE(target, 220U, 152U, cuc) && WriteU32LE(target, 220U, 160U, iodc) &&
        WriteU32LE(target, 220U, 164U, toc_u32) && WriteDoubleLE(target, 220U, 168U, af0) &&
        WriteDoubleLE(target, 220U, 176U, af1) && WriteDoubleLE(target, 220U, 184U, af2) &&
        WriteDoubleLE(target, 220U, 192U, tgdb1cp) && WriteDoubleLE(target, 220U, 200U, tgdb2ap) &&
        WriteDoubleLE(target, 220U, 208U, isc) && WriteU32LE(target, 220U, 216U, 0U);
}

void AppendNovAtelAsciiHeader(std::ostringstream& out, const char* name, const novatel::BinaryHeader& header)
{
    const char* const time_status = novatel::TimeStatusToAscii(header.time_status);
    out << '#' << name << ',' << PortAddressToAscii(header.port_address)
        << ',' << header.sequence << ',' << std::fixed << std::setprecision(1)
        << (static_cast<double>(header.idle_time) / 2.0)
        << ',' << (time_status != NULL ? time_status : "UNKNOWN")
        << ',' << header.week << ',' << std::fixed << std::setprecision(3)
        << (static_cast<double>(header.milliseconds) / 1000.0)
        << ',' << std::hex << std::nouppercase << std::setfill('0') << std::setw(8)
        << header.receiver_status << ',' << std::setw(4) << header.reserved
        << std::dec << std::setfill(' ') << ',' << header.receiver_sw_version << ';';
}

}  // namespace

bool IsSupportedEphIonAsciiLine(const char* line)
{
    char name[40] = {};
    if (!ExtractAsciiName(line, name, sizeof(name))) return false;
    return FindNativeByName(name) != NULL || IsUnicoreSourceName(name);
}

bool IsSupportedEphIonBinaryMessageId(std::uint16_t message_id)
{
    return FindNativeById(message_id) != NULL;
}

bool ConvertEphIonAsciiLineToBinary(const char* line,
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

    char name[40] = {};
    if (!ExtractAsciiName(line, name, sizeof(name))) {
        SetError(error_text, error_text_size, "unsupported EPH/ION ASCII record");
        return false;
    }
    const MessageSpec* const native = FindNativeByName(name);
    const bool unicore = IsUnicoreSourceName(name);
    if (native == NULL && !unicore) {
        SetError(error_text, error_text_size, "unsupported EPH/ION ASCII record");
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
        if (len == 0U || (mutable_line[len - 1U] != '\r' && mutable_line[len - 1U] != '\n')) break;
        mutable_line[len - 1U] = '\0';
    }
    char* const star = std::strrchr(mutable_line, '*');
    if (star != NULL) *star = '\0';
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
    if (native != NULL) {
        if (!ParseNovAtelAsciiHeader(header_text, name, &header)) {
            std::free(mutable_line);
            SetError(error_text, error_text_size, "invalid NovAtel EPH/ION header");
            return false;
        }
        std::uint8_t payload[264] = {};
        if (!EncodeFields(body_text, native->fields, native->field_count, payload, native->payload_size)) {
            std::free(mutable_line);
            SetError(error_text, error_text_size, "invalid NovAtel EPH/ION body");
            return false;
        }
        const bool ok = BuildRecord(header, native->message_id, payload, native->payload_size, record, record_size);
        std::free(mutable_line);
        if (!ok) SetError(error_text, error_text_size, "failed to encode NovAtel EPH/ION record");
        return ok;
    }

    if (!ParseUnicoreAsciiHeader(header_text, name, &header)) {
        std::free(mutable_line);
        SetError(error_text, error_text_size, "invalid Unicore EPH/ION header");
        return false;
    }

    std::uint8_t payload[264] = {};
    std::uint16_t message_id = 0U;
    std::uint16_t payload_size = 0U;
    bool body_ok = false;
    if (std::strcmp(name, "GPSEPHA") == 0) {
        message_id = kMessageIdGpsEphem; payload_size = 224U;
        body_ok = EncodeFields(body_text, kGpsEphemFields, FIELD_COUNT(kGpsEphemFields), payload, payload_size);
    } else if (std::strcmp(name, "GLOEPHA") == 0) {
        message_id = kMessageIdGloEphemeris; payload_size = 144U;
        body_ok = EncodeFields(body_text, kGloEphemerisFields, FIELD_COUNT(kGloEphemerisFields), payload, payload_size);
    } else if (std::strcmp(name, "GALEPHA") == 0) {
        message_id = kMessageIdGalEphemeris; payload_size = 220U;
        body_ok = EncodeFields(body_text, kGalEphemerisFields, FIELD_COUNT(kGalEphemerisFields), payload, payload_size);
    } else if (std::strcmp(name, "QZSSEPHA") == 0) {
        message_id = kMessageIdQzssEphemeris; payload_size = 228U;
        body_ok = MapQzssFromUnicore(body_text, payload);
    } else if (std::strcmp(name, "BDSEPHA") == 0) {
        message_id = kMessageIdBd2Ephem; payload_size = 232U;
        body_ok = MapBd2FromUnicore(body_text, payload);
    } else if (std::strcmp(name, "GPSIONA") == 0) {
        message_id = 8U; payload_size = 108U;
        body_ok = MapIonFromUnicore(body_text, payload);
    } else if (std::strcmp(name, "BDSIONA") == 0) {
        message_id = kMessageIdBd2IonUtc; payload_size = 108U;
        body_ok = MapIonFromUnicore(body_text, payload);
    } else if (std::strcmp(name, "GALIONA") == 0) {
        message_id = kMessageIdGalIono; payload_size = 29U;
        body_ok = MapGalIonFromUnicore(body_text, payload);
    } else if (std::strcmp(name, "IRNSSEPHA") == 0) {
        message_id = kMessageIdNavicEphemeris; payload_size = 204U;
        body_ok = MapIrnssFromUnicore(body_text, payload);
    } else if (std::strcmp(name, "BD3EPHA") == 0) {
        body_ok = MapBd3FromUnicore(body_text, &message_id, &payload_size, payload, sizeof(payload));
    }

    std::free(mutable_line);
    if (!body_ok) {
        SetError(error_text, error_text_size, "invalid or unmappable Unicore EPH/ION body");
        return false;
    }
    if (!BuildRecord(header, message_id, payload, payload_size, record, record_size)) {
        SetError(error_text, error_text_size, "failed to encode mapped NovAtel EPH/ION record");
        return false;
    }
    return true;
}

bool ConvertEphIonBinaryRecordToAscii(const std::uint8_t* record,
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
    if (!novatel::DecodeBinaryHeader(record, record_size, &header)) {
        SetError(error_text, error_text_size, "invalid NovAtel binary header");
        return false;
    }
    const MessageSpec* const spec = FindNativeById(header.message_id);
    if (spec == NULL) {
        SetError(error_text, error_text_size, "unsupported EPH/ION binary message ID");
        return false;
    }
    const std::size_t expected_size = novatel::kBinaryHeaderSize +
        static_cast<std::size_t>(header.message_length) + novatel::kBinaryCrcSize;
    if (record_size != expected_size || header.message_length != spec->payload_size ||
        !novatel::ValidateBinaryRecordCrc(record, record_size)) {
        SetError(error_text, error_text_size, "invalid EPH/ION binary length or CRC");
        return false;
    }
    if (novatel::TimeStatusToAscii(header.time_status) == NULL) {
        SetError(error_text, error_text_size, "unknown NovAtel time status");
        return false;
    }

    std::ostringstream out;
    AppendNovAtelAsciiHeader(out, spec->ascii_name, header);
    if (!AppendDecodedFields(out,
                             spec->fields,
                             spec->field_count,
                             record + novatel::kBinaryHeaderSize,
                             spec->payload_size)) {
        SetError(error_text, error_text_size, "failed to decode EPH/ION payload");
        return false;
    }

    const std::string without_crc = out.str();
    const std::uint32_t crc = CalculateNovAtelCrc32(
        reinterpret_cast<const std::uint8_t*>(without_crc.data() + 1U),
        without_crc.size() - 1U);
    out << '*' << std::hex << std::nouppercase << std::setfill('0') << std::setw(8)
        << crc << "\r\n";
    const std::string result = out.str();
    char* output = static_cast<char*>(std::malloc(result.size() + 1U));
    if (output == NULL) {
        SetError(error_text, error_text_size, "out of memory");
        return false;
    }
    std::memcpy(output, result.c_str(), result.size() + 1U);
    *line = output;
    *line_size = result.size();
    return true;
}

}  // namespace gnsslog
