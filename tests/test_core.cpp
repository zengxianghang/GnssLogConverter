#include <cmath>
#include <cstdio>
#include <cstring>

#include "byte_io.h"
#include "crc32.h"
#include "novatel/novatel_protocol.h"
#include "novatel/novatel_range.h"
#include "unicore/unicore_obsvm.h"
#include "unicore/unicore_protocol.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

bool NearlyEqual(double a, double b, double tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

void TestByteIo()
{
    std::uint8_t buffer[32] = {};
    Check(gnsslog::WriteU16LE(buffer, sizeof(buffer), 1U, 0x1234U), "WriteU16LE");
    Check(gnsslog::WriteU32LE(buffer, sizeof(buffer), 3U, 0x89ABCDEFU), "WriteU32LE");
    Check(gnsslog::WriteDoubleLE(buffer, sizeof(buffer), 8U, 12345.25), "WriteDoubleLE");

    std::uint16_t u16 = 0U;
    std::uint32_t u32 = 0U;
    double d = 0.0;
    Check(gnsslog::ReadU16LE(buffer, sizeof(buffer), 1U, &u16) && u16 == 0x1234U, "ReadU16LE");
    Check(gnsslog::ReadU32LE(buffer, sizeof(buffer), 3U, &u32) && u32 == 0x89ABCDEFU, "ReadU32LE");
    Check(gnsslog::ReadDoubleLE(buffer, sizeof(buffer), 8U, &d) && NearlyEqual(d, 12345.25, 1e-12), "ReadDoubleLE");
    Check(!gnsslog::ReadU32LE(buffer, 3U, 1U, &u32), "ReadU32LE bounds check");
}

void TestUnicoreCrcFromManual()
{
    static const char text[] =
        "GPSIONA,90,GPS,FINE,2190,371250000,0,0,18,21;"
        "1.490116119384766e-08,-7.450580596923828e-09,-5.960464477539062e-08,"
        "1.192092895507812e-07,1.290240000000000e+05,-1.966080000000000e+05,"
        "6.553600000000000e+04,3.276800000000000e+05,0,0,0,0";
    const std::uint32_t crc = gnsslog::CalculateUnicoreCrc32(
        reinterpret_cast<const std::uint8_t*>(text), std::strlen(text));
    Check(crc == 0xC5974F70U, "Unicore CRC32 manual GPSIONA vector");
    Check(gnsslog::CalculateNovAtelCrc32(reinterpret_cast<const std::uint8_t*>(text), std::strlen(text)) == crc,
          "NovAtel and Unicore reflected CRC implementation match");
}

void TestUnicoreHeaderRoundTrip()
{
    gnsslog::unicore::BinaryHeader src = {};
    src.cpu_idle = 90U;
    src.message_id = gnsslog::unicore::kMessageIdObsVm;
    src.message_length = 44U;
    src.time_ref = 1U;
    src.time_status = 160U;
    src.week = 2190U;
    src.milliseconds = 117395000U;
    src.version = 0U;
    src.reserved = 0U;
    src.leap_seconds = 18U;
    src.delay_ms = 17U;

    std::uint8_t buffer[gnsslog::unicore::kBinaryHeaderSize] = {};
    Check(gnsslog::unicore::EncodeBinaryHeader(src, buffer, sizeof(buffer)), "EncodeBinaryHeader");
    Check(gnsslog::unicore::HasBinarySync(buffer, sizeof(buffer)), "HasBinarySync");

    gnsslog::unicore::BinaryHeader dst = {};
    Check(gnsslog::unicore::DecodeBinaryHeader(buffer, sizeof(buffer), &dst), "DecodeBinaryHeader");
    Check(dst.cpu_idle == src.cpu_idle, "header cpu_idle");
    Check(dst.message_id == src.message_id, "header message_id");
    Check(dst.message_length == src.message_length, "header message_length");
    Check(dst.time_ref == src.time_ref, "header time_ref");
    Check(dst.time_status == src.time_status, "header time_status");
    Check(dst.week == src.week, "header week");
    Check(dst.milliseconds == src.milliseconds, "header milliseconds");
}

void TestObsVmRoundTrip()
{
    gnsslog::unicore::ObsVmMeasurement src[2] = {};
    src[0].system_freq = 0U;
    src[0].prn_slot = 26U;
    src[0].pseudorange_m = 21720097.812;
    src[0].adr_cycles = -114139892.254585;
    src[0].pseudorange_std_x100 = 52U;
    src[0].adr_std_x10000 = 181U;
    src[0].doppler_hz = -2263.222F;
    src[0].cn0_x100 = 4270U;
    src[0].reserved = 0U;
    src[0].lock_time_s = 6262.010F;
    src[0].tracking_status = 0x00181C23U;

    src[1].system_freq = 7U;
    src[1].prn_slot = 52U;
    src[1].pseudorange_m = 23348014.480;
    src[1].adr_cycles = -124764670.630508;
    src[1].pseudorange_std_x100 = 74U;
    src[1].adr_std_x10000 = 237U;
    src[1].doppler_hz = -2702.620F;
    src[1].cn0_x100 = 4022U;
    src[1].reserved = 0U;
    src[1].lock_time_s = 254.010F;
    src[1].tracking_status = 0x00191C23U;

    std::uint8_t payload[gnsslog::unicore::kObsVmCountSize + 2U * gnsslog::unicore::kObsVmMeasurementSize] = {};
    std::size_t payload_size = 0U;
    Check(gnsslog::unicore::EncodeObsVmPayload(src, 2U, payload, sizeof(payload), &payload_size), "EncodeObsVmPayload");
    Check(payload_size == sizeof(payload), "OBSVM payload size");

    gnsslog::unicore::ObsVmMeasurement dst[2] = {};
    std::uint32_t count = 0U;
    Check(gnsslog::unicore::DecodeObsVmPayload(payload, payload_size, dst, 2U, &count), "DecodeObsVmPayload");
    Check(count == 2U, "OBSVM count");
    Check(dst[0].prn_slot == src[0].prn_slot, "OBSVM PRN");
    Check(NearlyEqual(dst[0].pseudorange_m, src[0].pseudorange_m, 1e-9), "OBSVM pseudorange");
    Check(dst[0].pseudorange_std_x100 == 52U, "OBSVM psr std raw scale");
    Check(dst[0].adr_std_x10000 == 181U, "OBSVM adr std raw scale");
    Check(dst[0].cn0_x100 == 4270U, "OBSVM C/N0 raw scale");
    Check(!gnsslog::unicore::DecodeObsVmPayload(payload, payload_size - 1U, dst, 2U, &count), "OBSVM truncated payload rejected");
}

void TestNovAtelTimeStatus()
{
    std::uint8_t value = 0U;
    Check(gnsslog::novatel::TimeStatusFromAscii("UNKNOWN", &value) && value == 20U, "UNKNOWN time status");
    Check(gnsslog::novatel::TimeStatusFromAscii("FINE", &value) && value == 160U, "FINE time status");
    Check(gnsslog::novatel::TimeStatusFromAscii("FINESTEERING", &value) && value == 180U, "FINESTEERING time status");
    Check(gnsslog::novatel::TimeStatusFromAscii("SATTIME", &value) && value == 200U, "SATTIME time status");
    Check(!gnsslog::novatel::TimeStatusFromAscii("INVALID", &value), "invalid time status rejected");
    Check(std::strcmp(gnsslog::novatel::TimeStatusToAscii(160U), "FINE") == 0, "FINE reverse mapping");
}

void TestNovAtelHeaderRoundTrip()
{
    gnsslog::novatel::BinaryHeader src = {};
    src.message_id = gnsslog::novatel::kMessageIdRange;
    src.message_type = 0U;
    src.port_address = 0U;
    src.message_length = 48U;
    src.sequence = 0U;
    src.idle_time = 180U;
    src.time_status = 160U;
    src.week = 2190U;
    src.milliseconds = 117395000U;
    src.receiver_status = 0U;
    src.reserved = 0U;
    src.receiver_sw_version = 0U;

    std::uint8_t buffer[gnsslog::novatel::kBinaryHeaderSize] = {};
    Check(gnsslog::novatel::EncodeBinaryHeader(src, buffer, sizeof(buffer)), "NovAtel header encode");
    Check(gnsslog::novatel::HasBinarySync(buffer, sizeof(buffer)), "NovAtel AA4412 sync");
    Check(buffer[3] == 28U, "NovAtel header length");

    gnsslog::novatel::BinaryHeader dst = {};
    Check(gnsslog::novatel::DecodeBinaryHeader(buffer, sizeof(buffer), &dst), "NovAtel header decode");
    Check(dst.message_id == 43U, "NovAtel RANGE message id");
    Check(dst.message_length == src.message_length, "NovAtel message length");
    Check(dst.time_status == 160U, "NovAtel time status round trip");
    Check(dst.week == src.week, "NovAtel week");
    Check(dst.milliseconds == src.milliseconds, "NovAtel milliseconds");
}

void TestRangePayloadRoundTrip()
{
    gnsslog::novatel::RangeMeasurement src = {};
    src.prn_slot = 26U;
    src.glofreq = 0U;
    src.pseudorange_m = 21720097.812;
    src.pseudorange_std_m = 0.52F;
    src.adr_cycles = -114139892.254585;
    src.adr_std_cycles = 0.0181F;
    src.doppler_hz = -2263.222F;
    src.cn0_db_hz = 42.70F;
    src.lock_time_s = 6262.010F;
    src.tracking_status = 0x00181C23U;

    std::uint8_t payload[gnsslog::novatel::kRangeCountSize + gnsslog::novatel::kRangeMeasurementSize] = {};
    std::size_t payload_size = 0U;
    Check(gnsslog::novatel::EncodeRangePayload(&src, 1U, payload, sizeof(payload), &payload_size), "RANGE payload encode");
    Check(payload_size == 48U, "RANGE one-observation payload is 48 bytes");

    gnsslog::novatel::RangeMeasurement dst = {};
    std::uint32_t count = 0U;
    Check(gnsslog::novatel::DecodeRangePayload(payload, payload_size, &dst, 1U, &count), "RANGE payload decode");
    Check(count == 1U, "RANGE count");
    Check(dst.prn_slot == src.prn_slot, "RANGE PRN");
    Check(NearlyEqual(dst.pseudorange_std_m, 0.52, 1e-6), "RANGE psr sigma");
    Check(NearlyEqual(dst.adr_std_cycles, 0.0181, 1e-6), "RANGE adr sigma");
    Check(NearlyEqual(dst.cn0_db_hz, 42.70, 1e-5), "RANGE C/N0");
    Check(dst.tracking_status == src.tracking_status, "RANGE tracking status storage");
    Check(!gnsslog::novatel::DecodeRangePayload(payload, payload_size - 1U, &dst, 1U, &count), "RANGE truncated payload rejected");
}

void TestNovAtelBinaryRecordCrc()
{
    const std::size_t payload_size = 4U;
    const std::size_t record_size = gnsslog::novatel::kBinaryHeaderSize + payload_size + gnsslog::novatel::kBinaryCrcSize;
    std::uint8_t record[40] = {};

    gnsslog::novatel::BinaryHeader header = {};
    header.message_id = gnsslog::novatel::kMessageIdRange;
    header.message_length = static_cast<std::uint16_t>(payload_size);
    header.time_status = 160U;
    header.week = 2190U;
    header.milliseconds = 1U;

    Check(gnsslog::novatel::EncodeBinaryHeader(header, record, sizeof(record)), "NovAtel record header encode");
    Check(gnsslog::WriteU32LE(record, sizeof(record), gnsslog::novatel::kBinaryHeaderSize, 0U), "NovAtel empty RANGE count write");
    Check(gnsslog::novatel::WriteBinaryRecordCrc(record, record_size - gnsslog::novatel::kBinaryCrcSize, sizeof(record)), "NovAtel record CRC write");
    Check(gnsslog::novatel::ValidateBinaryRecordCrc(record, record_size), "NovAtel record CRC validate");
    record[gnsslog::novatel::kBinaryHeaderSize] ^= 0x01U;
    Check(!gnsslog::novatel::ValidateBinaryRecordCrc(record, record_size), "NovAtel record CRC detects corruption");
}

}  // namespace

int main()
{
    TestByteIo();
    TestUnicoreCrcFromManual();
    TestUnicoreHeaderRoundTrip();
    TestObsVmRoundTrip();
    TestNovAtelTimeStatus();
    TestNovAtelHeaderRoundTrip();
    TestRangePayloadRoundTrip();
    TestNovAtelBinaryRecordCrc();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
