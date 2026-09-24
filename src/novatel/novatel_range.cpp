#include "novatel/novatel_range.h"

#include <limits>

#include "byte_io.h"

namespace gnsslog {
namespace novatel {

std::size_t RangePayloadSize(std::uint32_t measurement_count)
{
    const std::size_t count = static_cast<std::size_t>(measurement_count);
    if (count > ((std::numeric_limits<std::size_t>::max() - kRangeCountSize) / kRangeMeasurementSize)) {
        return 0U;
    }
    return kRangeCountSize + count * kRangeMeasurementSize;
}

bool DecodeRangePayload(const std::uint8_t* payload,
                        std::size_t payload_size,
                        RangeMeasurement* measurements,
                        std::size_t measurement_capacity,
                        std::uint32_t* measurement_count)
{
    if (payload == NULL || measurement_count == NULL || payload_size < kRangeCountSize) {
        return false;
    }

    std::uint32_t count = 0U;
    if (!ReadU32LE(payload, payload_size, 0U, &count)) {
        return false;
    }

    const std::size_t expected_size = RangePayloadSize(count);
    if (expected_size == 0U || payload_size != expected_size ||
        static_cast<std::size_t>(count) > measurement_capacity ||
        (count != 0U && measurements == NULL)) {
        return false;
    }

    for (std::uint32_t i = 0U; i < count; ++i) {
        RangeMeasurement* const obs = &measurements[i];
        const std::size_t base = kRangeCountSize + static_cast<std::size_t>(i) * kRangeMeasurementSize;
        if (!ReadU16LE(payload, payload_size, base + 0U, &obs->prn_slot) ||
            !ReadU16LE(payload, payload_size, base + 2U, &obs->glofreq) ||
            !ReadDoubleLE(payload, payload_size, base + 4U, &obs->pseudorange_m) ||
            !ReadFloatLE(payload, payload_size, base + 12U, &obs->pseudorange_std_m) ||
            !ReadDoubleLE(payload, payload_size, base + 16U, &obs->adr_cycles) ||
            !ReadFloatLE(payload, payload_size, base + 24U, &obs->adr_std_cycles) ||
            !ReadFloatLE(payload, payload_size, base + 28U, &obs->doppler_hz) ||
            !ReadFloatLE(payload, payload_size, base + 32U, &obs->cn0_db_hz) ||
            !ReadFloatLE(payload, payload_size, base + 36U, &obs->lock_time_s) ||
            !ReadU32LE(payload, payload_size, base + 40U, &obs->tracking_status)) {
            return false;
        }
    }

    *measurement_count = count;
    return true;
}

bool EncodeRangePayload(const RangeMeasurement* measurements,
                        std::uint32_t measurement_count,
                        std::uint8_t* payload,
                        std::size_t payload_capacity,
                        std::size_t* payload_size)
{
    if (payload == NULL || payload_size == NULL || (measurement_count != 0U && measurements == NULL)) {
        return false;
    }

    const std::size_t required = RangePayloadSize(measurement_count);
    if (required == 0U || payload_capacity < required) {
        return false;
    }

    if (!WriteU32LE(payload, payload_capacity, 0U, measurement_count)) {
        return false;
    }

    for (std::uint32_t i = 0U; i < measurement_count; ++i) {
        const RangeMeasurement* const obs = &measurements[i];
        const std::size_t base = kRangeCountSize + static_cast<std::size_t>(i) * kRangeMeasurementSize;
        if (!WriteU16LE(payload, payload_capacity, base + 0U, obs->prn_slot) ||
            !WriteU16LE(payload, payload_capacity, base + 2U, obs->glofreq) ||
            !WriteDoubleLE(payload, payload_capacity, base + 4U, obs->pseudorange_m) ||
            !WriteFloatLE(payload, payload_capacity, base + 12U, obs->pseudorange_std_m) ||
            !WriteDoubleLE(payload, payload_capacity, base + 16U, obs->adr_cycles) ||
            !WriteFloatLE(payload, payload_capacity, base + 24U, obs->adr_std_cycles) ||
            !WriteFloatLE(payload, payload_capacity, base + 28U, obs->doppler_hz) ||
            !WriteFloatLE(payload, payload_capacity, base + 32U, obs->cn0_db_hz) ||
            !WriteFloatLE(payload, payload_capacity, base + 36U, obs->lock_time_s) ||
            !WriteU32LE(payload, payload_capacity, base + 40U, obs->tracking_status)) {
            return false;
        }
    }

    *payload_size = required;
    return true;
}

}  // namespace novatel
}  // namespace gnsslog
