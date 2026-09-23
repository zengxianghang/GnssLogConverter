#include "crc32.h"

namespace gnsslog {

std::uint32_t CalculateUnicoreCrc32(const std::uint8_t* data, std::size_t size)
{
    std::uint32_t crc = 0U;
    if (data == NULL && size != 0U) {
        return 0U;
    }

    for (std::size_t i = 0U; i < size; ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]);
        for (unsigned int bit = 0U; bit < 8U; ++bit) {
            const std::uint32_t mask = static_cast<std::uint32_t>(0U - (crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return crc;
}

}  // namespace gnsslog
