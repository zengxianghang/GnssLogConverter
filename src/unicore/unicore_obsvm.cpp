#include "unicore/unicore_obsvm.h"

#include <limits>

#include "byte_io.h"

namespace gnsslog {
namespace unicore {

std::size_t ObsVmPayloadSize(std::uint32_t measurement_count)
{
    const std::size_t count = static_cast<std::size_t>(measurement_count);
    if (count > ((std::numeric_limits<std::size_t>::max() - kObsVmCountSize) / kObsVmMeasurementSize)) {
        return 0U;
    }
    return kObsVmCountSize + count * kObsVmMeasurementSize;
}

bool DecodeObsVmPayload(const std::uint8_t* payload,
                        std::size_t payload_size,
                        ObsVmMeasurement* measurements,
                        std::size_t measurement_capacity,
                        std::uint32_t* measurement_count)
{
    if (payload == NULL || measurement_count == NULL || payload_size < kObsVmCountSize) {
        return false;
    }

    std::uint32_t count = 0U;
    if (!ReadU32LE(payload, payload_size, 0U, &count)) {
        return false;
    }

    const std::size_t expected_size = ObsVmPayloadSize(count);
    if (expected_size == 0U || payload_size != expected_size) {
        return false;
    }
    if (static_cast<std::size_t>(count) > measurement_capacity) {
        return false;
    }
    if (count != 0U && measurements == NULL) {
        return false;
    }

    for (std::uint32_t i = 0U; i < count; ++i) {
        ObsVmMeasurement* const obs = &measurements[i];
        const std::size_t base = kObsVmCountSize + static_cast<std::size_t>(i) * kObsVmMeasurementSize;
        if (!ReadU16LE(payload, payload_size, base + 0U, &obs->system_freq) ||
            !ReadU16LE(payload, payload_size, base + 2U, &obs->prn_slot) ||
            !ReadDoubleLE(payload, payload_size, base + 4U, &obs->pseudorange_m) ||
            !ReadDoubleLE(payload, payload_size, base + 12U, &obs->adr_cycles) ||
            !ReadU16LE(payload, payload_size, base + 20U, &obs->pseudorange_std_x100) ||
            !ReadU16LE(payload, payload_size, base + 22U, &obs->adr_std_x10000) ||
            !ReadFloatLE(payload, payload_size, base + 24U, &obs->doppler_hz) ||
            !ReadU16LE(payload, payload_size, base + 28U, &obs->cn0_x100) ||
            !ReadU16LE(payload, payload_size, base + 30U, &obs->reserved) ||
            !ReadFloatLE(payload, payload_size, base + 32U, &obs->lock_time_s) ||
            !ReadU32LE(payload, payload_size, base + 36U, &obs->tracking_status)) {
            return false;
        }
    }

    *measurement_count = count;
    return true;
}

bool EncodeObsVmPayload(const ObsVmMeasurement* measurements,
                        std::uint32_t measurement_count,
                        std::uint8_t* payload,
                        std::size_t payload_capacity,
                        std::size_t* payload_size)
{
    if (payload == NULL || payload_size == NULL || (measurement_count != 0U && measurements == NULL)) {
        return false;
    }

    const std::size_t required = ObsVmPayloadSize(measurement_count);
    if (required == 0U || payload_capacity < required) {
        return false;
    }

    if (!WriteU32LE(payload, payload_capacity, 0U, measurement_count)) {
        return false;
    }

    for (std::uint32_t i = 0U; i < measurement_count; ++i) {
        const ObsVmMeasurement* const obs = &measurements[i];
        const std::size_t base = kObsVmCountSize + static_cast<std::size_t>(i) * kObsVmMeasurementSize;
        if (!WriteU16LE(payload, payload_capacity, base + 0U, obs->system_freq) ||
            !WriteU16LE(payload, payload_capacity, base + 2U, obs->prn_slot) ||
            !WriteDoubleLE(payload, payload_capacity, base + 4U, obs->pseudorange_m) ||
            !WriteDoubleLE(payload, payload_capacity, base + 12U, obs->adr_cycles) ||
            !WriteU16LE(payload, payload_capacity, base + 20U, obs->pseudorange_std_x100) ||
            !WriteU16LE(payload, payload_capacity, base + 22U, obs->adr_std_x10000) ||
            !WriteFloatLE(payload, payload_capacity, base + 24U, obs->doppler_hz) ||
            !WriteU16LE(payload, payload_capacity, base + 28U, obs->cn0_x100) ||
            !WriteU16LE(payload, payload_capacity, base + 30U, obs->reserved) ||
            !WriteFloatLE(payload, payload_capacity, base + 32U, obs->lock_time_s) ||
            !WriteU32LE(payload, payload_capacity, base + 36U, obs->tracking_status)) {
            return false;
        }
    }

    *payload_size = required;
    return true;
}

}  // namespace unicore
}  // namespace gnsslog
