#ifndef GNSSLOGCONVERTER_CRC32_H_
#define GNSSLOGCONVERTER_CRC32_H_

#include <cstddef>
#include <cstdint>

namespace gnsslog {

// Unicore N4 Appendix 1 CRC-32: reflected polynomial 0xEDB88320,
// initial value 0, no final XOR.
std::uint32_t CalculateUnicoreCrc32(const std::uint8_t* data, std::size_t size);

}  // namespace gnsslog

#endif  // GNSSLOGCONVERTER_CRC32_H_
