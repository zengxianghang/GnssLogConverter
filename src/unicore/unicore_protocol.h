#ifndef GNSSLOGCONVERTER_UNICORE_PROTOCOL_H_
#define GNSSLOGCONVERTER_UNICORE_PROTOCOL_H_

#include <cstddef>
#include <cstdint>

namespace gnsslog {
namespace unicore {

static const std::size_t kBinaryHeaderSize = 24U;
static const std::size_t kBinaryCrcSize = 4U;
static const std::uint8_t kSync1 = 0xAAU;
static const std::uint8_t kSync2 = 0x44U;
static const std::uint8_t kSync3 = 0xB5U;
static const std::uint16_t kMessageIdObsVm = 12U;

struct BinaryHeader
{
    std::uint8_t cpu_idle;
    std::uint16_t message_id;
    std::uint16_t message_length;
    std::uint8_t time_ref;
    std::uint8_t time_status;
    std::uint16_t week;
    std::uint32_t milliseconds;
    std::uint32_t version;
    std::uint8_t reserved;
    std::uint8_t leap_seconds;
    std::uint16_t delay_ms;
};

bool HasBinarySync(const std::uint8_t* data, std::size_t size);
bool DecodeBinaryHeader(const std::uint8_t* data, std::size_t size, BinaryHeader* header);
bool EncodeBinaryHeader(const BinaryHeader& header, std::uint8_t* data, std::size_t size);

// Returns header + payload + CRC size. Returns 0 on overflow.
std::size_t BinaryRecordSize(const BinaryHeader& header);

bool ValidateBinaryRecordCrc(const std::uint8_t* record, std::size_t size);
bool WriteBinaryRecordCrc(std::uint8_t* record, std::size_t size_without_crc, std::size_t capacity);

}  // namespace unicore
}  // namespace gnsslog

#endif  // GNSSLOGCONVERTER_UNICORE_PROTOCOL_H_
