#ifndef GNSSLOGCONVERTER_BYTE_IO_H_
#define GNSSLOGCONVERTER_BYTE_IO_H_

#include <cstddef>
#include <cstdint>

namespace gnsslog {

bool ReadU8(const std::uint8_t* data, std::size_t size, std::size_t offset, std::uint8_t* value);
bool ReadU16LE(const std::uint8_t* data, std::size_t size, std::size_t offset, std::uint16_t* value);
bool ReadU32LE(const std::uint8_t* data, std::size_t size, std::size_t offset, std::uint32_t* value);
bool ReadU64LE(const std::uint8_t* data, std::size_t size, std::size_t offset, std::uint64_t* value);
bool ReadFloatLE(const std::uint8_t* data, std::size_t size, std::size_t offset, float* value);
bool ReadDoubleLE(const std::uint8_t* data, std::size_t size, std::size_t offset, double* value);

bool WriteU8(std::uint8_t* data, std::size_t size, std::size_t offset, std::uint8_t value);
bool WriteU16LE(std::uint8_t* data, std::size_t size, std::size_t offset, std::uint16_t value);
bool WriteU32LE(std::uint8_t* data, std::size_t size, std::size_t offset, std::uint32_t value);
bool WriteU64LE(std::uint8_t* data, std::size_t size, std::size_t offset, std::uint64_t value);
bool WriteFloatLE(std::uint8_t* data, std::size_t size, std::size_t offset, float value);
bool WriteDoubleLE(std::uint8_t* data, std::size_t size, std::size_t offset, double value);

}  // namespace gnsslog

#endif  // GNSSLOGCONVERTER_BYTE_IO_H_
