#include "novatel/novatel_protocol.h"

#include <cstring>

#include "byte_io.h"
#include "crc32.h"

namespace gnsslog {
namespace novatel {
namespace {

static const std::uint8_t kSync0 = 0xAAU;
static const std::uint8_t kSync1 = 0x44U;
static const std::uint8_t kSync2 = 0x12U;

struct TimeStatusEntry
{
    const char* text;
    std::uint8_t value;
};

static const TimeStatusEntry kTimeStatusTable[] = {
    {"UNKNOWN", 20U},
    {"APPROXIMATE", 60U},
    {"COARSEADJUSTING", 80U},
    {"COARSE", 100U},
    {"COARSESTEERING", 120U},
    {"FREEWHEELING", 130U},
    {"FINEADJUSTING", 140U},
    {"FINE", 160U},
    {"FINEBACKUPSTEERING", 170U},
    {"FINESTEERING", 180U},
    {"SATTIME", 200U}
};

}  // namespace

bool HasBinarySync(const std::uint8_t* data, std::size_t size)
{
    return data != NULL && size >= 3U && data[0] == kSync0 && data[1] == kSync1 && data[2] == kSync2;
}

bool EncodeBinaryHeader(const BinaryHeader& header, std::uint8_t* data, std::size_t size)
{
    if (data == NULL || size < kBinaryHeaderSize) {
        return false;
    }

    data[0] = kSync0;
    data[1] = kSync1;
    data[2] = kSync2;
    data[3] = static_cast<std::uint8_t>(kBinaryHeaderSize);

    return WriteU16LE(data, size, 4U, header.message_id) &&
           WriteU8(data, size, 6U, header.message_type) &&
           WriteU8(data, size, 7U, header.port_address) &&
           WriteU16LE(data, size, 8U, header.message_length) &&
           WriteU16LE(data, size, 10U, header.sequence) &&
           WriteU8(data, size, 12U, header.idle_time) &&
           WriteU8(data, size, 13U, header.time_status) &&
           WriteU16LE(data, size, 14U, header.week) &&
           WriteU32LE(data, size, 16U, header.milliseconds) &&
           WriteU32LE(data, size, 20U, header.receiver_status) &&
           WriteU16LE(data, size, 24U, header.reserved) &&
           WriteU16LE(data, size, 26U, header.receiver_sw_version);
}

bool DecodeBinaryHeader(const std::uint8_t* data, std::size_t size, BinaryHeader* header)
{
    if (data == NULL || header == NULL || size < kBinaryHeaderSize || !HasBinarySync(data, size)) {
        return false;
    }
    if (data[3] != static_cast<std::uint8_t>(kBinaryHeaderSize)) {
        return false;
    }

    return ReadU16LE(data, size, 4U, &header->message_id) &&
           ReadU8(data, size, 6U, &header->message_type) &&
           ReadU8(data, size, 7U, &header->port_address) &&
           ReadU16LE(data, size, 8U, &header->message_length) &&
           ReadU16LE(data, size, 10U, &header->sequence) &&
           ReadU8(data, size, 12U, &header->idle_time) &&
           ReadU8(data, size, 13U, &header->time_status) &&
           ReadU16LE(data, size, 14U, &header->week) &&
           ReadU32LE(data, size, 16U, &header->milliseconds) &&
           ReadU32LE(data, size, 20U, &header->receiver_status) &&
           ReadU16LE(data, size, 24U, &header->reserved) &&
           ReadU16LE(data, size, 26U, &header->receiver_sw_version);
}

bool TimeStatusFromAscii(const char* text, std::uint8_t* value)
{
    if (text == NULL || value == NULL) {
        return false;
    }

    const std::size_t count = sizeof(kTimeStatusTable) / sizeof(kTimeStatusTable[0]);
    for (std::size_t i = 0U; i < count; ++i) {
        if (std::strcmp(text, kTimeStatusTable[i].text) == 0) {
            *value = kTimeStatusTable[i].value;
            return true;
        }
    }
    return false;
}

const char* TimeStatusToAscii(std::uint8_t value)
{
    const std::size_t count = sizeof(kTimeStatusTable) / sizeof(kTimeStatusTable[0]);
    for (std::size_t i = 0U; i < count; ++i) {
        if (value == kTimeStatusTable[i].value) {
            return kTimeStatusTable[i].text;
        }
    }
    return NULL;
}

bool WriteBinaryRecordCrc(std::uint8_t* record,
                          std::size_t record_size_without_crc,
                          std::size_t capacity)
{
    if (record == NULL || record_size_without_crc > capacity ||
        kBinaryCrcSize > (capacity - record_size_without_crc)) {
        return false;
    }

    const std::uint32_t crc = CalculateNovAtelCrc32(record, record_size_without_crc);
    return WriteU32LE(record, capacity, record_size_without_crc, crc);
}

bool ValidateBinaryRecordCrc(const std::uint8_t* record, std::size_t record_size)
{
    if (record == NULL || record_size < (kBinaryHeaderSize + kBinaryCrcSize)) {
        return false;
    }

    const std::size_t crc_offset = record_size - kBinaryCrcSize;
    std::uint32_t stored_crc = 0U;
    if (!ReadU32LE(record, record_size, crc_offset, &stored_crc)) {
        return false;
    }

    return stored_crc == CalculateNovAtelCrc32(record, crc_offset);
}

}  // namespace novatel
}  // namespace gnsslog
