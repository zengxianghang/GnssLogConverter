#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "novatel/novatel_protocol.h"
#include "novatel/novatel_range.h"
#include "range_converter.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

void TestNovAtelCom1FineRoundTrip()
{
    // RANGE observation values are taken from the first observation in the
    // NovAtel OEM7 RANGEA documentation example. The header port/time-status
    // are intentionally COM1/FINE so their ASCII <-> binary mappings can be
    // checked without the lossy USB1 -> 0xA0 detailed-port truncation.
    static const char input[] =
        "#RANGEA,COM1,0,54.0,FINE,2209,512449.000,02000020,5103,16809;"
        "1,26,0,24101771.233,0.199,-126655684.482618,0.012,2806.247,44.4,853.017,1810dc04*00000000";

    std::uint8_t* binary = NULL;
    std::size_t binary_size = 0U;
    char error[256] = {};
    Check(gnsslog::ConvertRangeAsciiLineToBinary(input,
                                                  &binary,
                                                  &binary_size,
                                                  error,
                                                  sizeof(error)),
          "RANGEA COM1/FINE converts to RANGEB");
    if (binary == NULL) {
        return;
    }

    gnsslog::novatel::BinaryHeader header = {};
    Check(gnsslog::novatel::DecodeBinaryHeader(binary, binary_size, &header),
          "RANGEA COM1/FINE target header decodes");
    Check(binary_size >= gnsslog::novatel::kBinaryHeaderSize,
          "RANGEB contains a complete standard header");
    Check(binary[7] == 0x20U, "COM1 is encoded as binary header byte 7 = 0x20");
    Check(header.port_address == 0x20U, "decoded detailed port identifier is COM1/0x20");
    Check(binary[13] == 160U, "FINE is encoded as binary header byte 13 = 160");
    Check(header.time_status == 160U, "decoded TimeStatus is FINE/160");
    Check(header.week == 2209U && header.milliseconds == 512449000U,
          "RANGEA GPS week/time are preserved in RANGEB header");
    Check(gnsslog::novatel::ValidateBinaryRecordCrc(binary, binary_size),
          "COM1/FINE RANGEB CRC is valid");

    gnsslog::novatel::RangeMeasurement obs = {};
    std::uint32_t count = 0U;
    Check(gnsslog::novatel::DecodeRangePayload(binary + gnsslog::novatel::kBinaryHeaderSize,
                                               header.message_length,
                                               &obs,
                                               1U,
                                               &count),
          "RANGEA website-example observation decodes from generated RANGEB");
    Check(count == 1U, "RANGEA website-example excerpt keeps one observation");
    Check(obs.prn_slot == 26U && obs.glofreq == 0U,
          "RANGEA website-example PRN/glofreq preserved");
    Check(obs.tracking_status == 0x1810DC04U,
          "RANGEA website-example ch-tr-status preserved bit-for-bit");

    char* output = NULL;
    std::size_t output_size = 0U;
    Check(gnsslog::ConvertRangeBinaryRecordToAscii(binary,
                                                    binary_size,
                                                    &output,
                                                    &output_size,
                                                    error,
                                                    sizeof(error)),
          "RANGEB COM1/FINE converts back to RANGEA");
    if (output != NULL) {
        static const char expected_prefix[] = "#RANGEA,COM1,0,54.0,FINE,";
        Check(std::strncmp(output, expected_prefix, std::strlen(expected_prefix)) == 0,
              "round trip restores COM1 and FINE labels exactly");
        Check(std::strstr(output, "#RANGEA,SPECIAL,") == NULL,
              "COM1 round trip never becomes SPECIAL");
        Check(std::strstr(output, ",FINESTEERING,") == NULL,
              "FINE round trip is not changed to FINESTEERING");
        Check(std::strstr(output, ",1810dc04*") != NULL,
              "round trip retains tracking-status text value");
        gnsslog::FreeConvertedBuffer(output);
    }

    gnsslog::FreeConvertedBuffer(binary);
}

void TestUnicoreGpsFineLabels()
{
    // Header and first observation follow the Unicore N4 OBSVMA manual sample.
    // GPS is the Unicore ASCII TimeRef label. The target NovAtel standard
    // binary header has no TimeRef field, so this test verifies that GPS is
    // accepted and that its week/ms are carried into the target header.
    static const char input[] =
        "#OBSVMA,94,GPS,FINE,2190,117395000,0,0,18,17;"
        "1,0,26,21720097.812,-114139892.254585,52,181,-2263.222,4270,0,6262.010,00181c23*00000000";

    std::uint8_t* binary = NULL;
    std::size_t binary_size = 0U;
    char error[256] = {};
    Check(gnsslog::ConvertRangeAsciiLineToBinary(input,
                                                  &binary,
                                                  &binary_size,
                                                  error,
                                                  sizeof(error)),
          "OBSVMA GPS/FINE labels are accepted");
    if (binary == NULL) {
        return;
    }

    gnsslog::novatel::BinaryHeader header = {};
    Check(gnsslog::novatel::DecodeBinaryHeader(binary, binary_size, &header),
          "OBSVMA GPS/FINE target header decodes");
    Check(header.message_id == gnsslog::novatel::kMessageIdRange,
          "OBSVMA GPS/FINE target is NovAtel RANGE message ID 43");
    Check(header.time_status == 160U && binary[13] == 160U,
          "Unicore FINE label maps to NovAtel TimeStatus 160");
    Check(header.week == 2190U && header.milliseconds == 117395000U,
          "Unicore GPS TimeRef week/ms are copied to NovAtel GPS week/ms");
    Check(header.port_address == 0xC0U,
          "OBSVMA without a source COM field uses target THISPORT policy");
    Check(gnsslog::novatel::ValidateBinaryRecordCrc(binary, binary_size),
          "OBSVMA GPS/FINE target RANGEB CRC is valid");

    char* output = NULL;
    std::size_t output_size = 0U;
    Check(gnsslog::ConvertRangeBinaryRecordToAscii(binary,
                                                    binary_size,
                                                    &output,
                                                    &output_size,
                                                    error,
                                                    sizeof(error)),
          "OBSVMA-generated RANGEB converts to RANGEA");
    if (output != NULL) {
        Check(std::strstr(output, ",FINE,2190,117395.000,") != NULL,
              "FINE and GPS week/time survive OBSVMA -> RANGEB -> RANGEA");
        Check(std::strstr(output, "00181c23") != NULL,
              "OBSVMA tracking status survives round trip");
        gnsslog::FreeConvertedBuffer(output);
    }

    gnsslog::FreeConvertedBuffer(binary);
}

void TestNovAtelWebsiteRangeExampleHeader()
{
    // Direct excerpt of the current OEM7 RANGE page: original header plus its
    // first observation, with #obs reduced from 154 to 1 for an isolated test.
    static const char input[] =
        "#RANGEA,USB1,0,54.0,FINESTEERING,2209,512449.000,02000020,5103,16809;"
        "1,26,0,24101771.233,0.199,-126655684.482618,0.012,2806.247,44.4,853.017,1810dc04*00000000";

    std::uint8_t* binary = NULL;
    std::size_t binary_size = 0U;
    char error[256] = {};
    Check(gnsslog::ConvertRangeAsciiLineToBinary(input,
                                                  &binary,
                                                  &binary_size,
                                                  error,
                                                  sizeof(error)),
          "NovAtel website RANGEA excerpt converts to RANGEB");
    if (binary == NULL) {
        return;
    }

    gnsslog::novatel::BinaryHeader header = {};
    Check(gnsslog::novatel::DecodeBinaryHeader(binary, binary_size, &header),
          "NovAtel website RANGEA excerpt target header decodes");
    Check(header.port_address == 0xA0U && binary[7] == 0xA0U,
          "website USB1 detailed port uses lower 8 bits 0xA0");
    Check(header.time_status == 180U && binary[13] == 180U,
          "website FINESTEERING label maps to TimeStatus 180");
    Check(header.sequence == 0U && header.idle_time == 108U,
          "website sequence/idle fields map correctly");
    Check(header.receiver_status == 0x02000020U &&
          header.reserved == 0x5103U &&
          header.receiver_sw_version == 16809U,
          "website RANGEA remaining header fields map correctly");
    Check(gnsslog::novatel::ValidateBinaryRecordCrc(binary, binary_size),
          "website RANGEA excerpt generated RANGEB CRC is valid");

    gnsslog::FreeConvertedBuffer(binary);
}

}  // namespace

int main()
{
    TestNovAtelCom1FineRoundTrip();
    TestUnicoreGpsFineLabels();
    TestNovAtelWebsiteRangeExampleHeader();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d ASCII-label/round-trip test(s) failed.\n", g_failures);
        return 1;
    }

    std::printf("All ASCII label tests passed: COM1, GPS, FINE, and NovAtel RANGE example.\n");
    return 0;
}
