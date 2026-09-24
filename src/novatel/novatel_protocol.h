#ifndef GNSSLOGCONVERTER_NOVATEL_PROTOCOL_H_
#define GNSSLOGCONVERTER_NOVATEL_PROTOCOL_H_

#include <cstddef>
#include <cstdint>

namespace gnsslog {
namespace novatel {

static const std::size_t kBinaryHeaderSize = 28U;
static const std::size_t kBinaryCrcSize = 4U;
static const std::uint16_t kMessageIdRange = 43U;

struct BinaryHeader
{
    std::uint16_t message_id;
    std::uint8_t message_type;
    std::uint8_t port_address;
    std::uint16_t message_length;
    std::uint16_t sequence;
    std::uint8_t idle_time;
    std::uint8_t time_status;
    std::uint16_t week;
    std::uint32_t milliseconds;
    std::uint32_t receiver_status;
    std::uint16_t reserved;
    std::uint16_t receiver_sw_version;
};

bool HasBinarySync(const std::uint8_t* data, std::size_t size);
bool EncodeBinaryHeader(const BinaryHeader& header, std::uint8_t* data, std::size_t size);
bool DecodeBinaryHeader(const std::uint8_t* data, std::size_t size, BinaryHeader* header);

bool TimeStatusFromAscii(const char* text, std::uint8_t* value);
const char* TimeStatusToAscii(std::uint8_t value);

bool WriteBinaryRecordCrc(std::uint8_t* record,
                          std::size_t record_size_without_crc,
                          std::size_t capacity);
bool ValidateBinaryRecordCrc(const std::uint8_t* record, std::size_t record_size);

}  // namespace novatel
}  // namespace gnsslog

#endif  // GNSSLOGCONVERTER_NOVATEL_PROTOCOL_H_
