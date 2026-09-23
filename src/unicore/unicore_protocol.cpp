#include "unicore/unicore_protocol.h"

#include <limits>

#include "byte_io.h"
#include "crc32.h"

namespace gnsslog {
namespace unicore {

bool HasBinarySync(const std::uint8_t* data, std::size_t size)
{
    return data != NULL && size >= 3U && data[0] == kSync1 && data[1] == kSync2 && data[2] == kSync3;
}

bool DecodeBinaryHeader(const std::uint8_t* data, std::size_t size, BinaryHeader* header)
{
    if (header == NULL || !HasBinarySync(data, size) || size < kBinaryHeaderSize) {
        return false;
    }

    return ReadU8(data, size, 3U, &header->cpu_idle) &&
           ReadU16LE(data, size, 4U, &header->message_id) &&
           ReadU16LE(data, size, 6U, &header->message_length) &&
           ReadU8(data, size, 8U, &header->time_ref) &&
           ReadU8(data, size, 9U, &header->time_status) &&
           ReadU16LE(data, size, 10U, &header->week) &&
           ReadU32LE(data, size, 12U, &header->milliseconds) &&
           ReadU32LE(data, size, 16U, &header->version) &&
           ReadU8(data, size, 20U, &header->reserved) &&
           ReadU8(data, size, 21U, &header->leap_seconds) &&
           ReadU16LE(data, size, 22U, &header->delay_ms);
}

bool EncodeBinaryHeader(const BinaryHeader& header, std::uint8_t* data, std::size_t size)
{
    if (data == NULL || size < kBinaryHeaderSize) {
        return false;
    }

    return WriteU8(data, size, 0U, kSync1) &&
           WriteU8(data, size, 1U, kSync2) &&
           WriteU8(data, size, 2U, kSync3) &&
           WriteU8(data, size, 3U, header.cpu_idle) &&
           WriteU16LE(data, size, 4U, header.message_id) &&
           WriteU16LE(data, size, 6U, header.message_length) &&
           WriteU8(data, size, 8U, header.time_ref) &&
           WriteU8(data, size, 9U, header.time_status) &&
           WriteU16LE(data, size, 10U, header.week) &&
           WriteU32LE(data, size, 12U, header.milliseconds) &&
           WriteU32LE(data, size, 16U, header.version) &&
           WriteU8(data, size, 20U, header.reserved) &&
           WriteU8(data, size, 21U, header.leap_seconds) &&
           WriteU16LE(data, size, 22U, header.delay_ms);
}

std::size_t BinaryRecordSize(const BinaryHeader& header)
{
    const std::size_t payload = static_cast<std::size_t>(header.message_length);
    const std::size_t overhead = kBinaryHeaderSize + kBinaryCrcSize;
    if (payload > (std::numeric_limits<std::size_t>::max() - overhead)) {
        return 0U;
    }
    return overhead + payload;
}

bool ValidateBinaryRecordCrc(const std::uint8_t* record, std::size_t size)
{
    BinaryHeader header = {};
    if (!DecodeBinaryHeader(record, size, &header)) {
        return false;
    }

    const std::size_t expected_size = BinaryRecordSize(header);
    if (expected_size == 0U || size < expected_size) {
        return false;
    }

    std::uint32_t stored_crc = 0U;
    if (!ReadU32LE(record, expected_size, expected_size - kBinaryCrcSize, &stored_crc)) {
        return false;
    }

    const std::uint32_t calculated = CalculateUnicoreCrc32(record, expected_size - kBinaryCrcSize);
    return stored_crc == calculated;
}

bool WriteBinaryRecordCrc(std::uint8_t* record, std::size_t size_without_crc, std::size_t capacity)
{
    if (record == NULL || size_without_crc > capacity || (capacity - size_without_crc) < kBinaryCrcSize) {
        return false;
    }
    const std::uint32_t crc = CalculateUnicoreCrc32(record, size_without_crc);
    return WriteU32LE(record, capacity, size_without_crc, crc);
}

}  // namespace unicore
}  // namespace gnsslog
