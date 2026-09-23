#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "novatel/novatel_protocol.h"
#include "range_converter.h"

int main()
{
    static const char input[] =
        "#RANGEA,COM1,0,54.0,FINESTEERING,2209,512449.000,02000020,5103,16809;"
        "1,26,0,24101771.233,0.199,-126655684.482618,0.012,2806.247,44.4,853.017,1810dc04*00000000";

    std::uint8_t* binary = NULL;
    std::size_t binary_size = 0U;
    char error[256] = {};
    if (!gnsslog::ConvertRangeAsciiLineToBinary(input,
                                                 &binary,
                                                 &binary_size,
                                                 error,
                                                 sizeof(error))) {
        std::fprintf(stderr, "ASCII -> binary failed: %s\n", error);
        return 1;
    }

    gnsslog::novatel::BinaryHeader header = {};
    if (!gnsslog::novatel::DecodeBinaryHeader(binary, binary_size, &header)) {
        std::fprintf(stderr, "binary header decode failed\n");
        gnsslog::FreeConvertedBuffer(binary);
        return 1;
    }

    if (header.port_address != 0x20U || binary[7] != 0x20U) {
        std::fprintf(stderr,
                     "COM1 was not encoded as port 0x20: header=0x%02X byte7=0x%02X\n",
                     static_cast<unsigned int>(header.port_address),
                     static_cast<unsigned int>(binary[7]));
        gnsslog::FreeConvertedBuffer(binary);
        return 1;
    }

    char* output = NULL;
    std::size_t output_size = 0U;
    if (!gnsslog::ConvertRangeBinaryRecordToAscii(binary,
                                                   binary_size,
                                                   &output,
                                                   &output_size,
                                                   error,
                                                   sizeof(error))) {
        std::fprintf(stderr, "binary -> ASCII failed: %s\n", error);
        gnsslog::FreeConvertedBuffer(binary);
        return 1;
    }

    const bool ok = std::strncmp(output, "#RANGEA,COM1,", 13U) == 0 &&
                    std::strstr(output, "#RANGEA,SPECIAL,") == NULL;
    if (!ok) {
        std::fprintf(stderr, "COM1 round trip failed. Output: %s\n", output);
    }

    gnsslog::FreeConvertedBuffer(output);
    gnsslog::FreeConvertedBuffer(binary);
    return ok ? 0 : 1;
}
