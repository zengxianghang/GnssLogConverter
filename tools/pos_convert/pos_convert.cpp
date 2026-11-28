/*
 * pos_convert.cpp
 *
 * One executable, five conversion modes:
 *   1) Position result -> GGA + INSPVA + BESTPOS
 *   2) INSPVA ASCII    -> BESTPOS ASCII
 *   3) INSPVAX ASCII   -> INSPVA ASCII
 *   4) INSPVAX ASCII   -> BESTPOS ASCII
 *   5) CSV/TSV          -> INSPVA ASCII
 *
 * Key behavior:
 *   - INSPVAX source CRC is always ignored.
 *   - Generated NovAtel INSPVA/BESTPOS CRC32 is always recalculated.
 *   - INSPVAX -> INSPVA uses ellipsoidal height = MSL height + undulation.
 *   - INSPVAX -> BESTPOS copies MSL height, undulation, position type and
 *     position standard deviations directly from INSPVAX.
 *   - --rate supports integer-factor downsampling only; no interpolation.
 *   - --rate OUT_HZ auto-detects the input rate from GNSS timestamps.
 *
 * Run "pos_convert.exe --help" for complete usage.
 */

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define LINE_BUFFER_SIZE      4096
#define SENTENCE_BUFFER_SIZE  2048
#define TOKEN_MAX_COUNT       32
#define FILE_NAME_BUFFER_SIZE 2048

static const double PI_VALUE =
    3.1415926535897932384626433832795;
static const double DEG_TO_RAD = PI_VALUE / 180.0;
static const double GNSS_WEEK_SECONDS = 604800.0;

static const char *GGA_SENTENCE_ID = "GNGGA";
static const char *NOVATEL_PORT = "COM1";
static const char *NOVATEL_TIME_STATUS = "FINESTEERING";
static const char *BESTPOS_VALID_POS_TYPE = "SINGLE";

enum ProgramMode
{
    PROGRAM_MODE_POSITION_TO_ALL = 0,
    PROGRAM_MODE_INSPVA_TO_BESTPOS = 1,
    PROGRAM_MODE_INSPVAX_TO_INSPVA = 2,
    PROGRAM_MODE_INSPVAX_TO_BESTPOS = 3,
    PROGRAM_MODE_CSV_TO_INSPVA = 4
};

struct InspvaRecord
{
    bool shortHeader;

    char port[32];
    char sequence[32];
    char idleTime[32];
    char timeStatus[32];
    char receiverStatus[32];
    char reserved[32];
    char receiverVersion[32];

    unsigned int week;
    double second;

    double latitude;
    double longitude;
    double ellipsoidalHeight;

    double northVelocity;
    double eastVelocity;
    double upVelocity;

    double roll;
    double pitch;
    double azimuth;

    char insStatus[64];
};


struct InspvaxRecord
{
    char port[32];
    char sequence[32];
    char idleTime[32];
    char timeStatus[32];
    char receiverStatus[32];
    char reserved[32];
    char receiverVersion[32];

    unsigned int week;
    double second;

    char insStatus[64];
    char posType[64];

    double latitude;
    double longitude;
    double mslHeight;
    double undulation;

    double northVelocity;
    double eastVelocity;
    double upVelocity;

    double roll;
    double pitch;
    double azimuth;

    double latSigma;
    double lonSigma;
    double heightSigma;

    char extSolStat[32];
};

struct PositionRecord
{
    bool hasWeek;
    unsigned int week;

    double utcSecond;
    char utcHms[32];

    double latitude;
    double longitude;
    double hEll;

    double velocityX;
    double velocityY;
    double velocityZ;

    int sourceQ;

    double heading;
    double roll;
    double pitch;

    /* Source units are cm. */
    double sdHeightCm;
    double sdHorizCm;
};

struct ProgramOptions
{
    ProgramMode mode;

    const char *inputFileName;
    const char *outputPrefix;

    unsigned int defaultWeek;
    double utcOffset;
    double geoidSeparation;
    int satelliteNumber;
    double hdop;

    /* Used when INSPVA is converted to BESTPOS. */
    double inspvaHorizontalSigma;
    double inspvaHeightSigma;
    const char *inspvaBestPosType;

    /* INSPVAX -> INSPVA: if false, non-INSPVAX records are copied unchanged. */
    bool onlyInspva;

    bool rateEnabled;
    bool autoDetectInputHz;
    unsigned int inputHz;
    unsigned int outputHz;
    unsigned int decimationFactor;

    /* CSV has no roll/pitch columns: use explicit defaults or overrides. */
    double csvRoll;
    double csvPitch;
};

static void PrintUsage(const char *programName)
{
    std::printf(
        "Usage:\n"
        "\n"
        "  1) Position result -> GGA + INSPVA + BESTPOS:\n"
        "     %s input.txt output_prefix [options]\n"
        "\n"
        "  2) INSPVA ASCII -> BESTPOS ASCII:\n"
        "     %s --inspva-to-bestpos input.inspva output.bestpos [options]\n"
        "\n"
        "  3) INSPVAX ASCII -> INSPVA ASCII:\n"
        "     %s --inspvax-to-inspva input.log output.log [options]\n"
        "\n"
        "  4) INSPVAX ASCII -> BESTPOS ASCII:\n"
        "     %s --inspvax-to-bestpos input.log output.bestpos [options]\n"
        "\n"
        "  5) CSV/TSV -> INSPVA ASCII:\n"
        "     %s --csv-to-inspva input.csv output.inspva [options]\n"
        "\n"
        "Common options:\n"
        "  --sat-num N          Satellite count written to GGA/BESTPOS, default 0\n"
        "  --rate OUT_HZ        Auto-detect input rate and downsample without interpolation\n"
        "  --rate IN_HZ OUT_HZ  Manual source/output rates; IN_HZ must be exactly\n"
        "                       divisible by OUT_HZ\n"
        "  -h, --help           Show this help\n"
        "\n"
        "Position-result-only options:\n"
        "  --week N             GNSS week used when the input has no Week field\n"
        "  --utc-offset SEC     GNSS-UTC offset in seconds, default 18\n"
        "  --geoid-sep M        Geoid undulation in metres, default 0\n"
        "  --hdop VALUE         HDOP written to GGA, default 1.0\n"
        "\n"
        "INSPVA-to-BESTPOS-only options:\n"
        "  --geoid-sep M        Geoid undulation in metres, default 0\n"
        "  --pos-type TYPE      BESTPOS position type for valid INS solutions,\n"
        "                       default INS_PSRSP\n"
        "  --horiz-sigma M      BESTPOS latitude/longitude sigma, default 0\n"
        "  --height-sigma M     BESTPOS height sigma, default 0\n"
        "\n"
        "INSPVAX-to-INSPVA-only options:\n"
        "  --only-inspva        Output only converted INSPVA records. Without this\n"
        "                       option, non-INSPVAX records are copied unchanged.\n"
        "  --ignore-input-crc   Compatibility option; INSPVAX source CRC is always ignored\n"
        "\n"
        "CSV-to-INSPVA-only options:\n"
        "  --week N             GNSS week of first CSV sample (required unless UTC\n"
        "                       contains full date-time and Leap is known)\n"
        "  --roll DEG           Constant roll, default 0 (CSV has no roll column)\n"
        "  --pitch DEG          Constant pitch, default 0 (CSV has no pitch column)\n"
        "  CSV: TOW is already GNSS seconds-of-week; Leap is NOT added again.\n"
        "  UTC full date-time and Leap may be used to infer week if --week absent.\n"
        "\n"
        "INSPVAX-to-BESTPOS notes:\n"
        "  - MSL height, undulation, position type and position sigmas are taken\n"
        "    directly from INSPVAX.\n"
        "  - INSPVAX does not contain satellite counts; use --sat-num if required.\n"
        "  - The source INSPVAX CRC is ignored and the BESTPOS CRC is recalculated.\n"
        "\n"
        "Examples:\n"
        "  %s input.txt result --week 2362 --rate 10\n"
        "  %s --inspva-to-bestpos input.inspva output.bestpos --geoid-sep -17\n"
        "  %s --inspvax-to-inspva input.log output.log --only-inspva\n"
        "  %s --inspvax-to-bestpos input.log output.bestpos --sat-num 30 --rate 10\n"
        "  %s --csv-to-inspva input.csv output.inspva --week 2447\n"
        "  %s --csv-to-inspva input.tsv output.inspva --week 2447 --rate 10\n"
        "  %s --csv-to-inspva input.csv output.inspva --roll 0 --pitch 0\n"
        "  %s -h\n",
        programName,
        programName,
        programName,
        programName,
        programName,
        programName,
        programName,
        programName,
        programName,
        programName,
        programName,
        programName,
        programName);
}

static bool ParseDoubleStrict(const char *text, double *value)
{
    if (text == NULL || value == NULL || *text == '\0')
    {
        return false;
    }

    errno = 0;
    char *end = NULL;
    const double parsedValue = std::strtod(text, &end);

    if (end == text || *end != '\0' || errno == ERANGE ||
        !std::isfinite(parsedValue))
    {
        return false;
    }

    *value = parsedValue;
    return true;
}

static bool ParseLongStrict(const char *text, long *value)
{
    if (text == NULL || value == NULL || *text == '\0')
    {
        return false;
    }

    errno = 0;
    char *end = NULL;
    const long parsedValue = std::strtol(text, &end, 10);

    if (end == text || *end != '\0' || errno == ERANGE)
    {
        return false;
    }

    *value = parsedValue;
    return true;
}

static bool ParseUnsignedIntStrict(const char *text,
                                   unsigned int *value)
{
    if (text == NULL || value == NULL || *text == '\0')
    {
        return false;
    }

    double parsedValue = 0.0;

    if (!ParseDoubleStrict(text, &parsedValue) ||
        parsedValue < 0.0 ||
        parsedValue > static_cast<double>(UINT_MAX))
    {
        return false;
    }

    const double roundedValue = std::floor(parsedValue + 0.5);

    if (std::fabs(parsedValue - roundedValue) > 1e-6)
    {
        return false;
    }

    *value = static_cast<unsigned int>(roundedValue);
    return true;
}

static bool ParseIntStrict(const char *text, int *value)
{
    long parsedValue = 0;

    if (!ParseLongStrict(text, &parsedValue) ||
        parsedValue < INT_MIN || parsedValue > INT_MAX)
    {
        return false;
    }

    *value = static_cast<int>(parsedValue);
    return true;
}

static bool ParseCommandLine(int argc,
                             char *argv[],
                             ProgramOptions *options)
{
    if (options == NULL)
    {
        return false;
    }

    if (argc == 2 && (std::strcmp(argv[1], "--help") == 0 ||
                      std::strcmp(argv[1], "-h") == 0))
    {
        PrintUsage(argv[0]);
        return false;
    }

    options->mode = PROGRAM_MODE_POSITION_TO_ALL;
    options->inputFileName = NULL;
    options->outputPrefix = NULL;
    options->defaultWeek = 0;
    options->utcOffset = 18.0;
    options->geoidSeparation = 0.0;
    options->satelliteNumber = 0;
    options->hdop = 1.0;
    options->inspvaHorizontalSigma = 0.0;
    options->inspvaHeightSigma = 0.0;
    options->inspvaBestPosType = "INS_PSRSP";
    options->onlyInspva = false;
    options->rateEnabled = false;
    options->autoDetectInputHz = false;
    options->inputHz = 0;
    options->outputHz = 0;
    options->decimationFactor = 1;
    options->csvRoll = 0.0;
    options->csvPitch = 0.0;

    int index = 0;

    if (argc >= 2 && std::strcmp(argv[1], "--inspva-to-bestpos") == 0)
    {
        if (argc < 4)
        {
            PrintUsage(argv[0]);
            return false;
        }

        options->mode = PROGRAM_MODE_INSPVA_TO_BESTPOS;
        options->inputFileName = argv[2];
        options->outputPrefix = argv[3];
        index = 4;
    }
    else if (argc >= 2 && std::strcmp(argv[1], "--inspvax-to-inspva") == 0)
    {
        if (argc < 4)
        {
            PrintUsage(argv[0]);
            return false;
        }

        options->mode = PROGRAM_MODE_INSPVAX_TO_INSPVA;
        options->inputFileName = argv[2];
        options->outputPrefix = argv[3];
        index = 4;
    }
    else if (argc >= 2 && std::strcmp(argv[1], "--inspvax-to-bestpos") == 0)
    {
        if (argc < 4)
        {
            PrintUsage(argv[0]);
            return false;
        }

        options->mode = PROGRAM_MODE_INSPVAX_TO_BESTPOS;
        options->inputFileName = argv[2];
        options->outputPrefix = argv[3];
        index = 4;
    }
    else if (argc >= 2 && std::strcmp(argv[1], "--csv-to-inspva") == 0)
    {
        if (argc < 4)
        {
            PrintUsage(argv[0]);
            return false;
        }

        options->mode = PROGRAM_MODE_CSV_TO_INSPVA;
        options->inputFileName = argv[2];
        options->outputPrefix = argv[3];
        index = 4;
    }
    else
    {
        if (argc < 3)
        {
            PrintUsage(argv[0]);
            return false;
        }

        options->mode = PROGRAM_MODE_POSITION_TO_ALL;
        options->inputFileName = argv[1];
        options->outputPrefix = argv[2];
        index = 3;
    }

    while (index < argc)
    {
        const char *option = argv[index];

        if (std::strcmp(option, "--help") == 0 ||
            std::strcmp(option, "-h") == 0)
        {
            PrintUsage(argv[0]);
            return false;
        }
        else if (std::strcmp(option, "--week") == 0)
        {
            if (options->mode != PROGRAM_MODE_POSITION_TO_ALL &&
                options->mode != PROGRAM_MODE_CSV_TO_INSPVA)
            {
                std::printf("--week is only valid in position-result and CSV modes.\n");
                return false;
            }

            if (index + 1 >= argc ||
                !ParseUnsignedIntStrict(argv[index + 1],
                                        &options->defaultWeek))
            {
                std::printf("Invalid --week value.\n");
                return false;
            }

            index += 2;
        }
        else if (std::strcmp(option, "--roll") == 0 ||
                 std::strcmp(option, "--pitch") == 0)
        {
            if (options->mode != PROGRAM_MODE_CSV_TO_INSPVA)
            {
                std::printf("--roll and --pitch are only valid in CSV-to-INSPVA mode.\n");
                return false;
            }
            double value = 0.0;
            if (index + 1 >= argc ||
                !ParseDoubleStrict(argv[index + 1], &value))
            {
                std::printf("Invalid --roll/--pitch value.\n");
                return false;
            }
            if (std::strcmp(option, "--roll") == 0)
                options->csvRoll = value;
            else
                options->csvPitch = value;
            index += 2;
        }
        else if (std::strcmp(option, "--utc-offset") == 0)
        {
            if (options->mode != PROGRAM_MODE_POSITION_TO_ALL)
            {
                std::printf("--utc-offset is only valid in position-result mode.\n");
                return false;
            }

            if (index + 1 >= argc ||
                !ParseDoubleStrict(argv[index + 1],
                                   &options->utcOffset))
            {
                std::printf("Invalid --utc-offset value.\n");
                return false;
            }

            index += 2;
        }
        else if (std::strcmp(option, "--geoid-sep") == 0)
        {
            if (options->mode != PROGRAM_MODE_POSITION_TO_ALL &&
                options->mode != PROGRAM_MODE_INSPVA_TO_BESTPOS)
            {
                std::printf(
                    "--geoid-sep is not needed in INSPVAX modes because INSPVAX already contains MSL height and undulation.\n");
                return false;
            }

            if (index + 1 >= argc ||
                !ParseDoubleStrict(argv[index + 1],
                                   &options->geoidSeparation))
            {
                std::printf("Invalid --geoid-sep value.\n");
                return false;
            }

            index += 2;
        }
        else if (std::strcmp(option, "--sat-num") == 0)
        {
            if (options->mode == PROGRAM_MODE_INSPVAX_TO_INSPVA)
            {
                std::printf("--sat-num is not used in INSPVAX-to-INSPVA mode.\n");
                return false;
            }

            if (index + 1 >= argc ||
                !ParseIntStrict(argv[index + 1],
                                &options->satelliteNumber) ||
                options->satelliteNumber < 0 ||
                options->satelliteNumber > 255)
            {
                std::printf("Invalid --sat-num value; expected 0 to 255.\n");
                return false;
            }

            index += 2;
        }
        else if (std::strcmp(option, "--hdop") == 0)
        {
            if (options->mode != PROGRAM_MODE_POSITION_TO_ALL)
            {
                std::printf("--hdop is only valid in position-result mode.\n");
                return false;
            }

            if (index + 1 >= argc ||
                !ParseDoubleStrict(argv[index + 1],
                                   &options->hdop) ||
                options->hdop < 0.0)
            {
                std::printf("Invalid --hdop value.\n");
                return false;
            }

            index += 2;
        }
        else if (std::strcmp(option, "--pos-type") == 0)
        {
            if (options->mode != PROGRAM_MODE_INSPVA_TO_BESTPOS)
            {
                std::printf(
                    "--pos-type is only valid in INSPVA-to-BESTPOS mode. INSPVAX already contains its position type.\n");
                return false;
            }

            if (index + 1 >= argc || argv[index + 1][0] == '\0')
            {
                std::printf("Invalid --pos-type value.\n");
                return false;
            }

            options->inspvaBestPosType = argv[index + 1];
            index += 2;
        }
        else if (std::strcmp(option, "--horiz-sigma") == 0)
        {
            if (options->mode != PROGRAM_MODE_INSPVA_TO_BESTPOS)
            {
                std::printf(
                    "--horiz-sigma is only valid in INSPVA-to-BESTPOS mode. INSPVAX already contains position sigmas.\n");
                return false;
            }

            if (index + 1 >= argc ||
                !ParseDoubleStrict(argv[index + 1],
                                   &options->inspvaHorizontalSigma) ||
                options->inspvaHorizontalSigma < 0.0)
            {
                std::printf("Invalid --horiz-sigma value.\n");
                return false;
            }

            index += 2;
        }
        else if (std::strcmp(option, "--height-sigma") == 0)
        {
            if (options->mode != PROGRAM_MODE_INSPVA_TO_BESTPOS)
            {
                std::printf(
                    "--height-sigma is only valid in INSPVA-to-BESTPOS mode. INSPVAX already contains height sigma.\n");
                return false;
            }

            if (index + 1 >= argc ||
                !ParseDoubleStrict(argv[index + 1],
                                   &options->inspvaHeightSigma) ||
                options->inspvaHeightSigma < 0.0)
            {
                std::printf("Invalid --height-sigma value.\n");
                return false;
            }

            index += 2;
        }
        else if (std::strcmp(option, "--only-inspva") == 0)
        {
            if (options->mode != PROGRAM_MODE_INSPVAX_TO_INSPVA)
            {
                std::printf("--only-inspva is only valid in INSPVAX-to-INSPVA mode.\n");
                return false;
            }

            options->onlyInspva = true;
            index += 1;
        }
        else if (std::strcmp(option, "--ignore-input-crc") == 0)
        {
            if (options->mode != PROGRAM_MODE_INSPVAX_TO_INSPVA &&
                options->mode != PROGRAM_MODE_INSPVAX_TO_BESTPOS)
            {
                std::printf("--ignore-input-crc is only valid in INSPVAX modes.\n");
                return false;
            }

            /* Compatibility no-op: source INSPVAX CRC is always ignored. */
            index += 1;
        }
        else if (std::strcmp(option, "--rate") == 0)
        {
            unsigned int firstRate = 0;

            if (index + 1 >= argc ||
                !ParseUnsignedIntStrict(argv[index + 1], &firstRate) ||
                firstRate == 0)
            {
                std::printf(
                    "Invalid --rate value; expected a positive integer.\n");
                return false;
            }

            options->rateEnabled = true;

            if (index + 2 < argc &&
                std::strncmp(argv[index + 2], "--", 2) != 0)
            {
                unsigned int secondRate = 0;

                if (!ParseUnsignedIntStrict(argv[index + 2],
                                            &secondRate) ||
                    secondRate == 0)
                {
                    std::printf(
                        "Invalid --rate values; frequencies must be positive integers.\n");
                    return false;
                }

                options->autoDetectInputHz = false;
                options->inputHz = firstRate;
                options->outputHz = secondRate;

                if (options->outputHz > options->inputHz ||
                    options->inputHz % options->outputHz != 0)
                {
                    std::printf(
                        "Invalid --rate values: input frequency must be exactly divisible by output frequency.\n");
                    return false;
                }

                options->decimationFactor =
                    options->inputHz / options->outputHz;
                index += 3;
            }
            else
            {
                options->autoDetectInputHz = true;
                options->inputHz = 0;
                options->outputHz = firstRate;
                options->decimationFactor = 1;
                index += 2;
            }
        }
        else
        {
            std::printf("Unknown option: %s\n", option);
            return false;
        }
    }

    return true;
}

static int SplitLine(char *line,
                     char *tokens[],
                     int maxTokenCount)
{
    if (line == NULL || tokens == NULL || maxTokenCount <= 0)
    {
        return 0;
    }

    int tokenCount = 0;
    char *token = std::strtok(line, " \t,\r\n");

    while (token != NULL && tokenCount < maxTokenCount)
    {
        tokens[tokenCount] = token;
        ++tokenCount;
        token = std::strtok(NULL, " \t,\r\n");
    }

    return tokenCount;
}

static bool IsHmsToken(const char *text)
{
    return text != NULL && std::strchr(text, ':') != NULL;
}

static bool ParsePositionLine(char *line, PositionRecord *record)
{
    if (line == NULL || record == NULL)
    {
        return false;
    }

    char *tokens[TOKEN_MAX_COUNT];
    const int tokenCount = SplitLine(line, tokens, TOKEN_MAX_COUNT);

    int baseIndex = 0;

    /*
     * No-week layout:
     *   0 UTCSecond, 1 UTCHMS, 2 Latitude, ...
     */
    if (tokenCount >= 14 && IsHmsToken(tokens[1]))
    {
        record->hasWeek = false;
        record->week = 0;
        baseIndex = 0;
    }
    /*
     * Week layout:
     *   0 Week, 1 UTCSecond, 2 UTCHMS, 3 Latitude, ...
     */
    else if (tokenCount >= 15 && IsHmsToken(tokens[2]))
    {
        record->hasWeek = true;
        baseIndex = 1;

        if (!ParseUnsignedIntStrict(tokens[0], &record->week))
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    if (!ParseDoubleStrict(tokens[baseIndex + 0], &record->utcSecond) ||
        !ParseDoubleStrict(tokens[baseIndex + 2], &record->latitude) ||
        !ParseDoubleStrict(tokens[baseIndex + 3], &record->longitude) ||
        !ParseDoubleStrict(tokens[baseIndex + 4], &record->hEll) ||
        !ParseDoubleStrict(tokens[baseIndex + 5], &record->velocityX) ||
        !ParseDoubleStrict(tokens[baseIndex + 6], &record->velocityY) ||
        !ParseDoubleStrict(tokens[baseIndex + 7], &record->velocityZ) ||
        !ParseIntStrict(tokens[baseIndex + 8], &record->sourceQ) ||
        !ParseDoubleStrict(tokens[baseIndex + 11], &record->heading) ||
        !ParseDoubleStrict(tokens[baseIndex + 12], &record->roll) ||
        !ParseDoubleStrict(tokens[baseIndex + 13], &record->pitch))
    {
        return false;
    }

    const int hmsLength = std::snprintf(
        record->utcHms,
        sizeof(record->utcHms),
        "%s",
        tokens[baseIndex + 1]);

    if (hmsLength <= 0 ||
        static_cast<size_t>(hmsLength) >= sizeof(record->utcHms))
    {
        return false;
    }

    /*
     * SDHeight/SDHoriz are available in the shown source format, but keep
     * compatibility with older files that stop at Pitch.
     */
    record->sdHeightCm = 0.0;
    record->sdHorizCm = 0.0;

    if (tokenCount > baseIndex + 15)
    {
        if (!ParseDoubleStrict(tokens[baseIndex + 14],
                               &record->sdHeightCm) ||
            !ParseDoubleStrict(tokens[baseIndex + 15],
                               &record->sdHorizCm))
        {
            return false;
        }
    }

    if (record->latitude < -90.0 || record->latitude > 90.0 ||
        record->longitude < -180.0 || record->longitude > 180.0 ||
        record->sdHeightCm < 0.0 || record->sdHorizCm < 0.0)
    {
        return false;
    }

    return true;
}


static bool CopyText(char *destination,
                     size_t destinationSize,
                     const char *source)
{
    if (destination == NULL || destinationSize == 0 || source == NULL)
    {
        return false;
    }

    const int length = std::snprintf(destination, destinationSize, "%s", source);
    return length >= 0 && static_cast<size_t>(length) < destinationSize;
}

static int SplitCommaSeparated(char *text,
                               char *tokens[],
                               int maxTokenCount)
{
    if (text == NULL || tokens == NULL || maxTokenCount <= 0)
    {
        return 0;
    }

    int count = 0;
    char *fieldStart = text;
    char *cursor = text;

    while (true)
    {
        if (*cursor == ',' || *cursor == '\0')
        {
            if (count >= maxTokenCount)
            {
                return count;
            }

            tokens[count++] = fieldStart;

            if (*cursor == '\0')
            {
                break;
            }

            *cursor = '\0';
            fieldStart = cursor + 1;
        }

        ++cursor;
    }

    return count;
}

static void InitializeInspvaRecord(InspvaRecord *record)
{
    std::memset(record, 0, sizeof(*record));
    record->shortHeader = false;
    CopyText(record->port, sizeof(record->port), NOVATEL_PORT);
    CopyText(record->sequence, sizeof(record->sequence), "0");
    CopyText(record->idleTime, sizeof(record->idleTime), "0.0");
    CopyText(record->timeStatus, sizeof(record->timeStatus), NOVATEL_TIME_STATUS);
    CopyText(record->receiverStatus, sizeof(record->receiverStatus), "00000000");
    CopyText(record->reserved, sizeof(record->reserved), "0000");
    CopyText(record->receiverVersion, sizeof(record->receiverVersion), "0");
}

/*
 * Parse NovAtel ASCII INSPVA logs.
 *
 * Supported forms:
 *   #INSPVAA,...;week,sec,lat,lon,hgt,n,e,u,roll,pitch,azimuth,status*crc
 *   %INSPVASA,week,sec;week,sec,lat,lon,hgt,n,e,u,roll,pitch,azimuth,status*crc
 *
 * Source CRC is intentionally not required for conversion. The generated
 * BESTPOS sentence always receives a newly calculated CRC.
 */
static bool ParseInspvaAsciiLine(char *line, InspvaRecord *record)
{
    if (line == NULL || record == NULL)
    {
        return false;
    }

    char *start = line;

    while (*start == ' ' || *start == '\t')
    {
        ++start;
    }

    const bool isFull = std::strncmp(start, "#INSPVAA,", 9) == 0;
    const bool isShort = std::strncmp(start, "%INSPVASA,", 10) == 0;

    if (!isFull && !isShort)
    {
        return false;
    }

    InitializeInspvaRecord(record);
    record->shortHeader = isShort;

    char *asterisk = std::strchr(start, '*');

    if (asterisk != NULL)
    {
        *asterisk = '\0';
    }
    else
    {
        char *end = start + std::strlen(start);

        while (end > start &&
               (end[-1] == '\r' || end[-1] == '\n' ||
                end[-1] == ' ' || end[-1] == '\t'))
        {
            --end;
        }

        *end = '\0';
    }

    char *semicolon = std::strchr(start, ';');

    if (semicolon == NULL)
    {
        return false;
    }

    *semicolon = '\0';
    char *headerText = start + 1; /* Skip # or %. */
    char *bodyText = semicolon + 1;

    char *headerTokens[16];
    char *bodyTokens[20];

    const int headerCount = SplitCommaSeparated(
        headerText, headerTokens, 16);
    const int bodyCount = SplitCommaSeparated(
        bodyText, bodyTokens, 20);

    if (bodyCount < 12)
    {
        return false;
    }

    if (isFull)
    {
        if (headerCount < 10 || std::strcmp(headerTokens[0], "INSPVAA") != 0)
        {
            return false;
        }

        if (!CopyText(record->port, sizeof(record->port), headerTokens[1]) ||
            !CopyText(record->sequence, sizeof(record->sequence), headerTokens[2]) ||
            !CopyText(record->idleTime, sizeof(record->idleTime), headerTokens[3]) ||
            !CopyText(record->timeStatus, sizeof(record->timeStatus), headerTokens[4]) ||
            !CopyText(record->receiverStatus, sizeof(record->receiverStatus), headerTokens[7]) ||
            !CopyText(record->reserved, sizeof(record->reserved), headerTokens[8]) ||
            !CopyText(record->receiverVersion, sizeof(record->receiverVersion), headerTokens[9]))
        {
            return false;
        }
    }
    else
    {
        if (headerCount < 3 || std::strcmp(headerTokens[0], "INSPVASA") != 0)
        {
            return false;
        }
    }

    if (!ParseUnsignedIntStrict(bodyTokens[0], &record->week) ||
        !ParseDoubleStrict(bodyTokens[1], &record->second) ||
        !ParseDoubleStrict(bodyTokens[2], &record->latitude) ||
        !ParseDoubleStrict(bodyTokens[3], &record->longitude) ||
        !ParseDoubleStrict(bodyTokens[4], &record->ellipsoidalHeight) ||
        !ParseDoubleStrict(bodyTokens[5], &record->northVelocity) ||
        !ParseDoubleStrict(bodyTokens[6], &record->eastVelocity) ||
        !ParseDoubleStrict(bodyTokens[7], &record->upVelocity) ||
        !ParseDoubleStrict(bodyTokens[8], &record->roll) ||
        !ParseDoubleStrict(bodyTokens[9], &record->pitch) ||
        !ParseDoubleStrict(bodyTokens[10], &record->azimuth) ||
        !CopyText(record->insStatus, sizeof(record->insStatus), bodyTokens[11]))
    {
        return false;
    }

    if (record->latitude < -90.0 || record->latitude > 90.0 ||
        record->longitude < -180.0 || record->longitude > 180.0 ||
        record->second < 0.0 || record->second >= GNSS_WEEK_SECONDS)
    {
        return false;
    }

    return true;
}


#define INSPVAX_MAX_HEADER_FIELDS 32
#define INSPVAX_MAX_BODY_FIELDS   64
#define INSPVAX_MAX_DIAGNOSTICS   10U
#define DYNAMIC_LINE_INITIAL_SIZE 4096U

struct FieldSpan
{
    const char *ptr;
    size_t len;
};

enum InspvaxParseError
{
    INSPVAX_PARSE_OK = 0,
    INSPVAX_PARSE_NO_SEMICOLON,
    INSPVAX_PARSE_NO_ASTERISK,
    INSPVAX_PARSE_HEADER_FIELDS,
    INSPVAX_PARSE_BODY_FIELDS,
    INSPVAX_PARSE_MESSAGE_NAME,
    INSPVAX_PARSE_WEEK,
    INSPVAX_PARSE_SECOND,
    INSPVAX_PARSE_REQUIRED_FIELD,
    INSPVAX_PARSE_RANGE
};

static const char *InspvaxParseErrorText(InspvaxParseError error)
{
    switch (error)
    {
    case INSPVAX_PARSE_NO_SEMICOLON:
        return "missing ';'";
    case INSPVAX_PARSE_NO_ASTERISK:
        return "missing '*'";
    case INSPVAX_PARSE_HEADER_FIELDS:
        return "INSPVAX header has fewer than 7 fields or too many fields";
    case INSPVAX_PARSE_BODY_FIELDS:
        return "INSPVAX data area has fewer than 23 fields or too many fields";
    case INSPVAX_PARSE_MESSAGE_NAME:
        return "message name is not INSPVAXA";
    case INSPVAX_PARSE_WEEK:
        return "invalid GNSS week";
    case INSPVAX_PARSE_SECOND:
        return "invalid GNSS seconds";
    case INSPVAX_PARSE_REQUIRED_FIELD:
        return "invalid required INSPVAX field";
    case INSPVAX_PARSE_RANGE:
        return "latitude/longitude/time is outside valid range";
    default:
        return "unknown";
    }
}

static bool IsAsciiSpace(unsigned char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
           ch == '\v' || ch == '\f';
}

static void TrimFieldSpan(FieldSpan *field)
{
    if (field == NULL)
    {
        return;
    }

    while (field->len > 0U &&
           IsAsciiSpace(static_cast<unsigned char>(field->ptr[0])))
    {
        ++field->ptr;
        --field->len;
    }

    while (field->len > 0U &&
           IsAsciiSpace(static_cast<unsigned char>(
               field->ptr[field->len - 1U])))
    {
        --field->len;
    }
}

static int SplitSpanFields(const char *begin,
                           const char *end,
                           FieldSpan fields[],
                           int maxFields)
{
    if (begin == NULL || end == NULL || fields == NULL ||
        maxFields <= 0 || begin > end)
    {
        return -1;
    }

    int count = 0;
    const char *fieldBegin = begin;

    for (const char *cursor = begin; cursor <= end; ++cursor)
    {
        if (cursor == end || *cursor == ',')
        {
            if (count >= maxFields)
            {
                return -1;
            }

            fields[count].ptr = fieldBegin;
            fields[count].len =
                static_cast<size_t>(cursor - fieldBegin);
            TrimFieldSpan(&fields[count]);
            ++count;
            fieldBegin = cursor + 1;
        }
    }

    return count;
}

static bool FieldSpanEquals(const FieldSpan *field,
                            const char *text)
{
    if (field == NULL || text == NULL)
    {
        return false;
    }

    const size_t textLength = std::strlen(text);

    return field->len == textLength &&
           std::memcmp(field->ptr, text, textLength) == 0;
}

static bool CopyFieldSpan(char *destination,
                          size_t destinationSize,
                          const FieldSpan *field)
{
    if (destination == NULL || destinationSize == 0 ||
        field == NULL || field->len >= destinationSize)
    {
        return false;
    }

    if (field->len > 0U)
    {
        std::memcpy(destination, field->ptr, field->len);
    }

    destination[field->len] = '\0';
    return true;
}

static bool FieldSpanToDouble(const FieldSpan *field,
                              double *value)
{
    if (field == NULL || value == NULL ||
        field->len == 0U || field->len >= 128U)
    {
        return false;
    }

    char text[128];
    std::memcpy(text, field->ptr, field->len);
    text[field->len] = '\0';

    return ParseDoubleStrict(text, value);
}

static bool FieldSpanToUnsignedInt(const FieldSpan *field,
                                   unsigned int *value)
{
    if (field == NULL || value == NULL ||
        field->len == 0U || field->len >= 64U)
    {
        return false;
    }

    char text[64];
    std::memcpy(text, field->ptr, field->len);
    text[field->len] = '\0';

    return ParseUnsignedIntStrict(text, value);
}

static void InitializeInspvaxRecord(InspvaxRecord *record)
{
    if (record == NULL)
    {
        return;
    }

    std::memset(record, 0, sizeof(*record));
    CopyText(record->port, sizeof(record->port), NOVATEL_PORT);
    CopyText(record->sequence, sizeof(record->sequence), "0");
    CopyText(record->idleTime, sizeof(record->idleTime), "0.0");
    CopyText(record->timeStatus, sizeof(record->timeStatus),
             NOVATEL_TIME_STATUS);
    CopyText(record->receiverStatus, sizeof(record->receiverStatus),
             "00000000");
    CopyText(record->reserved, sizeof(record->reserved), "0000");
    CopyText(record->receiverVersion, sizeof(record->receiverVersion), "0");
    CopyText(record->extSolStat, sizeof(record->extSolStat), "00000000");
}

/*
 * Parse a full NovAtel ASCII INSPVAXA record.
 *
 * Return values:
 *   1  valid INSPVAXA
 *   0  not an INSPVAXA record
 *  -1  looks like INSPVAXA but is malformed
 *
 * The input CRC is deliberately ignored. '*' is only treated as the end
 * delimiter of the INSPVAX data area. This preserves the behavior of the
 * previous inspvax_to_inspva_v3 tool.
 */
static int ParseInspvaxAsciiLine(const char *line,
                                 size_t lineLength,
                                 InspvaxRecord *record,
                                 InspvaxParseError *error)
{
    static const char prefix[] = "#INSPVAXA,";

    if (record == NULL || error == NULL)
    {
        return -1;
    }

    *error = INSPVAX_PARSE_OK;

    if (line == NULL || lineLength == 0U)
    {
        return 0;
    }

    const char *start = line;
    const char *lineEnd = line + lineLength;

    while (start < lineEnd &&
           (*start == ' ' || *start == '\t'))
    {
        ++start;
    }

    if (static_cast<size_t>(lineEnd - start) <
            sizeof(prefix) - 1U ||
        std::memcmp(start, prefix, sizeof(prefix) - 1U) != 0)
    {
        return 0;
    }

    const char *recordEnd = lineEnd;

    while (recordEnd > start &&
           (recordEnd[-1] == '\r' || recordEnd[-1] == '\n'))
    {
        --recordEnd;
    }

    const char *semicolon = static_cast<const char *>(
        std::memchr(start, ';',
                    static_cast<size_t>(recordEnd - start)));

    if (semicolon == NULL)
    {
        *error = INSPVAX_PARSE_NO_SEMICOLON;
        return -1;
    }

    const char *asterisk = static_cast<const char *>(
        std::memchr(semicolon + 1, '*',
                    static_cast<size_t>(
                        recordEnd - (semicolon + 1))));

    if (asterisk == NULL)
    {
        *error = INSPVAX_PARSE_NO_ASTERISK;
        return -1;
    }

    FieldSpan headerFields[INSPVAX_MAX_HEADER_FIELDS];
    FieldSpan bodyFields[INSPVAX_MAX_BODY_FIELDS];

    const int headerCount = SplitSpanFields(
        start + 1, semicolon,
        headerFields, INSPVAX_MAX_HEADER_FIELDS);

    const int bodyCount = SplitSpanFields(
        semicolon + 1, asterisk,
        bodyFields, INSPVAX_MAX_BODY_FIELDS);

    if (headerCount < 7)
    {
        *error = INSPVAX_PARSE_HEADER_FIELDS;
        return -1;
    }

    if (bodyCount < 23)
    {
        *error = INSPVAX_PARSE_BODY_FIELDS;
        return -1;
    }

    if (!FieldSpanEquals(&headerFields[0], "INSPVAXA"))
    {
        *error = INSPVAX_PARSE_MESSAGE_NAME;
        return -1;
    }

    InitializeInspvaxRecord(record);

    if (!FieldSpanToUnsignedInt(&headerFields[5], &record->week))
    {
        *error = INSPVAX_PARSE_WEEK;
        return -1;
    }

    if (!FieldSpanToDouble(&headerFields[6], &record->second))
    {
        *error = INSPVAX_PARSE_SECOND;
        return -1;
    }

    if (headerCount > 1 &&
        !CopyFieldSpan(record->port, sizeof(record->port),
                       &headerFields[1]))
    {
        *error = INSPVAX_PARSE_REQUIRED_FIELD;
        return -1;
    }

    if (headerCount > 2 &&
        !CopyFieldSpan(record->sequence, sizeof(record->sequence),
                       &headerFields[2]))
    {
        *error = INSPVAX_PARSE_REQUIRED_FIELD;
        return -1;
    }

    if (headerCount > 3 &&
        !CopyFieldSpan(record->idleTime, sizeof(record->idleTime),
                       &headerFields[3]))
    {
        *error = INSPVAX_PARSE_REQUIRED_FIELD;
        return -1;
    }

    if (headerCount > 4 &&
        !CopyFieldSpan(record->timeStatus, sizeof(record->timeStatus),
                       &headerFields[4]))
    {
        *error = INSPVAX_PARSE_REQUIRED_FIELD;
        return -1;
    }

    if (headerCount > 7 &&
        !CopyFieldSpan(record->receiverStatus,
                       sizeof(record->receiverStatus),
                       &headerFields[7]))
    {
        *error = INSPVAX_PARSE_REQUIRED_FIELD;
        return -1;
    }

    if (headerCount > 8 &&
        !CopyFieldSpan(record->reserved, sizeof(record->reserved),
                       &headerFields[8]))
    {
        *error = INSPVAX_PARSE_REQUIRED_FIELD;
        return -1;
    }

    if (headerCount > 9 &&
        !CopyFieldSpan(record->receiverVersion,
                       sizeof(record->receiverVersion),
                       &headerFields[9]))
    {
        *error = INSPVAX_PARSE_REQUIRED_FIELD;
        return -1;
    }

    if (!CopyFieldSpan(record->insStatus,
                       sizeof(record->insStatus),
                       &bodyFields[0]) ||
        !CopyFieldSpan(record->posType,
                       sizeof(record->posType),
                       &bodyFields[1]) ||
        !FieldSpanToDouble(&bodyFields[2], &record->latitude) ||
        !FieldSpanToDouble(&bodyFields[3], &record->longitude) ||
        !FieldSpanToDouble(&bodyFields[4], &record->mslHeight) ||
        !FieldSpanToDouble(&bodyFields[5], &record->undulation) ||
        !FieldSpanToDouble(&bodyFields[6], &record->northVelocity) ||
        !FieldSpanToDouble(&bodyFields[7], &record->eastVelocity) ||
        !FieldSpanToDouble(&bodyFields[8], &record->upVelocity) ||
        !FieldSpanToDouble(&bodyFields[9], &record->roll) ||
        !FieldSpanToDouble(&bodyFields[10], &record->pitch) ||
        !FieldSpanToDouble(&bodyFields[11], &record->azimuth) ||
        !FieldSpanToDouble(&bodyFields[12], &record->latSigma) ||
        !FieldSpanToDouble(&bodyFields[13], &record->lonSigma) ||
        !FieldSpanToDouble(&bodyFields[14], &record->heightSigma) ||
        !CopyFieldSpan(record->extSolStat,
                       sizeof(record->extSolStat),
                       &bodyFields[21]))
    {
        *error = INSPVAX_PARSE_REQUIRED_FIELD;
        return -1;
    }

    if (record->latitude < -90.0 || record->latitude > 90.0 ||
        record->longitude < -180.0 || record->longitude > 180.0 ||
        record->second < 0.0 ||
        record->second >= GNSS_WEEK_SECONDS)
    {
        *error = INSPVAX_PARSE_RANGE;
        return -1;
    }

    return 1;
}

/*
 * Dynamic line reader used by INSPVAX modes. INSPVAX records can be longer
 * than the fixed line buffer used by the position-result parser.
 *
 * Return: 1 = record read, 0 = EOF, -1 = allocation/read error.
 */
static int ReadDynamicRecord(FILE *file,
                             char **buffer,
                             size_t *capacity,
                             size_t *length)
{
    if (file == NULL || buffer == NULL ||
        capacity == NULL || length == NULL)
    {
        return -1;
    }

    *length = 0U;

    if (*buffer == NULL || *capacity == 0U)
    {
        *capacity = DYNAMIC_LINE_INITIAL_SIZE;
        *buffer = static_cast<char *>(std::malloc(*capacity));

        if (*buffer == NULL)
        {
            return -1;
        }
    }

    int ch = EOF;

    while ((ch = std::fgetc(file)) != EOF)
    {
        if (*length + 1U >= *capacity)
        {
            if (*capacity > static_cast<size_t>(-1) / 2U)
            {
                return -1;
            }

            const size_t newCapacity = *capacity * 2U;
            char *newBuffer = static_cast<char *>(
                std::realloc(*buffer, newCapacity));

            if (newBuffer == NULL)
            {
                return -1;
            }

            *buffer = newBuffer;
            *capacity = newCapacity;
        }

        (*buffer)[(*length)++] = static_cast<char>(ch);

        if (ch == '\n')
        {
            break;
        }
    }

    if (std::ferror(file))
    {
        return -1;
    }

    if (*length == 0U && ch == EOF)
    {
        return 0;
    }

    (*buffer)[*length] = '\0';
    return 1;
}

static void PrintInspvaxPreview(const char *line,
                                size_t lineLength)
{
    if (line == NULL)
    {
        return;
    }

    const size_t maxLength = 240U;
    const size_t shown =
        lineLength < maxLength ? lineLength : maxLength;

    for (size_t i = 0U; i < shown; ++i)
    {
        const unsigned char ch =
            static_cast<unsigned char>(line[i]);

        if (ch == '\r')
        {
            std::fputs("\\r", stderr);
        }
        else if (ch == '\n')
        {
            std::fputs("\\n", stderr);
        }
        else if (ch >= 32U && ch <= 126U)
        {
            std::fputc(static_cast<int>(ch), stderr);
        }
        else
        {
            std::fprintf(stderr, "\\x%02X",
                         static_cast<unsigned int>(ch));
        }
    }

    if (shown < lineLength)
    {
        std::fputs("...", stderr);
    }

    std::fputc('\n', stderr);
}



#define RATE_DETECT_MAX_INTERVALS 1024
#define RATE_DETECT_MAX_VALID_RECORDS 4096
#define RATE_DETECT_MAX_HZ 10000U

struct RateDetectionResult
{
    unsigned int inputHz;
    double nominalInterval;
    unsigned int intervalCount;
    unsigned int singleStepCount;
    unsigned int consistentCount;
};

static int CompareDoubleAscending(const void *left, const void *right)
{
    const double leftValue = *static_cast<const double *>(left);
    const double rightValue = *static_cast<const double *>(right);

    if (leftValue < rightValue)
    {
        return -1;
    }

    if (leftValue > rightValue)
    {
        return 1;
    }

    return 0;
}

/*
 * Return the positive elapsed UTC time between two records.
 *
 * When both records contain a week, the week difference is used directly.
 * When the week is absent, a large negative jump is treated as week rollover.
 * Small negative/zero differences are rejected as duplicate or out-of-order
 * timestamps and are not used for rate detection.
 */
static bool GetRecordTimeDifference(const PositionRecord *previous,
                                    const PositionRecord *current,
                                    double *difference)
{
    if (previous == NULL || current == NULL || difference == NULL)
    {
        return false;
    }

    double delta = current->utcSecond - previous->utcSecond;

    if (previous->hasWeek && current->hasWeek)
    {
        const int64_t weekDifference =
            static_cast<int64_t>(current->week) -
            static_cast<int64_t>(previous->week);

        delta += static_cast<double>(weekDifference) *
                 GNSS_WEEK_SECONDS;
    }
    else if (delta < -GNSS_WEEK_SECONDS * 0.5)
    {
        delta += GNSS_WEEK_SECONDS;
    }

    if (!std::isfinite(delta) || delta <= 0.0)
    {
        return false;
    }

    *difference = delta;
    return true;
}

/*
 * Detect the nominal integer input frequency from timestamps near the start
 * of the file. Only a small prefix is scanned, then the file is rewound.
 *
 * The 20th-percentile interval is used instead of the minimum so that a
 * single abnormally short interval cannot make the detected rate too high.
 * Missing epochs are accepted as integer multiples of the nominal period.
 */
static bool DetectInputRate(FILE *inputFile,
                            RateDetectionResult *result)
{
    if (inputFile == NULL || result == NULL)
    {
        return false;
    }

    std::rewind(inputFile);

    double intervals[RATE_DETECT_MAX_INTERVALS];
    unsigned int intervalCount = 0;
    unsigned int validRecordCount = 0;

    bool hasPrevious = false;
    PositionRecord previousRecord;
    char line[LINE_BUFFER_SIZE];

    while (intervalCount < RATE_DETECT_MAX_INTERVALS &&
           validRecordCount < RATE_DETECT_MAX_VALID_RECORDS &&
           std::fgets(line, sizeof(line), inputFile) != NULL)
    {
        PositionRecord currentRecord;

        if (!ParsePositionLine(line, &currentRecord))
        {
            continue;
        }

        ++validRecordCount;

        if (hasPrevious)
        {
            double delta = 0.0;

            if (GetRecordTimeDifference(&previousRecord,
                                        &currentRecord,
                                        &delta))
            {
                /*
                 * Very large gaps are not useful for detecting a normal
                 * positioning output rate. Frequencies below 0.1 Hz are not
                 * supported by the integer-Hz --rate interface.
                 */
                if (delta <= 10.0)
                {
                    intervals[intervalCount] = delta;
                    ++intervalCount;
                }
            }
        }

        previousRecord = currentRecord;
        hasPrevious = true;
    }

    std::rewind(inputFile);

    if (intervalCount < 5)
    {
        std::printf(
            "Unable to auto-detect input rate: fewer than 5 valid time "
            "intervals were found near the start of the file.\n");
        return false;
    }

    std::qsort(intervals,
               intervalCount,
               sizeof(intervals[0]),
               CompareDoubleAscending);

    const unsigned int percentileIndex =
        (intervalCount - 1U) / 5U;
    const double representativeInterval =
        intervals[percentileIndex];

    if (!std::isfinite(representativeInterval) ||
        representativeInterval <= 0.0)
    {
        std::printf(
            "Unable to auto-detect input rate: invalid timestamp "
            "intervals.\n");
        return false;
    }

    const double rawFrequency = 1.0 / representativeInterval;
    const unsigned int detectedHz = static_cast<unsigned int>(
        std::floor(rawFrequency + 0.5));

    if (detectedHz == 0 || detectedHz > RATE_DETECT_MAX_HZ)
    {
        std::printf(
            "Unable to auto-detect input rate: estimated frequency %.6f "
            "Hz is outside 1 to %u Hz.\n",
            rawFrequency,
            RATE_DETECT_MAX_HZ);
        return false;
    }

    const double nominalInterval =
        1.0 / static_cast<double>(detectedHz);

    /*
     * The representative interval itself must agree with an integer-Hz rate.
     * A 2%% relative tolerance permits normal text rounding/jitter without
     * silently accepting a clearly non-integer source rate.
     */
    const double representativeError =
        std::fabs(representativeInterval - nominalInterval);
    const double representativeTolerance =
        nominalInterval * 0.02 + 1e-9;

    if (representativeError > representativeTolerance)
    {
        std::printf(
            "Unable to auto-detect an integer input rate: representative "
            "interval %.9f s corresponds to %.6f Hz.\n",
            representativeInterval,
            rawFrequency);
        return false;
    }

    unsigned int singleStepCount = 0;
    unsigned int consistentCount = 0;

    for (unsigned int i = 0; i < intervalCount; ++i)
    {
        const double interval = intervals[i];
        const double stepValue =
            interval / nominalInterval;
        const unsigned int stepCount = static_cast<unsigned int>(
            std::floor(stepValue + 0.5));

        if (stepCount == 0)
        {
            continue;
        }

        const double expectedInterval =
            static_cast<double>(stepCount) * nominalInterval;
        const double tolerance =
            nominalInterval * 0.05 + 1e-8;

        if (std::fabs(interval - expectedInterval) <= tolerance)
        {
            ++consistentCount;

            if (stepCount == 1)
            {
                ++singleStepCount;
            }
        }
    }

    /*
     * Require most intervals to fit the detected time grid and enough direct
     * one-period intervals to distinguish the real file rate from an
     * arbitrary higher multiple.
     */
    if (consistentCount * 100U < intervalCount * 80U ||
        singleStepCount * 100U < intervalCount * 20U)
    {
        std::printf(
            "Unable to reliably auto-detect input rate: detected %u Hz, "
            "but timestamps are too irregular (%u/%u consistent, "
            "%u single-step).\n",
            detectedHz,
            consistentCount,
            intervalCount,
            singleStepCount);
        return false;
    }

    result->inputHz = detectedHz;
    result->nominalInterval = nominalInterval;
    result->intervalCount = intervalCount;
    result->singleStepCount = singleStepCount;
    result->consistentCount = consistentCount;
    return true;
}


static bool GetInspvaTimeDifference(const InspvaRecord *previous,
                                    const InspvaRecord *current,
                                    double *difference)
{
    if (previous == NULL || current == NULL || difference == NULL)
    {
        return false;
    }

    const int64_t weekDifference =
        static_cast<int64_t>(current->week) -
        static_cast<int64_t>(previous->week);

    const double delta =
        current->second - previous->second +
        static_cast<double>(weekDifference) * GNSS_WEEK_SECONDS;

    if (!std::isfinite(delta) || delta <= 0.0)
    {
        return false;
    }

    *difference = delta;
    return true;
}

static bool DetectInspvaInputRate(FILE *inputFile,
                                  RateDetectionResult *result)
{
    if (inputFile == NULL || result == NULL)
    {
        return false;
    }

    std::rewind(inputFile);

    double intervals[RATE_DETECT_MAX_INTERVALS];
    unsigned int intervalCount = 0;
    unsigned int validRecordCount = 0;

    bool hasPrevious = false;
    InspvaRecord previousRecord;
    char line[LINE_BUFFER_SIZE];

    while (intervalCount < RATE_DETECT_MAX_INTERVALS &&
           validRecordCount < RATE_DETECT_MAX_VALID_RECORDS &&
           std::fgets(line, sizeof(line), inputFile) != NULL)
    {
        InspvaRecord currentRecord;

        if (!ParseInspvaAsciiLine(line, &currentRecord))
        {
            continue;
        }

        ++validRecordCount;

        if (hasPrevious)
        {
            double delta = 0.0;

            if (GetInspvaTimeDifference(&previousRecord,
                                        &currentRecord,
                                        &delta) &&
                delta <= 10.0)
            {
                intervals[intervalCount++] = delta;
            }
        }

        previousRecord = currentRecord;
        hasPrevious = true;
    }

    std::rewind(inputFile);

    if (intervalCount < 5)
    {
        std::printf(
            "Unable to auto-detect INSPVA input rate: fewer than 5 valid time intervals were found.\n");
        return false;
    }

    std::qsort(intervals,
               intervalCount,
               sizeof(intervals[0]),
               CompareDoubleAscending);

    const unsigned int percentileIndex = (intervalCount - 1U) / 5U;
    const double representativeInterval = intervals[percentileIndex];

    if (!std::isfinite(representativeInterval) || representativeInterval <= 0.0)
    {
        std::printf("Unable to auto-detect INSPVA input rate: invalid intervals.\n");
        return false;
    }

    const double rawFrequency = 1.0 / representativeInterval;
    const unsigned int detectedHz = static_cast<unsigned int>(
        std::floor(rawFrequency + 0.5));

    if (detectedHz == 0 || detectedHz > RATE_DETECT_MAX_HZ)
    {
        std::printf(
            "Unable to auto-detect INSPVA input rate: estimated frequency %.6f Hz is outside 1 to %u Hz.\n",
            rawFrequency,
            RATE_DETECT_MAX_HZ);
        return false;
    }

    const double nominalInterval = 1.0 / static_cast<double>(detectedHz);
    const double representativeTolerance = nominalInterval * 0.02 + 1e-9;

    if (std::fabs(representativeInterval - nominalInterval) > representativeTolerance)
    {
        std::printf(
            "Unable to auto-detect an integer INSPVA input rate: representative interval %.9f s corresponds to %.6f Hz.\n",
            representativeInterval,
            rawFrequency);
        return false;
    }

    unsigned int singleStepCount = 0;
    unsigned int consistentCount = 0;

    for (unsigned int i = 0; i < intervalCount; ++i)
    {
        const double stepValue = intervals[i] / nominalInterval;
        const unsigned int stepCount = static_cast<unsigned int>(
            std::floor(stepValue + 0.5));

        if (stepCount == 0)
        {
            continue;
        }

        const double expectedInterval =
            static_cast<double>(stepCount) * nominalInterval;
        const double tolerance = nominalInterval * 0.05 + 1e-8;

        if (std::fabs(intervals[i] - expectedInterval) <= tolerance)
        {
            ++consistentCount;

            if (stepCount == 1)
            {
                ++singleStepCount;
            }
        }
    }

    if (consistentCount * 100U < intervalCount * 80U ||
        singleStepCount * 100U < intervalCount * 20U)
    {
        std::printf(
            "Unable to reliably auto-detect INSPVA input rate: detected %u Hz, but timestamps are too irregular (%u/%u consistent, %u single-step).\n",
            detectedHz,
            consistentCount,
            intervalCount,
            singleStepCount);
        return false;
    }

    result->inputHz = detectedHz;
    result->nominalInterval = nominalInterval;
    result->intervalCount = intervalCount;
    result->singleStepCount = singleStepCount;
    result->consistentCount = consistentCount;
    return true;
}


static bool GetInspvaxTimeDifference(const InspvaxRecord *previous,
                                     const InspvaxRecord *current,
                                     double *difference)
{
    if (previous == NULL || current == NULL || difference == NULL)
    {
        return false;
    }

    const int64_t weekDifference =
        static_cast<int64_t>(current->week) -
        static_cast<int64_t>(previous->week);

    const double delta =
        current->second - previous->second +
        static_cast<double>(weekDifference) * GNSS_WEEK_SECONDS;

    if (!std::isfinite(delta) || delta <= 0.0)
    {
        return false;
    }

    *difference = delta;
    return true;
}

static bool DetectInspvaxInputRate(FILE *inputFile,
                                   RateDetectionResult *result)
{
    if (inputFile == NULL || result == NULL)
    {
        return false;
    }

    std::rewind(inputFile);

    double intervals[RATE_DETECT_MAX_INTERVALS];
    unsigned int intervalCount = 0;
    unsigned int validRecordCount = 0;

    bool hasPrevious = false;
    InspvaxRecord previousRecord;

    char *line = NULL;
    size_t lineCapacity = 0U;
    size_t lineLength = 0U;
    int readResult = 0;

    while (intervalCount < RATE_DETECT_MAX_INTERVALS &&
           validRecordCount < RATE_DETECT_MAX_VALID_RECORDS &&
           (readResult = ReadDynamicRecord(
                inputFile,
                &line,
                &lineCapacity,
                &lineLength)) == 1)
    {
        InspvaxRecord currentRecord;
        InspvaxParseError parseError = INSPVAX_PARSE_OK;

        if (ParseInspvaxAsciiLine(
                line,
                lineLength,
                &currentRecord,
                &parseError) != 1)
        {
            continue;
        }

        ++validRecordCount;

        if (hasPrevious)
        {
            double delta = 0.0;

            if (GetInspvaxTimeDifference(
                    &previousRecord,
                    &currentRecord,
                    &delta) &&
                delta <= 10.0)
            {
                intervals[intervalCount++] = delta;
            }
        }

        previousRecord = currentRecord;
        hasPrevious = true;
    }

    std::free(line);
    std::rewind(inputFile);

    if (readResult < 0)
    {
        std::printf(
            "Unable to auto-detect INSPVAX input rate: read/allocation error.\n");
        return false;
    }

    if (intervalCount < 5)
    {
        std::printf(
            "Unable to auto-detect INSPVAX input rate: fewer than 5 valid time intervals were found.\n");
        return false;
    }

    std::qsort(intervals,
               intervalCount,
               sizeof(intervals[0]),
               CompareDoubleAscending);

    const unsigned int percentileIndex =
        (intervalCount - 1U) / 5U;
    const double representativeInterval =
        intervals[percentileIndex];

    if (!std::isfinite(representativeInterval) ||
        representativeInterval <= 0.0)
    {
        std::printf(
            "Unable to auto-detect INSPVAX input rate: invalid intervals.\n");
        return false;
    }

    const double rawFrequency =
        1.0 / representativeInterval;

    const unsigned int detectedHz =
        static_cast<unsigned int>(
            std::floor(rawFrequency + 0.5));

    if (detectedHz == 0U ||
        detectedHz > RATE_DETECT_MAX_HZ)
    {
        std::printf(
            "Unable to auto-detect INSPVAX input rate: estimated frequency %.6f Hz is outside 1 to %u Hz.\n",
            rawFrequency,
            RATE_DETECT_MAX_HZ);
        return false;
    }

    const double nominalInterval =
        1.0 / static_cast<double>(detectedHz);

    const double representativeTolerance =
        nominalInterval * 0.02 + 1e-9;

    if (std::fabs(
            representativeInterval - nominalInterval) >
        representativeTolerance)
    {
        std::printf(
            "Unable to auto-detect an integer INSPVAX input rate: representative interval %.9f s corresponds to %.6f Hz.\n",
            representativeInterval,
            rawFrequency);
        return false;
    }

    unsigned int singleStepCount = 0;
    unsigned int consistentCount = 0;

    for (unsigned int i = 0U; i < intervalCount; ++i)
    {
        const double stepValue =
            intervals[i] / nominalInterval;

        const unsigned int stepCount =
            static_cast<unsigned int>(
                std::floor(stepValue + 0.5));

        if (stepCount == 0U)
        {
            continue;
        }

        const double expectedInterval =
            static_cast<double>(stepCount) *
            nominalInterval;

        const double tolerance =
            nominalInterval * 0.05 + 1e-8;

        if (std::fabs(
                intervals[i] - expectedInterval) <=
            tolerance)
        {
            ++consistentCount;

            if (stepCount == 1U)
            {
                ++singleStepCount;
            }
        }
    }

    if (consistentCount * 100U < intervalCount * 80U ||
        singleStepCount * 100U < intervalCount * 20U)
    {
        std::printf(
            "Unable to reliably auto-detect INSPVAX input rate: detected %u Hz, but timestamps are too irregular (%u/%u consistent, %u single-step).\n",
            detectedHz,
            consistentCount,
            intervalCount,
            singleStepCount);
        return false;
    }

    result->inputHz = detectedHz;
    result->nominalInterval = nominalInterval;
    result->intervalCount = intervalCount;
    result->singleStepCount = singleStepCount;
    result->consistentCount = consistentCount;

    return true;
}

static bool FormatUtcFromSecondOfDay(double secondOfDay,
                                     char *output,
                                     size_t outputSize)
{
    if (output == NULL || outputSize == 0 ||
        !std::isfinite(secondOfDay))
    {
        return false;
    }

    secondOfDay = std::fmod(secondOfDay, 86400.0);

    if (secondOfDay < 0.0)
    {
        secondOfDay += 86400.0;
    }

    long long centisecond = static_cast<long long>(
        std::floor(secondOfDay * 100.0 + 0.5));

    const long long centisecondPerDay = 86400LL * 100LL;
    centisecond %= centisecondPerDay;

    const int hour = static_cast<int>(
        centisecond / (3600LL * 100LL));
    centisecond %= 3600LL * 100LL;

    const int minute = static_cast<int>(
        centisecond / (60LL * 100LL));
    centisecond %= 60LL * 100LL;

    const int second = static_cast<int>(centisecond / 100LL);
    const int fraction = static_cast<int>(centisecond % 100LL);

    const int length = std::snprintf(
        output,
        outputSize,
        "%02d%02d%02d.%02d",
        hour,
        minute,
        second,
        fraction);

    return length > 0 && static_cast<size_t>(length) < outputSize;
}

static bool FormatUtcTime(const char *utcHms,
                          double utcSecond,
                          char *output,
                          size_t outputSize)
{
    if (utcHms != NULL)
    {
        int hour = 0;
        int minute = 0;
        double second = 0.0;
        char extra = '\0';

        const int fieldCount = std::sscanf(
            utcHms,
            "%d:%d:%lf%c",
            &hour,
            &minute,
            &second,
            &extra);

        if (fieldCount == 3 &&
            hour >= 0 && hour <= 23 &&
            minute >= 0 && minute <= 59 &&
            second >= 0.0 && second < 60.0)
        {
            const double secondOfDay =
                static_cast<double>(hour) * 3600.0 +
                static_cast<double>(minute) * 60.0 +
                second;

            return FormatUtcFromSecondOfDay(
                secondOfDay,
                output,
                outputSize);
        }
    }

    return FormatUtcFromSecondOfDay(
        utcSecond,
        output,
        outputSize);
}

static bool FormatLatitude(double latitude,
                           char *output,
                           size_t outputSize,
                           char *hemisphere)
{
    if (output == NULL || hemisphere == NULL ||
        !std::isfinite(latitude) ||
        latitude < -90.0 || latitude > 90.0)
    {
        return false;
    }

    *hemisphere = latitude >= 0.0 ? 'N' : 'S';

    const double absoluteValue = std::fabs(latitude);
    const int degree = static_cast<int>(absoluteValue);
    const double minute =
        (absoluteValue - static_cast<double>(degree)) * 60.0;

    const int length = std::snprintf(
        output,
        outputSize,
        "%02d%011.8f",
        degree,
        minute);

    return length > 0 && static_cast<size_t>(length) < outputSize;
}

static bool FormatLongitude(double longitude,
                            char *output,
                            size_t outputSize,
                            char *hemisphere)
{
    if (output == NULL || hemisphere == NULL ||
        !std::isfinite(longitude) ||
        longitude < -180.0 || longitude > 180.0)
    {
        return false;
    }

    *hemisphere = longitude >= 0.0 ? 'E' : 'W';

    const double absoluteValue = std::fabs(longitude);
    const int degree = static_cast<int>(absoluteValue);
    const double minute =
        (absoluteValue - static_cast<double>(degree)) * 60.0;

    const int length = std::snprintf(
        output,
        outputSize,
        "%03d%011.8f",
        degree,
        minute);

    return length > 0 && static_cast<size_t>(length) < outputSize;
}

static unsigned char CalculateNmeaChecksum(const char *body)
{
    unsigned char checksum = 0;

    if (body == NULL)
    {
        return checksum;
    }

    while (*body != '\0')
    {
        checksum ^= static_cast<unsigned char>(*body);
        ++body;
    }

    return checksum;
}

static uint32_t NovatelCrc32Value(int value)
{
    uint32_t crc = static_cast<uint32_t>(value);

    for (int bit = 0; bit < 8; ++bit)
    {
        if ((crc & 1U) != 0U)
        {
            crc = (crc >> 1U) ^ 0xEDB88320U;
        }
        else
        {
            crc >>= 1U;
        }
    }

    return crc;
}

static uint32_t CalculateNovatelCrc32(const char *data)
{
    uint32_t crc = 0;

    if (data == NULL)
    {
        return crc;
    }

    while (*data != '\0')
    {
        const uint32_t temp1 = (crc >> 8U) & 0x00FFFFFFU;
        const uint32_t temp2 = NovatelCrc32Value(
            static_cast<int>(
                (crc ^ static_cast<unsigned char>(*data)) & 0xFFU));

        crc = temp1 ^ temp2;
        ++data;
    }

    return crc;
}

static int MapGgaFixQuality(int sourceQ)
{
    return sourceQ > 0 ? 1 : 0;
}

static const char *MapSolutionStatus(int sourceQ)
{
    return sourceQ > 0 ? "SOL_COMPUTED" : "INSUFFICIENT_OBS";
}

static const char *MapBestPosType(int sourceQ)
{
    return sourceQ > 0 ? BESTPOS_VALID_POS_TYPE : "NONE";
}

static const char *MapInsStatus(int sourceQ)
{
    return sourceQ > 0 ? "INS_SOLUTION_GOOD" : "INS_INACTIVE";
}

static void UtcToGnssTime(unsigned int utcWeek,
                          double utcSecond,
                          double utcOffset,
                          unsigned int *gnssWeek,
                          double *gnssSecond)
{
    *gnssWeek = utcWeek;
    *gnssSecond = utcSecond + utcOffset;

    while (*gnssSecond >= GNSS_WEEK_SECONDS)
    {
        *gnssSecond -= GNSS_WEEK_SECONDS;
        ++(*gnssWeek);
    }

    while (*gnssSecond < 0.0)
    {
        *gnssSecond += GNSS_WEEK_SECONDS;

        if (*gnssWeek > 0)
        {
            --(*gnssWeek);
        }
    }
}

static bool ResolveGnssTime(const PositionRecord *record,
                            const ProgramOptions *options,
                            unsigned int *gnssWeek,
                            double *gnssSecond)
{
    if (record == NULL || options == NULL ||
        gnssWeek == NULL || gnssSecond == NULL)
    {
        return false;
    }

    unsigned int inputWeek = 0;

    if (record->hasWeek)
    {
        inputWeek = record->week;
    }
    else if (options->defaultWeek != 0)
    {
        inputWeek = options->defaultWeek;
    }
    else
    {
        return false;
    }

    UtcToGnssTime(
        inputWeek,
        record->utcSecond,
        options->utcOffset,
        gnssWeek,
        gnssSecond);

    return true;
}

static void EcefVelocityToNeu(double latitudeDegree,
                              double longitudeDegree,
                              double velocityX,
                              double velocityY,
                              double velocityZ,
                              double *northVelocity,
                              double *eastVelocity,
                              double *upVelocity)
{
    const double latitude = latitudeDegree * DEG_TO_RAD;
    const double longitude = longitudeDegree * DEG_TO_RAD;

    const double sinLatitude = std::sin(latitude);
    const double cosLatitude = std::cos(latitude);
    const double sinLongitude = std::sin(longitude);
    const double cosLongitude = std::cos(longitude);

    *eastVelocity =
        -sinLongitude * velocityX +
        cosLongitude * velocityY;

    *northVelocity =
        -sinLatitude * cosLongitude * velocityX -
        sinLatitude * sinLongitude * velocityY +
        cosLatitude * velocityZ;

    *upVelocity =
        cosLatitude * cosLongitude * velocityX +
        cosLatitude * sinLongitude * velocityY +
        sinLatitude * velocityZ;
}

static bool BuildGgaSentence(const PositionRecord *record,
                             const ProgramOptions *options,
                             char *output,
                             size_t outputSize)
{
    if (record == NULL || options == NULL ||
        output == NULL || outputSize == 0)
    {
        return false;
    }

    char utcText[32];
    char latitudeText[32];
    char longitudeText[32];
    char latitudeHemisphere = 'N';
    char longitudeHemisphere = 'E';

    if (!FormatUtcTime(record->utcHms,
                       record->utcSecond,
                       utcText,
                       sizeof(utcText)) ||
        !FormatLatitude(record->latitude,
                        latitudeText,
                        sizeof(latitudeText),
                        &latitudeHemisphere) ||
        !FormatLongitude(record->longitude,
                         longitudeText,
                         sizeof(longitudeText),
                         &longitudeHemisphere))
    {
        return false;
    }

    int satelliteNumber = options->satelliteNumber;

    if (satelliteNumber > 99)
    {
        satelliteNumber = 99;
    }

    const double mslHeight =
        record->hEll - options->geoidSeparation;

    char body[SENTENCE_BUFFER_SIZE];

    const int bodyLength = std::snprintf(
        body,
        sizeof(body),
        "%s,%s,%s,%c,%s,%c,%d,%02d,%.1f,"
        "%.4f,M,%.4f,M,,",
        GGA_SENTENCE_ID,
        utcText,
        latitudeText,
        latitudeHemisphere,
        longitudeText,
        longitudeHemisphere,
        MapGgaFixQuality(record->sourceQ),
        satelliteNumber,
        options->hdop,
        mslHeight,
        options->geoidSeparation);

    if (bodyLength <= 0 ||
        static_cast<size_t>(bodyLength) >= sizeof(body))
    {
        return false;
    }

    const unsigned char checksum = CalculateNmeaChecksum(body);

    const int outputLength = std::snprintf(
        output,
        outputSize,
        "$%s*%02X\r\n",
        body,
        static_cast<unsigned int>(checksum));

    return outputLength > 0 &&
           static_cast<size_t>(outputLength) < outputSize;
}

static bool BuildInspvaSentence(const PositionRecord *record,
                                const ProgramOptions *options,
                                char *output,
                                size_t outputSize)
{
    if (record == NULL || options == NULL ||
        output == NULL || outputSize == 0)
    {
        return false;
    }

    unsigned int gnssWeek = 0;
    double gnssSecond = 0.0;

    if (!ResolveGnssTime(record,
                         options,
                         &gnssWeek,
                         &gnssSecond))
    {
        return false;
    }

    double northVelocity = 0.0;
    double eastVelocity = 0.0;
    double upVelocity = 0.0;

    EcefVelocityToNeu(
        record->latitude,
        record->longitude,
        record->velocityX,
        record->velocityY,
        record->velocityZ,
        &northVelocity,
        &eastVelocity,
        &upVelocity);

    double azimuth = std::fmod(record->heading, 360.0);

    if (azimuth < 0.0)
    {
        azimuth += 360.0;
    }

    char body[SENTENCE_BUFFER_SIZE];

    const int bodyLength = std::snprintf(
        body,
        sizeof(body),
        "INSPVAA,%s,0,0.0,%s,%u,%.3f,00000000,0000,0;"
        "%u,%.9f,%.11f,%.11f,%.4f,"
        "%.4f,%.4f,%.4f,%.9f,%.9f,%.9f,%s",
        NOVATEL_PORT,
        NOVATEL_TIME_STATUS,
        gnssWeek,
        gnssSecond,
        gnssWeek,
        gnssSecond,
        record->latitude,
        record->longitude,
        record->hEll,
        northVelocity,
        eastVelocity,
        upVelocity,
        record->roll,
        record->pitch,
        azimuth,
        MapInsStatus(record->sourceQ));

    if (bodyLength <= 0 ||
        static_cast<size_t>(bodyLength) >= sizeof(body))
    {
        return false;
    }

    const uint32_t crc = CalculateNovatelCrc32(body);

    const int outputLength = std::snprintf(
        output,
        outputSize,
        "#%s*%08x\r\n",
        body,
        static_cast<unsigned int>(crc));

    return outputLength > 0 &&
           static_cast<size_t>(outputLength) < outputSize;
}

static bool BuildBestPosSentence(const PositionRecord *record,
                                 const ProgramOptions *options,
                                 char *output,
                                 size_t outputSize)
{
    if (record == NULL || options == NULL ||
        output == NULL || outputSize == 0)
    {
        return false;
    }

    unsigned int gnssWeek = 0;
    double gnssSecond = 0.0;

    if (!ResolveGnssTime(record,
                         options,
                         &gnssWeek,
                         &gnssSecond))
    {
        return false;
    }

    const double mslHeight =
        record->hEll - options->geoidSeparation;

    /* Source SD units are cm; BESTPOS sigma units are m. */
    const double horizontalSigma = record->sdHorizCm * 0.01;
    const double heightSigma = record->sdHeightCm * 0.01;

    int satelliteNumber = options->satelliteNumber;

    if (satelliteNumber < 0)
    {
        satelliteNumber = 0;
    }
    else if (satelliteNumber > 255)
    {
        satelliteNumber = 255;
    }

    char body[SENTENCE_BUFFER_SIZE];

    /*
     * BESTPOS data fields after ';':
     * sol stat, pos type, lat, lon, MSL height, undulation, datum,
     * lat sigma, lon sigma, hgt sigma, station id, diff age, sol age,
     * tracked SVs, solution SVs, L1 solution SVs, multi-frequency SVs,
     * reserved, extended status, Galileo/BeiDou mask, GPS/GLONASS mask.
     *
     * The source file has only one satellite-count option, so that value is
     * copied to the four BESTPOS count fields. Unknown status/mask fields are
     * written as zero.
     */
    const int bodyLength = std::snprintf(
        body,
        sizeof(body),
        "BESTPOSA,%s,0,0.0,%s,%u,%.3f,00000000,0000,0;"
        "%s,%s,%.11f,%.11f,%.4f,%.4f,WGS84,"
        "%.4f,%.4f,%.4f,\"\",0.000,0.000,"
        "%d,%d,%d,%d,00,00,00,00",
        NOVATEL_PORT,
        NOVATEL_TIME_STATUS,
        gnssWeek,
        gnssSecond,
        MapSolutionStatus(record->sourceQ),
        MapBestPosType(record->sourceQ),
        record->latitude,
        record->longitude,
        mslHeight,
        options->geoidSeparation,
        horizontalSigma,
        horizontalSigma,
        heightSigma,
        satelliteNumber,
        satelliteNumber,
        satelliteNumber,
        satelliteNumber);

    if (bodyLength <= 0 ||
        static_cast<size_t>(bodyLength) >= sizeof(body))
    {
        return false;
    }

    const uint32_t crc = CalculateNovatelCrc32(body);

    const int outputLength = std::snprintf(
        output,
        outputSize,
        "#%s*%08x\r\n",
        body,
        static_cast<unsigned int>(crc));

    return outputLength > 0 &&
           static_cast<size_t>(outputLength) < outputSize;
}


static bool IsInspvaSolutionGood(const char *status)
{
    return status != NULL && std::strcmp(status, "INS_SOLUTION_GOOD") == 0;
}

static bool BuildBestPosFromInspva(const InspvaRecord *record,
                                   const ProgramOptions *options,
                                   char *output,
                                   size_t outputSize)
{
    if (record == NULL || options == NULL ||
        output == NULL || outputSize == 0)
    {
        return false;
    }

    const bool solutionGood = IsInspvaSolutionGood(record->insStatus);
    const char *solutionStatus =
        solutionGood ? "SOL_COMPUTED" : "INSUFFICIENT_OBS";
    const char *positionType =
        solutionGood ? options->inspvaBestPosType : "NONE";

    const double mslHeight =
        record->ellipsoidalHeight - options->geoidSeparation;

    int satelliteNumber = options->satelliteNumber;

    if (satelliteNumber < 0)
    {
        satelliteNumber = 0;
    }
    else if (satelliteNumber > 255)
    {
        satelliteNumber = 255;
    }

    const char *port = record->port[0] != '\0' ? record->port : NOVATEL_PORT;
    const char *sequence = record->sequence[0] != '\0' ? record->sequence : "0";
    const char *idleTime = record->idleTime[0] != '\0' ? record->idleTime : "0.0";
    const char *timeStatus = record->timeStatus[0] != '\0' ? record->timeStatus : NOVATEL_TIME_STATUS;
    const char *receiverStatus = record->receiverStatus[0] != '\0' ? record->receiverStatus : "00000000";
    const char *reserved = record->reserved[0] != '\0' ? record->reserved : "0000";
    const char *receiverVersion = record->receiverVersion[0] != '\0' ? record->receiverVersion : "0";

    char body[SENTENCE_BUFFER_SIZE];

    const int bodyLength = std::snprintf(
        body,
        sizeof(body),
        "BESTPOSA,%s,%s,%s,%s,%u,%.3f,%s,%s,%s;"
        "%s,%s,%.11f,%.11f,%.4f,%.4f,WGS84,"
        "%.4f,%.4f,%.4f,\"\",0.000,0.000,"
        "%d,%d,%d,%d,00,00,00,00",
        port,
        sequence,
        idleTime,
        timeStatus,
        record->week,
        record->second,
        receiverStatus,
        reserved,
        receiverVersion,
        solutionStatus,
        positionType,
        record->latitude,
        record->longitude,
        mslHeight,
        options->geoidSeparation,
        options->inspvaHorizontalSigma,
        options->inspvaHorizontalSigma,
        options->inspvaHeightSigma,
        satelliteNumber,
        satelliteNumber,
        satelliteNumber,
        satelliteNumber);

    if (bodyLength <= 0 ||
        static_cast<size_t>(bodyLength) >= sizeof(body))
    {
        return false;
    }

    const uint32_t crc = CalculateNovatelCrc32(body);

    const int outputLength = std::snprintf(
        output,
        outputSize,
        "#%s*%08x\r\n",
        body,
        static_cast<unsigned int>(crc));

    return outputLength > 0 &&
           static_cast<size_t>(outputLength) < outputSize;
}


static unsigned int ParseHexLowByte(const char *text)
{
    if (text == NULL)
    {
        return 0U;
    }

    while (*text == ' ' || *text == '\t')
    {
        ++text;
    }

    if (text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X'))
    {
        text += 2;
    }

    if (*text == '\0')
    {
        return 0U;
    }

    errno = 0;
    char *end = NULL;
    const unsigned long value =
        std::strtoul(text, &end, 16);

    if (end == text || errno == ERANGE)
    {
        return 0U;
    }

    while (*end == ' ' || *end == '\t')
    {
        ++end;
    }

    if (*end != '\0')
    {
        return 0U;
    }

    return static_cast<unsigned int>(value & 0xFFU);
}

static bool BuildInspvaFromInspvax(
    const InspvaxRecord *record,
    char *output,
    size_t outputSize)
{
    if (record == NULL || output == NULL || outputSize == 0U)
    {
        return false;
    }

    const double ellipsoidalHeight =
        record->mslHeight + record->undulation;

    if (!std::isfinite(ellipsoidalHeight))
    {
        return false;
    }

    const char *port =
        record->port[0] != '\0' ? record->port : NOVATEL_PORT;
    const char *sequence =
        record->sequence[0] != '\0' ? record->sequence : "0";
    const char *idleTime =
        record->idleTime[0] != '\0' ? record->idleTime : "0.0";
    const char *timeStatus =
        record->timeStatus[0] != '\0'
            ? record->timeStatus
            : NOVATEL_TIME_STATUS;
    const char *receiverStatus =
        record->receiverStatus[0] != '\0'
            ? record->receiverStatus
            : "00000000";
    const char *reserved =
        record->reserved[0] != '\0'
            ? record->reserved
            : "0000";
    const char *receiverVersion =
        record->receiverVersion[0] != '\0'
            ? record->receiverVersion
            : "0";

    char body[SENTENCE_BUFFER_SIZE];

    const int bodyLength = std::snprintf(
        body,
        sizeof(body),
        "INSPVAA,%s,%s,%s,%s,%u,%.3f,%s,%s,%s;"
        "%u,%.9f,%.11f,%.11f,%.4f,"
        "%.6f,%.6f,%.6f,"
        "%.9f,%.9f,%.9f,%s",
        port,
        sequence,
        idleTime,
        timeStatus,
        record->week,
        record->second,
        receiverStatus,
        reserved,
        receiverVersion,
        record->week,
        record->second,
        record->latitude,
        record->longitude,
        ellipsoidalHeight,
        record->northVelocity,
        record->eastVelocity,
        record->upVelocity,
        record->roll,
        record->pitch,
        record->azimuth,
        record->insStatus);

    if (bodyLength <= 0 ||
        static_cast<size_t>(bodyLength) >= sizeof(body))
    {
        return false;
    }

    const uint32_t crc = CalculateNovatelCrc32(body);

    const int outputLength = std::snprintf(
        output,
        outputSize,
        "#%s*%08x\r\n",
        body,
        static_cast<unsigned int>(crc));

    return outputLength > 0 &&
           static_cast<size_t>(outputLength) < outputSize;
}

static bool BuildBestPosFromInspvax(
    const InspvaxRecord *record,
    const ProgramOptions *options,
    char *output,
    size_t outputSize)
{
    if (record == NULL || options == NULL ||
        output == NULL || outputSize == 0U)
    {
        return false;
    }

    const bool solutionGood =
        IsInspvaSolutionGood(record->insStatus);

    const char *solutionStatus =
        solutionGood
            ? "SOL_COMPUTED"
            : "INSUFFICIENT_OBS";

    const char *positionType =
        solutionGood && record->posType[0] != '\0'
            ? record->posType
            : "NONE";

    int satelliteNumber = options->satelliteNumber;

    if (satelliteNumber < 0)
    {
        satelliteNumber = 0;
    }
    else if (satelliteNumber > 255)
    {
        satelliteNumber = 255;
    }

    const char *port =
        record->port[0] != '\0' ? record->port : NOVATEL_PORT;
    const char *sequence =
        record->sequence[0] != '\0' ? record->sequence : "0";
    const char *idleTime =
        record->idleTime[0] != '\0' ? record->idleTime : "0.0";
    const char *timeStatus =
        record->timeStatus[0] != '\0'
            ? record->timeStatus
            : NOVATEL_TIME_STATUS;
    const char *receiverStatus =
        record->receiverStatus[0] != '\0'
            ? record->receiverStatus
            : "00000000";
    const char *reserved =
        record->reserved[0] != '\0'
            ? record->reserved
            : "0000";
    const char *receiverVersion =
        record->receiverVersion[0] != '\0'
            ? record->receiverVersion
            : "0";

    const unsigned int extSolStat =
        ParseHexLowByte(record->extSolStat);

    char body[SENTENCE_BUFFER_SIZE];

    /*
     * INSPVAX already reports:
     *   - Height above mean sea level
     *   - Undulation
     *   - Position type
     *   - Latitude/longitude/height standard deviations
     *
     * Therefore these values are copied directly into BESTPOS. Satellite
     * counts do not exist in INSPVAX and come from --sat-num.
     */
    const int bodyLength = std::snprintf(
        body,
        sizeof(body),
        "BESTPOSA,%s,%s,%s,%s,%u,%.3f,%s,%s,%s;"
        "%s,%s,%.11f,%.11f,%.4f,%.4f,WGS84,"
        "%.4f,%.4f,%.4f,\"\",0.000,0.000,"
        "%d,%d,%d,%d,00,%02x,00,00",
        port,
        sequence,
        idleTime,
        timeStatus,
        record->week,
        record->second,
        receiverStatus,
        reserved,
        receiverVersion,
        solutionStatus,
        positionType,
        record->latitude,
        record->longitude,
        record->mslHeight,
        record->undulation,
        record->latSigma,
        record->lonSigma,
        record->heightSigma,
        satelliteNumber,
        satelliteNumber,
        satelliteNumber,
        satelliteNumber,
        extSolStat);

    if (bodyLength <= 0 ||
        static_cast<size_t>(bodyLength) >= sizeof(body))
    {
        return false;
    }

    const uint32_t crc = CalculateNovatelCrc32(body);

    const int outputLength = std::snprintf(
        output,
        outputSize,
        "#%s*%08x\r\n",
        body,
        static_cast<unsigned int>(crc));

    return outputLength > 0 &&
           static_cast<size_t>(outputLength) < outputSize;
}

static bool BuildOutputFileName(const char *prefix,
                                const char *extension,
                                char *output,
                                size_t outputSize)
{
    if (prefix == NULL || extension == NULL ||
        output == NULL || outputSize == 0)
    {
        return false;
    }

    const int length = std::snprintf(
        output,
        outputSize,
        "%s%s",
        prefix,
        extension);

    return length > 0 && static_cast<size_t>(length) < outputSize;
}

static bool WriteSentence(FILE *file, const char *sentence)
{
    return file != NULL && sentence != NULL &&
           std::fputs(sentence, file) != EOF;
}

static int RunPositionToAll(const ProgramOptions *inputOptions)
{
    if (inputOptions == NULL)
    {
        return 1;
    }

    ProgramOptions options = *inputOptions;

    char ggaFileName[FILE_NAME_BUFFER_SIZE];
    char inspvaFileName[FILE_NAME_BUFFER_SIZE];
    char bestPosFileName[FILE_NAME_BUFFER_SIZE];

    if (!BuildOutputFileName(options.outputPrefix,
                             ".gga",
                             ggaFileName,
                             sizeof(ggaFileName)) ||
        !BuildOutputFileName(options.outputPrefix,
                             ".inspva",
                             inspvaFileName,
                             sizeof(inspvaFileName)) ||
        !BuildOutputFileName(options.outputPrefix,
                             ".bestpos",
                             bestPosFileName,
                             sizeof(bestPosFileName)))
    {
        std::printf("Output file name is too long.\n");
        return 1;
    }

    FILE *inputFile = std::fopen(options.inputFileName, "rb");

    if (inputFile == NULL)
    {
        std::printf("Failed to open input file: %s\n",
                    options.inputFileName);
        return 1;
    }

    RateDetectionResult rateDetection;
    std::memset(&rateDetection, 0, sizeof(rateDetection));

    if (options.rateEnabled && options.autoDetectInputHz)
    {
        if (!DetectInputRate(inputFile, &rateDetection))
        {
            std::fclose(inputFile);
            return 1;
        }

        options.inputHz = rateDetection.inputHz;

        if (options.outputHz > options.inputHz ||
            options.inputHz % options.outputHz != 0)
        {
            std::printf(
                "Invalid output rate: detected input rate is %u Hz, but %u Hz is not an exact divisor.\n",
                options.inputHz,
                options.outputHz);
            std::fclose(inputFile);
            return 1;
        }

        options.decimationFactor =
            options.inputHz / options.outputHz;
    }

    FILE *ggaFile = std::fopen(ggaFileName, "wb");

    if (ggaFile == NULL)
    {
        std::printf("Failed to create GGA file: %s\n", ggaFileName);
        std::fclose(inputFile);
        return 1;
    }

    FILE *inspvaFile = std::fopen(inspvaFileName, "wb");

    if (inspvaFile == NULL)
    {
        std::printf("Failed to create INSPVA file: %s\n", inspvaFileName);
        std::fclose(inputFile);
        std::fclose(ggaFile);
        return 1;
    }

    FILE *bestPosFile = std::fopen(bestPosFileName, "wb");

    if (bestPosFile == NULL)
    {
        std::printf("Failed to create BESTPOS file: %s\n", bestPosFileName);
        std::fclose(inputFile);
        std::fclose(ggaFile);
        std::fclose(inspvaFile);
        return 1;
    }

    char line[LINE_BUFFER_SIZE];

    unsigned long totalLineCount = 0;
    unsigned long validRecordCount = 0;
    unsigned long selectedRecordCount = 0;
    unsigned long decimatedRecordCount = 0;
    unsigned long ggaOutputCount = 0;
    unsigned long inspvaOutputCount = 0;
    unsigned long bestPosOutputCount = 0;
    unsigned long missingWeekCount = 0;

    bool writeFailed = false;

    while (std::fgets(line, sizeof(line), inputFile) != NULL)
    {
        ++totalLineCount;

        PositionRecord record;

        if (!ParsePositionLine(line, &record))
        {
            continue;
        }

        const unsigned long validIndex = validRecordCount;
        ++validRecordCount;

        if (validIndex % options.decimationFactor != 0)
        {
            ++decimatedRecordCount;
            continue;
        }

        ++selectedRecordCount;

        char sentence[SENTENCE_BUFFER_SIZE];

        if (BuildGgaSentence(&record,
                             &options,
                             sentence,
                             sizeof(sentence)))
        {
            if (!WriteSentence(ggaFile, sentence))
            {
                writeFailed = true;
                break;
            }

            ++ggaOutputCount;
        }

        const bool gnssTimeAvailable =
            record.hasWeek || options.defaultWeek != 0;

        if (BuildInspvaSentence(&record,
                                &options,
                                sentence,
                                sizeof(sentence)))
        {
            if (!WriteSentence(inspvaFile, sentence))
            {
                writeFailed = true;
                break;
            }

            ++inspvaOutputCount;
        }

        if (BuildBestPosSentence(&record,
                                 &options,
                                 sentence,
                                 sizeof(sentence)))
        {
            if (!WriteSentence(bestPosFile, sentence))
            {
                writeFailed = true;
                break;
            }

            ++bestPosOutputCount;
        }

        if (!gnssTimeAvailable)
        {
            ++missingWeekCount;
        }
    }

    std::fclose(inputFile);
    std::fclose(ggaFile);
    std::fclose(inspvaFile);
    std::fclose(bestPosFile);

    if (writeFailed)
    {
        std::printf("Failed while writing an output file.\n");
        return 1;
    }

    std::printf(
        "Conversion completed.\n"
        "Mode               : position -> GGA + INSPVA + BESTPOS\n"
        "Input lines        : %lu\n"
        "Valid records      : %lu\n"
        "Selected records   : %lu\n"
        "Rate-dropped       : %lu\n"
        "GGA records        : %lu\n"
        "INSPVA records     : %lu\n"
        "BESTPOS records    : %lu\n"
        "Missing week       : %lu\n"
        "GGA file           : %s\n"
        "INSPVA file        : %s\n"
        "BESTPOS file       : %s\n",
        totalLineCount,
        validRecordCount,
        selectedRecordCount,
        decimatedRecordCount,
        ggaOutputCount,
        inspvaOutputCount,
        bestPosOutputCount,
        missingWeekCount,
        ggaFileName,
        inspvaFileName,
        bestPosFileName);

    if (options.rateEnabled)
    {
        std::printf(
            "Rate conversion     : %u Hz -> %u Hz (keep every %uth record)\n",
            options.inputHz,
            options.outputHz,
            options.decimationFactor);

        if (options.autoDetectInputHz)
        {
            std::printf(
                "Rate detection      : automatic, nominal interval %.9f s, %u/%u intervals consistent\n",
                rateDetection.nominalInterval,
                rateDetection.consistentCount,
                rateDetection.intervalCount);
        }
        else
        {
            std::printf("Rate detection      : manual input rate\n");
        }
    }
    else
    {
        std::printf("Rate conversion     : disabled\n");
    }

    if (missingWeekCount > 0)
    {
        std::printf(
            "\nWarning: selected records without a Week field were written only to GGA.\n"
            "Specify --week N to enable INSPVA and BESTPOS output.\n");
    }

    return 0;
}

static int RunInspvaToBestPos(const ProgramOptions *inputOptions)
{
    if (inputOptions == NULL)
    {
        return 1;
    }

    ProgramOptions options = *inputOptions;

    FILE *inputFile = std::fopen(options.inputFileName, "rb");

    if (inputFile == NULL)
    {
        std::printf("Failed to open INSPVA input file: %s\n",
                    options.inputFileName);
        return 1;
    }

    RateDetectionResult rateDetection;
    std::memset(&rateDetection, 0, sizeof(rateDetection));

    if (options.rateEnabled && options.autoDetectInputHz)
    {
        if (!DetectInspvaInputRate(inputFile, &rateDetection))
        {
            std::fclose(inputFile);
            return 1;
        }

        options.inputHz = rateDetection.inputHz;

        if (options.outputHz > options.inputHz ||
            options.inputHz % options.outputHz != 0)
        {
            std::printf(
                "Invalid output rate: detected INSPVA input rate is %u Hz, but %u Hz is not an exact divisor.\n",
                options.inputHz,
                options.outputHz);
            std::fclose(inputFile);
            return 1;
        }

        options.decimationFactor =
            options.inputHz / options.outputHz;
    }

    FILE *bestPosFile = std::fopen(options.outputPrefix, "wb");

    if (bestPosFile == NULL)
    {
        std::printf("Failed to create BESTPOS output file: %s\n",
                    options.outputPrefix);
        std::fclose(inputFile);
        return 1;
    }

    char line[LINE_BUFFER_SIZE];
    unsigned long totalLineCount = 0;
    unsigned long validInspvaCount = 0;
    unsigned long selectedRecordCount = 0;
    unsigned long decimatedRecordCount = 0;
    unsigned long bestPosOutputCount = 0;
    unsigned long invalidSolutionCount = 0;
    bool writeFailed = false;

    while (std::fgets(line, sizeof(line), inputFile) != NULL)
    {
        ++totalLineCount;

        InspvaRecord record;

        if (!ParseInspvaAsciiLine(line, &record))
        {
            continue;
        }

        const unsigned long validIndex = validInspvaCount;
        ++validInspvaCount;

        if (validIndex % options.decimationFactor != 0)
        {
            ++decimatedRecordCount;
            continue;
        }

        ++selectedRecordCount;

        if (!IsInspvaSolutionGood(record.insStatus))
        {
            ++invalidSolutionCount;
        }

        char sentence[SENTENCE_BUFFER_SIZE];

        if (!BuildBestPosFromInspva(&record,
                                    &options,
                                    sentence,
                                    sizeof(sentence)))
        {
            continue;
        }

        if (!WriteSentence(bestPosFile, sentence))
        {
            writeFailed = true;
            break;
        }

        ++bestPosOutputCount;
    }

    std::fclose(inputFile);
    std::fclose(bestPosFile);

    if (writeFailed)
    {
        std::printf("Failed while writing BESTPOS output.\n");
        return 1;
    }

    if (validInspvaCount == 0)
    {
        std::printf(
            "No INSPVA ASCII records were found. Supported input records are #INSPVAA and %%INSPVASA.\n");
        return 1;
    }

    std::printf(
        "Conversion completed.\n"
        "Mode               : INSPVA -> BESTPOS\n"
        "Input lines        : %lu\n"
        "INSPVA records     : %lu\n"
        "Selected records   : %lu\n"
        "Rate-dropped       : %lu\n"
        "Invalid INS status : %lu\n"
        "BESTPOS records    : %lu\n"
        "BESTPOS pos type   : %s\n"
        "Geoid separation   : %.4f m\n"
        "Horizontal sigma   : %.4f m\n"
        "Height sigma       : %.4f m\n"
        "BESTPOS file       : %s\n",
        totalLineCount,
        validInspvaCount,
        selectedRecordCount,
        decimatedRecordCount,
        invalidSolutionCount,
        bestPosOutputCount,
        options.inspvaBestPosType,
        options.geoidSeparation,
        options.inspvaHorizontalSigma,
        options.inspvaHeightSigma,
        options.outputPrefix);

    if (options.rateEnabled)
    {
        std::printf(
            "Rate conversion     : %u Hz -> %u Hz (keep every %uth record)\n",
            options.inputHz,
            options.outputHz,
            options.decimationFactor);

        if (options.autoDetectInputHz)
        {
            std::printf(
                "Rate detection      : automatic, nominal interval %.9f s, %u/%u intervals consistent\n",
                rateDetection.nominalInterval,
                rateDetection.consistentCount,
                rateDetection.intervalCount);
        }
        else
        {
            std::printf("Rate detection      : manual input rate\n");
        }
    }
    else
    {
        std::printf("Rate conversion     : disabled\n");
    }

    return 0;
}


static bool ResolveInspvaxRate(FILE *inputFile,
                               ProgramOptions *options,
                               RateDetectionResult *rateDetection)
{
    if (inputFile == NULL || options == NULL ||
        rateDetection == NULL)
    {
        return false;
    }

    std::memset(rateDetection, 0, sizeof(*rateDetection));

    if (!options->rateEnabled ||
        !options->autoDetectInputHz)
    {
        return true;
    }

    if (!DetectInspvaxInputRate(
            inputFile,
            rateDetection))
    {
        return false;
    }

    options->inputHz = rateDetection->inputHz;

    if (options->outputHz > options->inputHz ||
        options->inputHz % options->outputHz != 0U)
    {
        std::printf(
            "Invalid output rate: detected INSPVAX input rate is %u Hz, but %u Hz is not an exact divisor.\n",
            options->inputHz,
            options->outputHz);
        return false;
    }

    options->decimationFactor =
        options->inputHz / options->outputHz;

    return true;
}

static void PrintRateSummary(
    const ProgramOptions *options,
    const RateDetectionResult *rateDetection)
{
    if (options == NULL)
    {
        return;
    }

    if (options->rateEnabled)
    {
        std::printf(
            "Rate conversion     : %u Hz -> %u Hz (keep every %uth record)\n",
            options->inputHz,
            options->outputHz,
            options->decimationFactor);

        if (options->autoDetectInputHz &&
            rateDetection != NULL)
        {
            std::printf(
                "Rate detection      : automatic, nominal interval %.9f s, %u/%u intervals consistent\n",
                rateDetection->nominalInterval,
                rateDetection->consistentCount,
                rateDetection->intervalCount);
        }
        else
        {
            std::printf(
                "Rate detection      : manual input rate\n");
        }
    }
    else
    {
        std::printf(
            "Rate conversion     : disabled\n");
    }
}

static int RunInspvaxToInspva(
    const ProgramOptions *inputOptions)
{
    if (inputOptions == NULL)
    {
        return 1;
    }

    ProgramOptions options = *inputOptions;

    if (std::strcmp(
            options.inputFileName,
            options.outputPrefix) == 0)
    {
        std::printf(
            "Input and output paths must be different.\n");
        return 1;
    }

    FILE *inputFile =
        std::fopen(options.inputFileName, "rb");

    if (inputFile == NULL)
    {
        std::printf(
            "Failed to open INSPVAX input file: %s\n",
            options.inputFileName);
        return 1;
    }

    RateDetectionResult rateDetection;

    if (!ResolveInspvaxRate(
            inputFile,
            &options,
            &rateDetection))
    {
        std::fclose(inputFile);
        return 1;
    }

    FILE *outputFile =
        std::fopen(options.outputPrefix, "wb");

    if (outputFile == NULL)
    {
        std::printf(
            "Failed to create INSPVA output file: %s\n",
            options.outputPrefix);
        std::fclose(inputFile);
        return 1;
    }

    char *line = NULL;
    size_t lineCapacity = 0U;
    size_t lineLength = 0U;

    unsigned long long totalRecords = 0U;
    unsigned long long inspvaxRecords = 0U;
    unsigned long long convertedRecords = 0U;
    unsigned long long decimatedRecords = 0U;
    unsigned long long malformedRecords = 0U;
    unsigned int diagnostics = 0U;

    int readResult = 0;
    bool writeFailed = false;

    while ((readResult = ReadDynamicRecord(
                inputFile,
                &line,
                &lineCapacity,
                &lineLength)) == 1)
    {
        ++totalRecords;

        InspvaxRecord record;
        InspvaxParseError parseError =
            INSPVAX_PARSE_OK;

        const int parseResult =
            ParseInspvaxAsciiLine(
                line,
                lineLength,
                &record,
                &parseError);

        if (parseResult == 1)
        {
            const unsigned long long validIndex =
                inspvaxRecords;
            ++inspvaxRecords;

            if (validIndex %
                    options.decimationFactor !=
                0U)
            {
                ++decimatedRecords;
                continue;
            }

            char sentence[SENTENCE_BUFFER_SIZE];

            if (!BuildInspvaFromInspvax(
                    &record,
                    sentence,
                    sizeof(sentence)))
            {
                std::printf(
                    "Failed to build INSPVA record %llu.\n",
                    totalRecords);
                writeFailed = true;
                break;
            }

            if (!WriteSentence(outputFile, sentence))
            {
                writeFailed = true;
                break;
            }

            ++convertedRecords;
        }
        else
        {
            if (parseResult < 0)
            {
                ++malformedRecords;

                if (diagnostics <
                    INSPVAX_MAX_DIAGNOSTICS)
                {
                    std::fprintf(
                        stderr,
                        "[MALFORMED INSPVAX] record=%llu reason=%s\n",
                        totalRecords,
                        InspvaxParseErrorText(
                            parseError));

                    PrintInspvaxPreview(
                        line,
                        lineLength);
                    ++diagnostics;
                }
            }

            /*
             * Preserve v3 behavior:
             *   - non-INSPVAX and malformed records are copied unchanged
             *     unless --only-inspva is specified.
             *   - a valid INSPVAX record that is dropped by --rate is not
             *     copied, otherwise the requested downsampling would be lost.
             */
            if (!options.onlyInspva)
            {
                if (std::fwrite(
                        line,
                        1U,
                        lineLength,
                        outputFile) !=
                    lineLength)
                {
                    writeFailed = true;
                    break;
                }
            }
        }
    }

    std::free(line);
    std::fclose(inputFile);

    if (std::fflush(outputFile) != 0)
    {
        writeFailed = true;
    }

    std::fclose(outputFile);

    if (readResult < 0)
    {
        std::printf(
            "Failed while reading INSPVAX input file.\n");
        return 1;
    }

    if (writeFailed)
    {
        std::printf(
            "Failed while writing INSPVA output file.\n");
        return 1;
    }

    if (inspvaxRecords == 0U)
    {
        std::printf(
            "No valid #INSPVAXA records were found.\n");
        return 1;
    }

    std::printf(
        "Conversion completed.\n"
        "Mode               : INSPVAX -> INSPVA\n"
        "Input records      : %llu\n"
        "INSPVAX records    : %llu\n"
        "Converted records  : %llu\n"
        "Rate-dropped       : %llu\n"
        "Malformed INSPVAX  : %llu\n"
        "Input CRC          : ignored\n"
        "Output CRC         : recalculated\n"
        "Only INSPVA        : %s\n"
        "INSPVA file        : %s\n",
        totalRecords,
        inspvaxRecords,
        convertedRecords,
        decimatedRecords,
        malformedRecords,
        options.onlyInspva ? "yes" : "no",
        options.outputPrefix);

    PrintRateSummary(
        &options,
        &rateDetection);

    if (malformedRecords > diagnostics)
    {
        std::printf(
            "Only the first %u malformed INSPVAX diagnostics were printed.\n",
            INSPVAX_MAX_DIAGNOSTICS);
    }

    return malformedRecords == 0U ? 0 : 3;
}

static int RunInspvaxToBestPos(
    const ProgramOptions *inputOptions)
{
    if (inputOptions == NULL)
    {
        return 1;
    }

    ProgramOptions options = *inputOptions;

    if (std::strcmp(
            options.inputFileName,
            options.outputPrefix) == 0)
    {
        std::printf(
            "Input and output paths must be different.\n");
        return 1;
    }

    FILE *inputFile =
        std::fopen(options.inputFileName, "rb");

    if (inputFile == NULL)
    {
        std::printf(
            "Failed to open INSPVAX input file: %s\n",
            options.inputFileName);
        return 1;
    }

    RateDetectionResult rateDetection;

    if (!ResolveInspvaxRate(
            inputFile,
            &options,
            &rateDetection))
    {
        std::fclose(inputFile);
        return 1;
    }

    FILE *outputFile =
        std::fopen(options.outputPrefix, "wb");

    if (outputFile == NULL)
    {
        std::printf(
            "Failed to create BESTPOS output file: %s\n",
            options.outputPrefix);
        std::fclose(inputFile);
        return 1;
    }

    char *line = NULL;
    size_t lineCapacity = 0U;
    size_t lineLength = 0U;

    unsigned long long totalRecords = 0U;
    unsigned long long inspvaxRecords = 0U;
    unsigned long long selectedRecords = 0U;
    unsigned long long decimatedRecords = 0U;
    unsigned long long bestPosRecords = 0U;
    unsigned long long malformedRecords = 0U;
    unsigned long long invalidSolutionRecords = 0U;
    unsigned int diagnostics = 0U;

    int readResult = 0;
    bool writeFailed = false;

    while ((readResult = ReadDynamicRecord(
                inputFile,
                &line,
                &lineCapacity,
                &lineLength)) == 1)
    {
        ++totalRecords;

        InspvaxRecord record;
        InspvaxParseError parseError =
            INSPVAX_PARSE_OK;

        const int parseResult =
            ParseInspvaxAsciiLine(
                line,
                lineLength,
                &record,
                &parseError);

        if (parseResult == 0)
        {
            continue;
        }

        if (parseResult < 0)
        {
            ++malformedRecords;

            if (diagnostics <
                INSPVAX_MAX_DIAGNOSTICS)
            {
                std::fprintf(
                    stderr,
                    "[MALFORMED INSPVAX] record=%llu reason=%s\n",
                    totalRecords,
                    InspvaxParseErrorText(
                        parseError));

                PrintInspvaxPreview(
                    line,
                    lineLength);
                ++diagnostics;
            }

            continue;
        }

        const unsigned long long validIndex =
            inspvaxRecords;
        ++inspvaxRecords;

        if (validIndex %
                options.decimationFactor !=
            0U)
        {
            ++decimatedRecords;
            continue;
        }

        ++selectedRecords;

        if (!IsInspvaSolutionGood(
                record.insStatus))
        {
            ++invalidSolutionRecords;
        }

        char sentence[SENTENCE_BUFFER_SIZE];

        if (!BuildBestPosFromInspvax(
                &record,
                &options,
                sentence,
                sizeof(sentence)))
        {
            std::printf(
                "Failed to build BESTPOS from INSPVAX record %llu.\n",
                totalRecords);
            writeFailed = true;
            break;
        }

        if (!WriteSentence(
                outputFile,
                sentence))
        {
            writeFailed = true;
            break;
        }

        ++bestPosRecords;
    }

    std::free(line);
    std::fclose(inputFile);

    if (std::fflush(outputFile) != 0)
    {
        writeFailed = true;
    }

    std::fclose(outputFile);

    if (readResult < 0)
    {
        std::printf(
            "Failed while reading INSPVAX input file.\n");
        return 1;
    }

    if (writeFailed)
    {
        std::printf(
            "Failed while writing BESTPOS output file.\n");
        return 1;
    }

    if (inspvaxRecords == 0U)
    {
        std::printf(
            "No valid #INSPVAXA records were found.\n");
        return 1;
    }

    std::printf(
        "Conversion completed.\n"
        "Mode               : INSPVAX -> BESTPOS\n"
        "Input records      : %llu\n"
        "INSPVAX records    : %llu\n"
        "Selected records   : %llu\n"
        "Rate-dropped       : %llu\n"
        "Malformed INSPVAX  : %llu\n"
        "Invalid INS status : %llu\n"
        "BESTPOS records    : %llu\n"
        "Satellite count    : %d\n"
        "Input CRC          : ignored\n"
        "Output CRC         : recalculated\n"
        "BESTPOS file       : %s\n",
        totalRecords,
        inspvaxRecords,
        selectedRecords,
        decimatedRecords,
        malformedRecords,
        invalidSolutionRecords,
        bestPosRecords,
        options.satelliteNumber,
        options.outputPrefix);

    PrintRateSummary(
        &options,
        &rateDetection);

    if (malformedRecords > diagnostics)
    {
        std::printf(
            "Only the first %u malformed INSPVAX diagnostics were printed.\n",
            INSPVAX_MAX_DIAGNOSTICS);
    }

    return malformedRecords == 0U ? 0 : 3;
}


/* -----------------------------------------------------------------------
 * CSV/TSV -> INSPVA. CSV input column names are mandatory and matched
 * case-insensitively, avoiding positional assumptions for reordered data.
 * Supported separators: comma and tab (selected from the first header row).
 * CSV quoted values and doubled quotes are supported.
 * ----------------------------------------------------------------------- */

enum CsvColumn
{
    CSV_TOW, CSV_UTC, CSV_LEAP, CSV_Q, CSV_LAT, CSV_LON,
    CSV_HEIGHT, CSV_POS_X, CSV_POS_Y, CSV_POS_Z,
    CSV_VEL_X, CSV_VEL_Y, CSV_VEL_Z, CSV_VEL_E, CSV_VEL_N,
    CSV_VEL_U, CSV_HEADING, CSV_REQUIRED_COUNT
};

static const char *CSV_COLUMN_NAMES[CSV_REQUIRED_COUNT] =
{
    "TOW", "UTC", "Leap", "Q", "Lat", "Lon", "Height",
    "Pos_x", "Pos_y", "Pos_z", "Vel_x", "Vel_y", "Vel_z",
    "Vel_e", "Vel_n", "Vel_u", "Heading"
};

struct CsvHeader
{
    char separator;
    int columnCount;
    int indices[CSV_REQUIRED_COUNT];
};

struct CsvPositionRecord
{
    double tow;
    double latitude;
    double longitude;
    double ellipsoidalHeight;
    double eastVelocity;
    double northVelocity;
    double upVelocity;
    double heading;
    int q;
    bool hasUtcDate;
    unsigned int utcDateWeek;
    double utcDateTow;
};

static bool EqualsAsciiIgnoreCase(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return false;
    while (*a != '\0' && *b != '\0')
    {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + 32);
        if (ca != cb) return false;
        ++a; ++b;
    }
    return *a == '\0' && *b == '\0';
}

/* Split in place; the character buffer must be writable. */
static int SplitCsvFields(char *line, char separator,
                          char *fields[], int capacity)
{
    if (line == NULL || fields == NULL || capacity <= 0)
        return -1;
    char *read = line;
    char *write = line;
    int count = 0;
    for (;;)
    {
        if (count == capacity)
            return -1;
        fields[count++] = write;
        bool quoted = false;
        bool afterQuote = false;
        while (*read != '\0' && *read != '\r' && *read != '\n')
        {
            const char ch = *read++;
            if (quoted)
            {
                if (ch == '"')
                {
                    if (*read == '"')
                    {
                        *write++ = '"';
                        ++read;
                    }
                    else
                        quoted = false, afterQuote = true;
                }
                else
                    *write++ = ch;
            }
            else if (ch == separator)
            {
                break;
            }
            else if (ch == '"')
            {
                if (write == fields[count - 1])
                    quoted = true;
                else
                    return -1;
            }
            else if (afterQuote && ch != ' ' && ch != '\t')
            {
                return -1;
            }
            else
                *write++ = ch;
        }
        if (quoted)
            return -1;
        const bool hasMore = read[-1] == separator;
        *write++ = '\0';
        if (!hasMore)
            break;
    }
    for (int i = 0; i < count; ++i)
    {
        char *first = fields[i];
        while (*first == ' ' || *first == '\t') ++first;
        if (first != fields[i]) fields[i] = first;
        char *last = first + std::strlen(first);
        while (last > first && (last[-1] == ' ' || last[-1] == '\t')) --last;
        *last = '\0';
    }
    return count;
}

static bool ParseCsvHeader(char *line, CsvHeader *header)
{
    if (line == NULL || header == NULL) return false;
    char *text = line;
    if (static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text += 3;  /* optional UTF-8 BOM */
    header->separator = std::strchr(text, '\t') != NULL ? '\t' : ',';
    char *fields[64];
    const int count = SplitCsvFields(text, header->separator, fields, 64);
    if (count <= 0)
    {
        std::fprintf(stderr, "Invalid CSV header.\n");
        return false;
    }
    header->columnCount = count;
    for (int i = 0; i < CSV_REQUIRED_COUNT; ++i) header->indices[i] = -1;
    for (int col = 0; col < count; ++col)
    {
        for (int i = 0; i < CSV_REQUIRED_COUNT; ++i)
        {
            if (EqualsAsciiIgnoreCase(fields[col], CSV_COLUMN_NAMES[i]))
            {
                if (header->indices[i] != -1)
                {
                    std::fprintf(stderr, "Duplicate CSV column: %s\n", CSV_COLUMN_NAMES[i]);
                    return false;
                }
                header->indices[i] = col;
            }
        }
    }
    bool ok = true;
    for (int i = 0; i < CSV_REQUIRED_COUNT; ++i)
    {
        if (header->indices[i] < 0)
        {
            std::fprintf(stderr, "Missing CSV column: %s\n", CSV_COLUMN_NAMES[i]);
            ok = false;
        }
    }
    return ok;
}

/* Days since 1970-01-01, Gregorian calendar; no OS timezone dependency. */
static int64_t DaysFromCivil(int year, unsigned int month, unsigned int day)
{
    year -= month <= 2U;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned int yoe = static_cast<unsigned int>(year - era * 400);
    const int adjustedMonth = static_cast<int>(month) + (month > 2U ? -3 : 9);
    const unsigned int doy = (153U * static_cast<unsigned int>(adjustedMonth) + 2U) / 5U + day - 1U;
    const unsigned int doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

static bool ParseUtcDateToGpsTime(const char *utcText, double leap,
                                  unsigned int *week, double *tow)
{
    if (utcText == NULL || week == NULL || tow == NULL ||
        !std::isfinite(leap) || leap < 0.0 || leap > 60.0)
        return false;
    int y = 0, m = 0, d = 0, h = 0, min = 0;
    double sec = 0.0;
    char sep = '\0', extra = '\0';
    int count = std::sscanf(utcText, "%d-%d-%d%c%d:%d:%lf%c",
                            &y, &m, &d, &sep, &h, &min, &sec, &extra);
    if (count < 7 || (sep != 'T' && sep != ' ') ||
        (count == 8 && extra != 'Z'))
    {
        count = std::sscanf(utcText, "%d/%d/%d%c%d:%d:%lf%c",
                            &y, &m, &d, &sep, &h, &min, &sec, &extra);
        if (count < 7 || (sep != 'T' && sep != ' ') ||
            (count == 8 && extra != 'Z'))
            return false;
    }
    if (y < 1980 || y > 9999 || m < 1 || m > 12 ||
        d < 1 || h < 0 || h > 23 || min < 0 || min > 59 ||
        sec < 0.0 || sec >= 60.0)
        return false;
    const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int daysInMonth = mdays[m - 1];
    if (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)))
        daysInMonth = 29;
    if (d > daysInMonth) return false;
    /* Unix day 3657 is 1980-01-06, GPS epoch. */
    const int64_t days = DaysFromCivil(y, static_cast<unsigned int>(m),
                                         static_cast<unsigned int>(d)) - 3657;
    const double gpsSec = static_cast<double>(days) * 86400.0 +
                          static_cast<double>(h * 3600 + min * 60) + sec + leap;
    if (gpsSec < 0.0) return false;
    const double wholeWeek = std::floor(gpsSec / GNSS_WEEK_SECONDS);
    if (wholeWeek >= static_cast<double>(UINT_MAX)) return false;
    *week = static_cast<unsigned int>(wholeWeek);
    *tow = gpsSec - wholeWeek * GNSS_WEEK_SECONDS;
    return true;
}

/* Optional detailed diagnostic used by the command-line CSV reader.
 * Other callers can leave the diagnostic arguments omitted. */
static bool CsvRowError(char *error, size_t errorSize,
                        const char *message, const char *column = NULL,
                        const char *value = NULL)
{
    if (error != NULL && errorSize > 0)
    {
        if (column != NULL && value != NULL)
            std::snprintf(error, errorSize, "%s: %s = '%.80s'",
                          message, column, value);
        else
            std::snprintf(error, errorSize, "%s", message);
    }
    return false;
}

static bool IsCsvBlankLine(const char *line)
{
    if (line == NULL) return true;
    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n')
        ++line;
    return *line == '\0';
}

static bool ParseCsvPositionRow(char *line, const CsvHeader *header,
                                CsvPositionRecord *record,
                                char *error = NULL, size_t errorSize = 0)
{
    if (line == NULL || header == NULL || record == NULL)
        return CsvRowError(error, errorSize, "null CSV parser argument");

    if (error != NULL && errorSize > 0) error[0] = '\0';

    char *fields[64];
    const int count = SplitCsvFields(line, header->separator, fields, 64);
    if (count < 0)
        return CsvRowError(error, errorSize,
                           "invalid CSV quoting or more than 64 columns");
    if (count != header->columnCount)
    {
        if (error != NULL && errorSize > 0)
            std::snprintf(error, errorSize,
                          "column count mismatch: expected %d, got %d "
                          "(delimiter: %s; check trailing separators and quotes)",
                          header->columnCount, count,
                          header->separator == '\t' ? "TAB" : "comma");
        return false;
    }

    double qValue = 0.0, leap = 0.0;
#define CSV_GET(name, target) \
    if (!ParseDoubleStrict(fields[header->indices[name]], &target)) \
        return CsvRowError(error, errorSize, "invalid numeric field", \
                           CSV_COLUMN_NAMES[name], fields[header->indices[name]])
    CSV_GET(CSV_TOW, record->tow);
    CSV_GET(CSV_Q, qValue);
    CSV_GET(CSV_LAT, record->latitude);
    CSV_GET(CSV_LON, record->longitude);
    CSV_GET(CSV_HEIGHT, record->ellipsoidalHeight);
    CSV_GET(CSV_VEL_E, record->eastVelocity);
    CSV_GET(CSV_VEL_N, record->northVelocity);
    CSV_GET(CSV_VEL_U, record->upVelocity);
    CSV_GET(CSV_HEADING, record->heading);
#undef CSV_GET

    if (record->tow < 0.0 || record->tow >= GNSS_WEEK_SECONDS)
        return CsvRowError(error, errorSize,
                           "TOW must be in [0,604800) seconds",
                           "TOW", fields[header->indices[CSV_TOW]]);
    if (record->latitude < -90.0 || record->latitude > 90.0)
        return CsvRowError(error, errorSize, "latitude outside [-90,90]",
                           "Lat", fields[header->indices[CSV_LAT]]);
    if (record->longitude < -180.0 || record->longitude > 180.0)
        return CsvRowError(error, errorSize, "longitude outside [-180,180]",
                           "Lon", fields[header->indices[CSV_LON]]);
    if (qValue < static_cast<double>(INT_MIN) ||
        qValue > static_cast<double>(INT_MAX) ||
        std::fabs(qValue - std::floor(qValue + 0.5)) > 1e-6)
        return CsvRowError(error, errorSize, "Q must be an integer",
                           "Q", fields[header->indices[CSV_Q]]);
    record->q = static_cast<int>(std::floor(qValue + 0.5));

    /* Cartesian columns are not required for INSPVA and may be empty. */
    record->hasUtcDate = false;
    const char *utc = fields[header->indices[CSV_UTC]];
    if (std::strchr(utc, '-') != NULL || std::strchr(utc, '/') != NULL)
    {
        if (!ParseDoubleStrict(fields[header->indices[CSV_LEAP]], &leap))
            return CsvRowError(error, errorSize,
                               "Leap must be numeric for a UTC date",
                               "Leap", fields[header->indices[CSV_LEAP]]);
        if (!ParseUtcDateToGpsTime(utc, leap, &record->utcDateWeek,
                                   &record->utcDateTow))
            return CsvRowError(error, errorSize,
                               "invalid UTC date/time (expected YYYY-MM-DD HH:MM:SS, "
                               "YYYY-MM-DDTHH:MM:SSZ, or YYYY/MM/DD HH:MM:SS)",
                               "UTC", utc);
        record->hasUtcDate = true;
        const double err = std::fabs(record->utcDateTow - record->tow);
        if (err > 0.5 && std::fabs(err - GNSS_WEEK_SECONDS) > 0.5)
        {
            if (error != NULL && errorSize > 0)
                std::snprintf(error, errorSize,
                              "UTC + Leap gives TOW %.6f, but CSV TOW is %.6f "
                              "(difference %.3f s). Verify whether TOW is GPS "
                              "seconds or UTC seconds and check Leap.",
                              record->utcDateTow, record->tow, err);
            return false;
        }
    }
    else
    {
        /* A time-of-day UTC field does not determine the GNSS week. */
        if (*fields[header->indices[CSV_LEAP]] != '\0' &&
            !ParseDoubleStrict(fields[header->indices[CSV_LEAP]], &leap))
            return CsvRowError(error, errorSize, "invalid numeric Leap field",
                               "Leap", fields[header->indices[CSV_LEAP]]);
    }
    return true;
}

static bool BuildInspvaFromCsv(const CsvPositionRecord *r,
                               unsigned int week, const ProgramOptions *options,
                               char *output, size_t outputSize)
{
    if (r == NULL || options == NULL || output == NULL || outputSize == 0)
        return false;
    double azimuth = std::fmod(r->heading, 360.0);
    if (azimuth < 0.0) azimuth += 360.0;
    char body[SENTENCE_BUFFER_SIZE];
    const int length = std::snprintf(
        body, sizeof(body),
        "INSPVAA,%s,0,0.0,%s,%u,%.3f,00000000,0000,0;"
        "%u,%.9f,%.11f,%.11f,%.4f,%.4f,%.4f,%.4f,%.9f,%.9f,%.9f,%s",
        NOVATEL_PORT, NOVATEL_TIME_STATUS,
        week, r->tow, week, r->tow,
        r->latitude, r->longitude, r->ellipsoidalHeight,
        r->northVelocity, r->eastVelocity, r->upVelocity,
        options->csvRoll, options->csvPitch, azimuth,
        MapInsStatus(r->q));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(body))
        return false;
    const uint32_t crc = CalculateNovatelCrc32(body);
    const int written = std::snprintf(output, outputSize, "#%s*%08x\r\n",
                                      body, static_cast<unsigned int>(crc));
    return written > 0 && static_cast<size_t>(written) < outputSize;
}

static bool DetectCsvInputRate(FILE *inputFile, const CsvHeader *header,
                               RateDetectionResult *result)
{
    if (inputFile == NULL || header == NULL || result == NULL) return false;
    /* The header has already been read; start the data from that position. */
    double intervals[RATE_DETECT_MAX_INTERVALS];
    unsigned int intervalCount = 0, validCount = 0;
    bool hasPrev = false;
    double prevTow = 0.0;
    char line[LINE_BUFFER_SIZE];
    while (intervalCount < RATE_DETECT_MAX_INTERVALS &&
           validCount < RATE_DETECT_MAX_VALID_RECORDS &&
           std::fgets(line, sizeof(line), inputFile) != NULL)
    {
        CsvPositionRecord r;
        if (!ParseCsvPositionRow(line, header, &r)) continue;
        ++validCount;
        if (hasPrev)
        {
            double dt = r.tow - prevTow;
            if (dt < -GNSS_WEEK_SECONDS / 2.0) dt += GNSS_WEEK_SECONDS;
            if (dt > 0.0 && dt <= 10.0)
                intervals[intervalCount++] = dt;
        }
        prevTow = r.tow;
        hasPrev = true;
    }
    if (intervalCount < 5)
    {
        std::fprintf(stderr, "CSV rate detection failed: fewer than 5 intervals.\n");
        return false;
    }
    std::qsort(intervals, intervalCount, sizeof(intervals[0]),
               CompareDoubleAscending);
    const double representative = intervals[(intervalCount - 1U) / 5U];
    const double rawHz = 1.0 / representative;
    if (rawHz > RATE_DETECT_MAX_HZ || rawHz < 1.0)
    {
        std::fprintf(stderr, "CSV rate detection failed: source frequency is not 1-10000 Hz.\n");
        return false;
    }
    const unsigned int hz = static_cast<unsigned int>(std::floor(rawHz + 0.5));
    const double dt = 1.0 / static_cast<double>(hz);
    if (std::fabs(representative - dt) > dt * 0.02 + 1e-9)
    {
        std::fprintf(stderr, "CSV rate detection failed: non-integer frequency.\n");
        return false;
    }
    unsigned int consistent = 0, single = 0;
    for (unsigned int i = 0; i < intervalCount; ++i)
    {
        const unsigned int steps = static_cast<unsigned int>(
            std::floor(intervals[i] / dt + 0.5));
        if (steps == 0) continue;
        if (std::fabs(intervals[i] - steps * dt) <= dt * 0.05 + 1e-8)
        {
            ++consistent;
            if (steps == 1) ++single;
        }
    }
    if (consistent * 100U < intervalCount * 80U ||
        single * 100U < intervalCount * 20U)
    {
        std::fprintf(stderr, "CSV rate detection failed: irregular TOW grid.\n");
        return false;
    }
    result->inputHz = hz;
    result->nominalInterval = dt;
    result->intervalCount = intervalCount;
    result->singleStepCount = single;
    result->consistentCount = consistent;
    return true;
}

static int RunCsvToInspva(const ProgramOptions *inputOptions)
{
    ProgramOptions options = *inputOptions;
    FILE *inputFile = std::fopen(options.inputFileName, "rb");
    if (inputFile == NULL)
    {
        std::fprintf(stderr, "Cannot open CSV input: %s\n", options.inputFileName);
        return 1;
    }
    char line[LINE_BUFFER_SIZE];
    CsvHeader header;
    if (std::fgets(line, sizeof(line), inputFile) == NULL ||
        !ParseCsvHeader(line, &header))
    {
        std::fprintf(stderr, "CSV requires the named header in its first line.\n");
        std::fclose(inputFile);
        return 1;
    }
    /* Check first data record before opening output, to avoid an empty file
       when no --week was specified for a time-only UTC column. */
    bool firstFound = false;
    CsvPositionRecord first;
    unsigned long firstDataLineNumber = 1;
    while (std::fgets(line, sizeof(line), inputFile) != NULL)
    {
        ++firstDataLineNumber;
        if (IsCsvBlankLine(line)) continue;
        char detail[320];
        if (!ParseCsvPositionRow(line, &header, &first, detail, sizeof(detail)))
        {
            std::fprintf(stderr, "Malformed first CSV row (file line %lu): %s\n",
                         firstDataLineNumber, detail);
            std::fclose(inputFile);
            return 3;
        }
        firstFound = true;
        break;
    }
    if (!firstFound)
    {
        std::fprintf(stderr, "CSV has no data rows.\n");
        std::fclose(inputFile);
        return 1;
    }
    if (options.defaultWeek == 0 && !first.hasUtcDate)
    {
        std::fprintf(stderr,
            "CSV has no GNSS week. Supply --week N, or full UTC date-time and Leap in the CSV.\n");
        std::fclose(inputFile);
        return 1;
    }
    std::rewind(inputFile);
    if (std::fgets(line, sizeof(line), inputFile) == NULL)
    {
        std::fclose(inputFile); return 1;
    }
    RateDetectionResult rate;
    std::memset(&rate, 0, sizeof(rate));
    if (options.rateEnabled && options.autoDetectInputHz)
    {
        if (!DetectCsvInputRate(inputFile, &header, &rate))
        {
            std::fclose(inputFile); return 1;
        }
        options.inputHz = rate.inputHz;
        if (options.outputHz > options.inputHz ||
            options.inputHz % options.outputHz != 0)
        {
            std::fprintf(stderr, "CSV input is %u Hz; output %u Hz must divide it.\n",
                         options.inputHz, options.outputHz);
            std::fclose(inputFile); return 1;
        }
        options.decimationFactor = options.inputHz / options.outputHz;
    }
    std::rewind(inputFile);
    if (std::fgets(line, sizeof(line), inputFile) == NULL)
    {
        std::fclose(inputFile); return 1;
    }
    FILE *outputFile = std::fopen(options.outputPrefix, "wb");
    if (outputFile == NULL)
    {
        std::fprintf(stderr, "Cannot create INSPVA output: %s\n", options.outputPrefix);
        std::fclose(inputFile); return 1;
    }
    unsigned long inputCount = 0, outputCount = 0, invalidCount = 0, dropped = 0;
    unsigned int week = options.defaultWeek != 0 ? options.defaultWeek : first.utcDateWeek;
    bool hasPrevious = false;
    double previousTow = 0.0;
    bool failed = false;
    unsigned long csvFileLineNumber = 1;
    while (std::fgets(line, sizeof(line), inputFile) != NULL)
    {
        ++csvFileLineNumber;
        if (IsCsvBlankLine(line)) continue;
        CsvPositionRecord r;
        char detail[320];
        if (!ParseCsvPositionRow(line, &header, &r, detail, sizeof(detail)))
        {
            ++invalidCount;
            if (invalidCount <= 10)
                std::fprintf(stderr, "Malformed CSV row (file line %lu): %s\n",
                             csvFileLineNumber, detail);
            continue;
        }
        if (hasPrevious && r.tow - previousTow < -GNSS_WEEK_SECONDS / 2.0)
        {
            if (week == UINT_MAX) { failed = true; break; }
            ++week;
        }
        if (r.hasUtcDate && options.defaultWeek == 0)
            week = r.utcDateWeek;
        previousTow = r.tow;
        hasPrevious = true;
        const unsigned long index = inputCount++;
        if (index % options.decimationFactor != 0)
        {
            ++dropped;
            continue;
        }
        char output[SENTENCE_BUFFER_SIZE];
        if (!BuildInspvaFromCsv(&r, week, &options, output, sizeof(output)) ||
            !WriteSentence(outputFile, output))
        {
            failed = true; break;
        }
        ++outputCount;
    }
    const bool closeFailed = std::fclose(outputFile) != 0;
    std::fclose(inputFile);
    if (failed || closeFailed)
    {
        std::fprintf(stderr, "CSV conversion failed while writing output.\n");
        return 1;
    }
    std::printf("CSV -> INSPVA complete: %lu input records, %lu output records, "
                "%lu rate-dropped, %lu malformed.\n",
                inputCount, outputCount, dropped, invalidCount);
    if (options.rateEnabled)
        std::printf("Rate conversion: %u Hz -> %u Hz (keep every %u records)%s\n",
                    options.inputHz, options.outputHz, options.decimationFactor,
                    options.autoDetectInputHz ? " [auto]" : "");
    std::printf("Output: %s\n", options.outputPrefix);
    return invalidCount == 0 ? 0 : 3;
}

int main(int argc, char *argv[])
{
    ProgramOptions options;

    if (!ParseCommandLine(argc, argv, &options))
    {
        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--help") == 0 ||
                std::strcmp(argv[i], "-h") == 0)
                return 0;
        }
        return 1;
    }

    if (options.mode == PROGRAM_MODE_CSV_TO_INSPVA)
    {
        return RunCsvToInspva(&options);
    }

    if (options.mode ==
        PROGRAM_MODE_INSPVA_TO_BESTPOS)
    {
        return RunInspvaToBestPos(&options);
    }

    if (options.mode ==
        PROGRAM_MODE_INSPVAX_TO_INSPVA)
    {
        return RunInspvaxToInspva(&options);
    }

    if (options.mode ==
        PROGRAM_MODE_INSPVAX_TO_BESTPOS)
    {
        return RunInspvaxToBestPos(&options);
    }

    return RunPositionToAll(&options);
}
