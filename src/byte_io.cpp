#include "byte_io.h"

#include <cstring>

namespace gnsslog {
namespace {

bool HasRange(std::size_t size, std::size_t offset, std::size_t width)
{
    return offset <= size && width <= (size - offset);
}

}  // namespace

bool ReadU8(const std::uint8_t* data, std::size_t size, std::size_t offset, std::uint8_t* value)
{
    if (data == NULL || value == NULL || !HasRange(size, offset, 1U)) {
        return false;
    }
    *value = data[offset];
    return true;
}

bool ReadU16LE(const std::uint8_t* data, std::size_t size, std::size_t offset, std::uint16_t* value)
{
    if (data == NULL || value == NULL || !HasRange(size, offset, 2U)) {
        return false;
    }
    *value = static_cast<std::uint16_t>(data[offset]) |
             static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset + 1U]) << 8U);
    return true;
}

bool ReadU32LE(const std::uint8_t* data, std::size_t size, std::size_t offset, std::uint32_t* value)
{
    if (data == NULL || value == NULL || !HasRange(size, offset, 4U)) {
        return false;
    }
    *value = static_cast<std::uint32_t>(data[offset]) |
             (static_cast<std::uint32_t>(data[offset + 1U]) << 8U) |
             (static_cast<std::uint32_t>(data[offset + 2U]) << 16U) |
             (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
    return true;
}

bool ReadU64LE(const std::uint8_t* data, std::size_t size, std::size_t offset, std::uint64_t* value)
{
    std::uint32_t lo = 0U;
    std::uint32_t hi = 0U;
    if (value == NULL || !ReadU32LE(data, size, offset, &lo) || !ReadU32LE(data, size, offset + 4U, &hi)) {
        return false;
    }
    *value = static_cast<std::uint64_t>(lo) | (static_cast<std::uint64_t>(hi) << 32U);
    return true;
}

bool ReadFloatLE(const std::uint8_t* data, std::size_t size, std::size_t offset, float* value)
{
    std::uint32_t raw = 0U;
    if (value == NULL || !ReadU32LE(data, size, offset, &raw)) {
        return false;
    }
    static_assert(sizeof(raw) == sizeof(*value), "float must be 32-bit");
    std::memcpy(value, &raw, sizeof(raw));
    return true;
}

bool ReadDoubleLE(const std::uint8_t* data, std::size_t size, std::size_t offset, double* value)
{
    std::uint64_t raw = 0U;
    if (value == NULL || !ReadU64LE(data, size, offset, &raw)) {
        return false;
    }
    static_assert(sizeof(raw) == sizeof(*value), "double must be 64-bit");
    std::memcpy(value, &raw, sizeof(raw));
    return true;
}

bool WriteU8(std::uint8_t* data, std::size_t size, std::size_t offset, std::uint8_t value)
{
    if (data == NULL || !HasRange(size, offset, 1U)) {
        return false;
    }
    data[offset] = value;
    return true;
}

bool WriteU16LE(std::uint8_t* data, std::size_t size, std::size_t offset, std::uint16_t value)
{
    if (data == NULL || !HasRange(size, offset, 2U)) {
        return false;
    }
    data[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    data[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    return true;
}

bool WriteU32LE(std::uint8_t* data, std::size_t size, std::size_t offset, std::uint32_t value)
{
    if (data == NULL || !HasRange(size, offset, 4U)) {
        return false;
    }
    data[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    data[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    data[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    data[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    return true;
}

bool WriteU64LE(std::uint8_t* data, std::size_t size, std::size_t offset, std::uint64_t value)
{
    return WriteU32LE(data, size, offset, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL)) &&
           WriteU32LE(data, size, offset + 4U, static_cast<std::uint32_t>((value >> 32U) & 0xFFFFFFFFULL));
}

bool WriteFloatLE(std::uint8_t* data, std::size_t size, std::size_t offset, float value)
{
    std::uint32_t raw = 0U;
    static_assert(sizeof(raw) == sizeof(value), "float must be 32-bit");
    std::memcpy(&raw, &value, sizeof(raw));
    return WriteU32LE(data, size, offset, raw);
}

bool WriteDoubleLE(std::uint8_t* data, std::size_t size, std::size_t offset, double value)
{
    std::uint64_t raw = 0U;
    static_assert(sizeof(raw) == sizeof(value), "double must be 64-bit");
    std::memcpy(&raw, &value, sizeof(raw));
    return WriteU64LE(data, size, offset, raw);
}

}  // namespace gnsslog
