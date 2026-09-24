#ifndef GNSSLOGCONVERTER_NOVATEL_RANGE_H_
#define GNSSLOGCONVERTER_NOVATEL_RANGE_H_

#include <cstddef>
#include <cstdint>

namespace gnsslog {
namespace novatel {

static const std::size_t kRangeCountSize = 4U;
static const std::size_t kRangeMeasurementSize = 44U;

struct RangeMeasurement
{
    std::uint16_t prn_slot;
    std::uint16_t glofreq;
    double pseudorange_m;
    float pseudorange_std_m;
    double adr_cycles;
    float adr_std_cycles;
    float doppler_hz;
    float cn0_db_hz;
    float lock_time_s;
    std::uint32_t tracking_status;
};

std::size_t RangePayloadSize(std::uint32_t measurement_count);

bool DecodeRangePayload(const std::uint8_t* payload,
                        std::size_t payload_size,
                        RangeMeasurement* measurements,
                        std::size_t measurement_capacity,
                        std::uint32_t* measurement_count);

bool EncodeRangePayload(const RangeMeasurement* measurements,
                        std::uint32_t measurement_count,
                        std::uint8_t* payload,
                        std::size_t payload_capacity,
                        std::size_t* payload_size);

}  // namespace novatel
}  // namespace gnsslog

#endif  // GNSSLOGCONVERTER_NOVATEL_RANGE_H_
