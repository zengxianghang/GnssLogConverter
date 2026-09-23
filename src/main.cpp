#include <cstdio>
#include <cstring>

namespace {

void PrintHelp(const char* exe)
{
    std::printf("GnssLogConverter - GNSS ASCII/Binary log converter\n\n");
    std::printf("Usage:\n");
    std::printf("  %s -h\n", exe);
    std::printf("  %s --help\n", exe);
    std::printf("  %s <input> <output> [--to ascii|binary] [--vendor novatel|unicore]\n\n", exe);
    std::printf("Current implementation status:\n");
    std::printf("  Core little-endian I/O, Unicore CRC32, Unicore 24-byte binary header,\n");
    std::printf("  and Unicore OBSVM binary payload codec are implemented.\n");
    std::printf("  End-to-end file conversion will be enabled as message codecs are completed.\n");
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc == 1 || (argc == 2 && (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0))) {
        PrintHelp(argv[0]);
        return 0;
    }

    std::fprintf(stderr,
                 "End-to-end conversion is not enabled in this bootstrap revision yet. "
                 "Run with -h for the current status.\n");
    return 2;
}
