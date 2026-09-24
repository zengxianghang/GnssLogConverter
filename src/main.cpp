#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "eph_ion_converter.h"
#include "novatel/novatel_protocol.h"
#include "range_converter.h"

namespace {

enum Direction
{
    kDirectionAuto = 0,
    kDirectionToBinary,
    kDirectionToAscii
};

void PrintHelp(const char* exe)
{
    std::printf("GnssLogConverter - GNSS ASCII/Binary log converter\n\n");
    std::printf("Usage:\n");
    std::printf("  %s -h\n", exe);
    std::printf("  %s <input> <output> [--to ascii|binary]\n\n", exe);
    std::printf("Currently executable conversions:\n");
    std::printf("  NovAtel RANGEA <-> NovAtel RANGEB\n");
    std::printf("  Unicore OBSVMA -> NovAtel RANGEB\n");
    std::printf("  NovAtel GPSEPHEM/GLOEPHEMERIS/QZSSEPHEMERIS EPH ASCII <-> binary\n");
    std::printf("  NovAtel GALEPHEMERIS/BD2EPHEM EPH ASCII <-> binary\n");
    std::printf("  NovAtel IONUTC/BD2IONUTC/GALIONO ASCII <-> binary\n");
    std::printf("  NovAtel NAVICEPHEMERIS/BDSBCNAV1/2/3EPHEMERIS ASCII <-> binary\n");
    std::printf("  Unicore GPSEPH/GLOEPH/QZSSEPH/GALEPH/BDSEPH -> NovAtel binary\n");
    std::printf("  Unicore GPSION/BDSION/GALION -> NovAtel binary\n");
    std::printf("  Unicore IRNSSEPH/BD3EPH -> NovAtel binary\n\n");
    std::printf("Notes:\n");
    std::printf("  OBSVMA is converted directly to RANGEB; no OBSVMB is generated.\n");
    std::printf("  Unsupported ASCII and binary records are skipped silently.\n");
    std::printf("  BD3IONA is skipped because no verified equivalent NovAtel decoded log is available.\n");
    std::printf("  ch-tr-status is copied bit-for-bit without reinterpretation.\n");
}

bool ParseDirection(int argc, char** argv, Direction* direction)
{
    if (direction == NULL) {
        return false;
    }
    *direction = kDirectionAuto;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--to") == 0) {
            if ((i + 1) >= argc) {
                return false;
            }
            ++i;
            if (std::strcmp(argv[i], "binary") == 0) {
                *direction = kDirectionToBinary;
            } else if (std::strcmp(argv[i], "ascii") == 0) {
                *direction = kDirectionToAscii;
            } else {
                return false;
            }
        } else if (std::strcmp(argv[i], "--vendor") == 0) {
            if ((i + 1) >= argc) {
                return false;
            }
            ++i;  // Kept for CLI compatibility; message name decides the parser.
        } else {
            return false;
        }
    }
    return true;
}

bool InputLooksBinary(const char* path, bool* is_binary)
{
    if (path == NULL || is_binary == NULL) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    unsigned char prefix[3] = {0U, 0U, 0U};
    input.read(reinterpret_cast<char*>(prefix), 3);
    if (input.gcount() < 1) {
        return false;
    }
    *is_binary = input.gcount() >= 3 &&
        prefix[0] == 0xAAU && prefix[1] == 0x44U && prefix[2] == 0x12U;
    return true;
}

int ConvertAsciiFileToBinary(const char* input_path, const char* output_path)
{
    std::ifstream input(input_path);
    if (!input) {
        std::fprintf(stderr, "[ERROR] Cannot open input file: %s\n", input_path);
        return 2;
    }
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        std::fprintf(stderr, "[ERROR] Cannot open output file: %s\n", output_path);
        return 2;
    }

    std::string line;
    std::size_t line_number = 0U;
    std::size_t converted = 0U;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line[line.size() - 1U] == '\r') {
            line.erase(line.size() - 1U);
        }
        if (line.empty()) {
            continue;
        }

        const bool is_range = gnsslog::IsSupportedRangeAsciiLine(line.c_str());
        const bool is_eph_ion = gnsslog::IsSupportedEphIonAsciiLine(line.c_str());
        if (!is_range && !is_eph_ion) {
            continue;
        }

        std::uint8_t* record = NULL;
        std::size_t record_size = 0U;
        char error[256] = {};
        const bool ok = is_range
            ? gnsslog::ConvertRangeAsciiLineToBinary(line.c_str(),
                                                      &record,
                                                      &record_size,
                                                      error,
                                                      sizeof(error))
            : gnsslog::ConvertEphIonAsciiLineToBinary(line.c_str(),
                                                       &record,
                                                       &record_size,
                                                       error,
                                                       sizeof(error));
        if (!ok) {
            std::fprintf(stderr, "[ERROR] Line %zu: %s\n", line_number, error);
            return 3;
        }

        output.write(reinterpret_cast<const char*>(record),
                     static_cast<std::streamsize>(record_size));
        gnsslog::FreeConvertedBuffer(record);
        if (!output) {
            std::fprintf(stderr, "[ERROR] Failed writing output file.\n");
            return 4;
        }
        ++converted;
    }

    std::printf("Converted %zu ASCII record(s) to NovAtel binary: %s\n",
                converted,
                output_path);
    return 0;
}

int ConvertBinaryFileToAscii(const char* input_path, const char* output_path)
{
    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        std::fprintf(stderr, "[ERROR] Cannot open input file: %s\n", input_path);
        return 2;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff file_size = input.tellg();
    if (file_size <= 0) {
        std::fprintf(stderr, "[ERROR] Input file is empty.\n");
        return 2;
    }
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    input.read(reinterpret_cast<char*>(&bytes[0]), file_size);
    if (!input) {
        std::fprintf(stderr, "[ERROR] Failed reading input file.\n");
        return 2;
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        std::fprintf(stderr, "[ERROR] Cannot open output file: %s\n", output_path);
        return 2;
    }

    std::size_t offset = 0U;
    std::size_t converted = 0U;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        gnsslog::novatel::BinaryHeader header = {};
        if (!gnsslog::novatel::DecodeBinaryHeader(&bytes[offset], remaining, &header)) {
            std::fprintf(stderr, "[ERROR] Invalid NovAtel binary header at byte %zu.\n", offset);
            return 3;
        }
        const std::size_t record_size = gnsslog::novatel::kBinaryHeaderSize +
            static_cast<std::size_t>(header.message_length) + gnsslog::novatel::kBinaryCrcSize;
        if (record_size > remaining) {
            std::fprintf(stderr, "[ERROR] Truncated binary record at byte %zu.\n", offset);
            return 3;
        }
        if (!gnsslog::novatel::ValidateBinaryRecordCrc(&bytes[offset], record_size)) {
            std::fprintf(stderr, "[ERROR] Invalid NovAtel binary CRC at byte %zu.\n", offset);
            return 3;
        }

        const bool is_range = header.message_id == gnsslog::novatel::kMessageIdRange;
        const bool is_eph_ion = gnsslog::IsSupportedEphIonBinaryMessageId(header.message_id);
        if (!is_range && !is_eph_ion) {
            offset += record_size;
            continue;
        }

        char* line = NULL;
        std::size_t line_size = 0U;
        char error[256] = {};
        const bool ok = is_range
            ? gnsslog::ConvertRangeBinaryRecordToAscii(&bytes[offset],
                                                        record_size,
                                                        &line,
                                                        &line_size,
                                                        error,
                                                        sizeof(error))
            : gnsslog::ConvertEphIonBinaryRecordToAscii(&bytes[offset],
                                                         record_size,
                                                         &line,
                                                         &line_size,
                                                         error,
                                                         sizeof(error));
        if (!ok) {
            std::fprintf(stderr, "[ERROR] Binary record at byte %zu: %s\n", offset, error);
            return 3;
        }
        output.write(line, static_cast<std::streamsize>(line_size));
        gnsslog::FreeConvertedBuffer(line);
        if (!output) {
            std::fprintf(stderr, "[ERROR] Failed writing output file.\n");
            return 4;
        }

        offset += record_size;
        ++converted;
    }

    std::printf("Converted %zu supported NovAtel binary record(s) to ASCII: %s\n",
                converted,
                output_path);
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc == 1 ||
        (argc == 2 && (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0))) {
        PrintHelp(argv[0]);
        return 0;
    }
    if (argc < 3) {
        PrintHelp(argv[0]);
        return 1;
    }

    Direction direction = kDirectionAuto;
    if (!ParseDirection(argc, argv, &direction)) {
        std::fprintf(stderr, "[ERROR] Invalid command line.\n");
        PrintHelp(argv[0]);
        return 1;
    }

    bool binary_input = false;
    if (!InputLooksBinary(argv[1], &binary_input)) {
        std::fprintf(stderr, "[ERROR] Cannot detect input format: %s\n", argv[1]);
        return 2;
    }

    if (direction == kDirectionToBinary && binary_input) {
        std::fprintf(stderr, "[ERROR] --to binary was requested but input is already binary.\n");
        return 1;
    }
    if (direction == kDirectionToAscii && !binary_input) {
        std::fprintf(stderr, "[ERROR] --to ascii was requested but input is ASCII.\n");
        return 1;
    }

    return binary_input
        ? ConvertBinaryFileToAscii(argv[1], argv[2])
        : ConvertAsciiFileToBinary(argv[1], argv[2]);
}
