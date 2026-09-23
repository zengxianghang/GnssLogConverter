#ifndef GNSSLOGCONVERTER_CRC32_H_
#define GNSSLOGCONVERTER_CRC32_H_

#include <cstddef>
#include <cstdint>

namespace gnsslog {

// Reflected CRC-32 used by NovAtel OEM7 and Unicore N4:
// polynomial 0xEDB88320, initial value 0, no final XOR.
std::uint32_t CalculateCrc32(const std::uint8_t* data, std::size_t size);

std::uint32_t CalculateUnicoreCrc32(const std::uint8_t* data, std::size_t size);
std::uint32_t CalculateNovAtelCrc32(const std::uint8_t* data, std::size_t size);

}  // namespace gnsslog

#endif  // GNSSLOGCONVERTER_CRC32_H_
