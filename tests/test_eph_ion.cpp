#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "byte_io.h"
#include "eph_ion_converter.h"
#include "novatel/novatel_protocol.h"
#include "range_converter.h"

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::exit(1);
    }
}

void ConvertAndDecode(const char* line,
                      std::uint16_t expected_id,
                      std::uint16_t expected_payload_size,
                      std::uint8_t** record,
                      std::size_t* record_size,
                      gnsslog::novatel::BinaryHeader* header)
{
    char error[256] = {};
    Require(gnsslog::ConvertEphIonAsciiLineToBinary(line,
                                                     record,
                                                     record_size,
                                                     error,
                                                     sizeof(error)), error);
    Require(gnsslog::novatel::DecodeBinaryHeader(*record, *record_size, header),
            "decode binary header");
    Require(header->message_id == expected_id, "target message ID");
    Require(header->message_length == expected_payload_size, "target payload length");
    Require(gnsslog::novatel::ValidateBinaryRecordCrc(*record, *record_size), "target CRC");
}

void TestGpsIon()
{
    const char* const source =
        "#GPSIONA,90,GPS,FINE,2190,371250000,0,0,18,21;"
        "1.490116119384766e-08,-7.450580596923828e-09,-5.960464477539062e-08,"
        "1.192092895507812e-07,1.290240000000000e+05,-1.966080000000000e+05,"
        "6.553600000000000e+04,3.276800000000000e+05,0,0,0,0*c5974f70";
    std::uint8_t* record = NULL;
    std::size_t size = 0U;
    gnsslog::novatel::BinaryHeader header = {};
    ConvertAndDecode(source, 8U, 108U, &record, &size, &header);
    const std::uint8_t* payload = record + gnsslog::novatel::kBinaryHeaderSize;
    double a0 = 0.0;
    std::uint32_t utc_wn = 99U;
    Require(gnsslog::ReadDoubleLE(payload, 108U, 0U, &a0), "GPSION a0 read");
    Require(std::fabs(a0 - 1.490116119384766e-08) < 1.0e-20, "GPSION a0 mapping");
    Require(gnsslog::ReadU32LE(payload, 108U, 64U, &utc_wn) && utc_wn == 0U,
            "GPSION unavailable UTC fields are zero");

    char* ascii = NULL;
    std::size_t ascii_size = 0U;
    char error[256] = {};
    Require(gnsslog::ConvertEphIonBinaryRecordToAscii(record, size, &ascii, &ascii_size,
                                                       error, sizeof(error)), error);
    Require(std::strncmp(ascii, "#IONUTCA,THISPORT,", 18U) == 0, "GPSION target ASCII name");
    gnsslog::FreeConvertedBuffer(ascii);
    gnsslog::FreeConvertedBuffer(record);
}

void TestBd2EphemPrnMapping()
{
    const char* const source =
        "#BDSEPHA,97,GPS,FINE,2190,362675000,0,0,18,5;"
        "60,360000.0,0,1,1,2190,2190,360000.0,4.216441036e+07,-4.103028050e-09,"
        "2.042808580e+00,3.8967351429e-05,2.4660025037e+00,-1.457566395e-05,"
        "-2.235500142e-05,6.85031250e+02,-4.52843750e+02,1.438893378e-07,"
        "-1.206062734e-07,1.2597663760e-01,1.132190017e-10,-1.993009969e+00,"
        "5.03270963e-09,1,360000.0,4.980000000e-08,4.980000000e-08,-1.45519e-07,"
        "8.26006e-14,0.00000e+00,TRUE,7.291643104e-05,4.00000000e+00*493bb7fb";
    std::uint8_t* record = NULL;
    std::size_t size = 0U;
    gnsslog::novatel::BinaryHeader header = {};
    ConvertAndDecode(source, 1047U, 232U, &record, &size, &header);
    std::uint32_t prn = 0U;
    Require(gnsslog::ReadU32LE(record + gnsslog::novatel::kBinaryHeaderSize,
                               232U, 0U, &prn) && prn == 220U,
            "BDSEPH PRN 60 -> BD2EPHEM PRN 220");
    gnsslog::FreeConvertedBuffer(record);
}

void TestQzssEphemMapping()
{
    const char* const source =
        "#QZSSEPHA,78,GPS,FINE,2262,368756700,0,0,18,16;"
        "4,368730.0,0,185,185,2262,2262,370800.0,4.216498367e+07,2.569749898e-09,"
        "1.905190738e+00,7.5245622196e-02,-1.5526664328e+00,1.514703035e-05,"
        "3.799796104e-06,-3.61250000e+01,4.70875000e+02,3.501772881e-07,"
        "2.438202500e-06,6.1294064513e-01,4.364467512e-10,-2.354107999e+00,"
        "-1.81507561e-09,953,370800.0,-3.725290298e-09,9.4612129e-05,"
        "2.2737368e-13,0.0000000e+00,FALSE,7.292162191e-05,7.84000000e+00*7c79248b";
    std::uint8_t* record = NULL;
    std::size_t size = 0U;
    gnsslog::novatel::BinaryHeader header = {};
    ConvertAndDecode(source, 1336U, 228U, &record, &size, &header);
    const std::uint8_t* payload = record + gnsslog::novatel::kBinaryHeaderSize;
    std::uint32_t prn = 0U;
    std::uint8_t fit = 1U;
    Require(gnsslog::ReadU32LE(payload, 228U, 0U, &prn) && prn == 196U,
            "QZSS PRN 4 -> 196");
    Require(gnsslog::ReadU8(payload, 228U, 224U, &fit) && fit == 0U,
            "QZSS unavailable fit interval defaults to zero");
    gnsslog::FreeConvertedBuffer(record);
}

void TestGalIono()
{
    const char* const source =
        "#GALIONA,96,GPS,FINE,2218,465990000,0,0,18,21;"
        "1.240000000000000e+02,4.921875000000000e-01,1.293945312500000e-02,"
        "0,0,0,0,0,0*9e349a84";
    std::uint8_t* record = NULL;
    std::size_t size = 0U;
    gnsslog::novatel::BinaryHeader header = {};
    ConvertAndDecode(source, 1127U, 29U, &record, &size, &header);
    double a0 = 0.0;
    Require(gnsslog::ReadDoubleLE(record + gnsslog::novatel::kBinaryHeaderSize,
                                  29U, 0U, &a0) && a0 == 124.0,
            "GALION -> GALIONO coefficients");
    gnsslog::FreeConvertedBuffer(record);
}

void TestIrnssEphem()
{
    const char* const source =
        "#IRNSSEPHA,87,GPS,FINE,2305,116273000,0,0,18,31;"
        "2,9685.0,0,193,0,2305,0,115536.0,4.216456644e+07,4.968778398e-09,"
        "-1.455813652e+00,2.0113651408e-03,3.0523021218e+00,2.138316631e-05,"
        "-2.254918218e-05,7.77500000e+02,6.59937500e+02,-2.346932888e-07,"
        "-2.123415470e-07,5.0866932453e-01,4.564475843e-10,1.506952289e+00,"
        "-4.93877715e-09,0,115536.0,-1.862645149e-09,6.0655642e-05,"
        "2.3760549e-11,0.0000000e+00,0,7.292510329e-05,4.0*298210c8";
    std::uint8_t* record = NULL;
    std::size_t size = 0U;
    gnsslog::novatel::BinaryHeader header = {};
    ConvertAndDecode(source, 2123U, 204U, &record, &size, &header);
    const std::uint8_t* payload = record + gnsslog::novatel::kBinaryHeaderSize;
    std::uint32_t week = 0U, alert = 1U, autonav = 1U;
    double root_a = 0.0;
    Require(gnsslog::ReadU32LE(payload, 204U, 4U, &week) && week == 1281U,
            "IRNSS GPS week -> NavIC week");
    Require(gnsslog::ReadDoubleLE(payload, 204U, 152U, &root_a) &&
            std::fabs(root_a - std::sqrt(4.216456644e+07)) < 1.0e-9,
            "IRNSS A -> NavIC RootA");
    Require(gnsslog::ReadU32LE(payload, 204U, 196U, &alert) && alert == 0U,
            "IRNSS alert flag");
    Require(gnsslog::ReadU32LE(payload, 204U, 200U, &autonav) && autonav == 0U,
            "IRNSS AutoNav flag");
    gnsslog::FreeConvertedBuffer(record);
}

void TestBd3Ephem()
{
    const char* const source =
        "#BD3EPHA,77,GPS,FINE,2211,180091000,0,0,18,4;"
        "44,0,3,15,21,21,2211,2211,176400.0,176400.0,-1.423828125e+01,"
        "1.108884811e-02,3.726583799e-09,-1.069685670e-13,1.309681137e+00,"
        "8.019023808e-04,6.109550176e-01,2.244487405e-07,8.259899914e-06,"
        "1.940156250e+02,6.187500000e+00,1.210719347e-08,7.450580597e-09,"
        "9.593903595e-01,-4.500187451e-11,1.952617584e+00,-6.803497679e-09,"
        "176400.0,-2.153683454e-09,-1.199077815e-08,0.000000000e+00,"
        "0.000000000e+00,0.000000000e+00,-2.910383046e-10,6.693656906e-04,"
        "1.219113699e-11,0.000000000e+00,588,0,27,0,7,0,0,1*b90d9566";
    std::uint8_t* record = NULL;
    std::size_t size = 0U;
    gnsslog::novatel::BinaryHeader header = {};
    ConvertAndDecode(source, 2372U, 220U, &record, &size, &header);
    const std::uint8_t* payload = record + gnsslog::novatel::kBinaryHeaderSize;
    std::uint32_t week = 0U, status = 0U, prn = 0U;
    Require(gnsslog::ReadU32LE(payload, 220U, 0U, &prn) && prn == 44U,
            "BD3 PRN");
    Require(gnsslog::ReadU32LE(payload, 220U, 4U, &week) && week == 855U,
            "BD3 GPS week -> BDT week");
    Require(gnsslog::ReadU32LE(payload, 220U, 8U, &status) && status == (15U << 5U),
            "BD3 health/SISMAI -> satellite status");
    gnsslog::FreeConvertedBuffer(record);
}

void TestUnsupportedBd3Ion()
{
    Require(!gnsslog::IsSupportedEphIonAsciiLine("#BD3IONA,50,GPS,FINE,2200,0,0,0,18,0;0"),
            "BD3ION remains silently unsupported without verified NovAtel equivalent");
}

}  // namespace

int main()
{
    TestGpsIon();
    TestBd2EphemPrnMapping();
    TestQzssEphemMapping();
    TestGalIono();
    TestIrnssEphem();
    TestBd3Ephem();
    TestUnsupportedBd3Ion();
    std::printf("EPH/ION tests passed.\n");
    return 0;
}
