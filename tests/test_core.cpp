#include <cmath>
#include <cstdio>
#include <cstring>

#include "byte_io.h"
#include "crc32.h"
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
    // Unicore N4 R1.15 GPSIONA example. ASCII CRC covers bytes after '#'
    // and before '*'. Expected CRC shown by the manual: c5974f70.
    static const char text[] =
        "GPSIONA,90,GPS,FINE,2190,371250000,0,0,18,21;"
        "1.490116119384766e-08,-7.450580596923828e-09,-5.960464477539062e-08,"
        "1.192092895507812e-07,1.290240000000000e+05,-1.966080000000000e+05,"
        "6.553600000000000e+04,3.276800000000000e+05,0,0,0,0";
    const std::uint32_t crc = gnsslog::CalculateUnicoreCrc32(
        reinterpret_cast<const std::uint8_t*>(text), std::strlen(text));
    Check(crc == 0xC5974F70U, "Unicore CRC32 manual GPSIONA vector");
}

void TestUnicoreHeaderRoundTrip()
{
    gnsslog::unicore::BinaryHeader src = {};
    src.cpu_idle = 90U;
    src.message_id = gnsslog::unicore::kMessageIdObsVm;
    src.message_length = 44U;
    src.time_ref = 1U;
    src.time_status = 2U;
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
    Check(dst.version == src.version, "header version");
    Check(dst.reserved == src.reserved, "header reserved");
    Check(dst.leap_seconds == src.leap_seconds, "header leap_seconds");
    Check(dst.delay_ms == src.delay_ms, "header delay_ms");
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
    Check(NearlyEqual(dst[0].adr_cycles, src[0].adr_cycles, 1e-9), "OBSVM ADR");
    Check(dst[0].pseudorange_std_x100 == 52U, "OBSVM psr std raw scale");
    Check(dst[0].adr_std_x10000 == 181U, "OBSVM adr std raw scale");
    Check(dst[0].cn0_x100 == 4270U, "OBSVM C/N0 raw scale");
    Check(dst[0].tracking_status == 0x00181C23U, "OBSVM tracking status");
    Check(!gnsslog::unicore::DecodeObsVmPayload(payload, payload_size - 1U, dst, 2U, &count), "OBSVM truncated payload rejected");
}

void TestBinaryRecordCrc()
{
    const std::size_t payload_size = 4U;
    const std::size_t record_size = gnsslog::unicore::kBinaryHeaderSize + payload_size + gnsslog::unicore::kBinaryCrcSize;
    std::uint8_t record[32] = {};

    gnsslog::unicore::BinaryHeader header = {};
    header.cpu_idle = 50U;
    header.message_id = 8U;
    header.message_length = static_cast<std::uint16_t>(payload_size);
    header.week = 2190U;
    header.milliseconds = 1U;
    header.leap_seconds = 18U;

    Check(gnsslog::unicore::EncodeBinaryHeader(header, record, sizeof(record)), "record header encode");
    Check(gnsslog::WriteU32LE(record, sizeof(record), gnsslog::unicore::kBinaryHeaderSize, 0x12345678U), "record payload write");
    Check(gnsslog::unicore::WriteBinaryRecordCrc(record, record_size - gnsslog::unicore::kBinaryCrcSize, sizeof(record)), "record CRC write");
    Check(gnsslog::unicore::ValidateBinaryRecordCrc(record, record_size), "record CRC validate");
    record[gnsslog::unicore::kBinaryHeaderSize] ^= 0x01U;
    Check(!gnsslog::unicore::ValidateBinaryRecordCrc(record, record_size), "record CRC detects corruption");
}

}  // namespace

int main()
{
    TestByteIo();
    TestUnicoreCrcFromManual();
    TestUnicoreHeaderRoundTrip();
    TestObsVmRoundTrip();
    TestBinaryRecordCrc();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
