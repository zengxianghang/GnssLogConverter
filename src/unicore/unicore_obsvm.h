#ifndef GNSSLOGCONVERTER_UNICORE_OBSVM_H_
#define GNSSLOGCONVERTER_UNICORE_OBSVM_H_

#include <cstddef>
#include <cstdint>

namespace gnsslog {
namespace unicore {

static const std::size_t kObsVmCountSize = 4U;
static const std::size_t kObsVmMeasurementSize = 40U;

struct ObsVmMeasurement
{
    std::uint16_t system_freq;
    std::uint16_t prn_slot;
    double pseudorange_m;
    double adr_cycles;
    std::uint16_t pseudorange_std_x100;
    std::uint16_t adr_std_x10000;
    float doppler_hz;
    std::uint16_t cn0_x100;
    std::uint16_t reserved;
    float lock_time_s;
    std::uint32_t tracking_status;
};

bool DecodeObsVmPayload(const std::uint8_t* payload,
                        std::size_t payload_size,
                        ObsVmMeasurement* measurements,
                        std::size_t measurement_capacity,
                        std::uint32_t* measurement_count);

bool EncodeObsVmPayload(const ObsVmMeasurement* measurements,
                        std::uint32_t measurement_count,
                        std::uint8_t* payload,
                        std::size_t payload_capacity,
                        std::size_t* payload_size);

std::size_t ObsVmPayloadSize(std::uint32_t measurement_count);

}  // namespace unicore
}  // namespace gnsslog

#endif  // GNSSLOGCONVERTER_UNICORE_OBSVM_H_
