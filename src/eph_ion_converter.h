#ifndef GNSSLOGCONVERTER_EPH_ION_CONVERTER_H_
#define GNSSLOGCONVERTER_EPH_ION_CONVERTER_H_

#include <cstddef>
#include <cstdint>

namespace gnsslog {

bool IsSupportedEphIonAsciiLine(const char* line);
bool IsSupportedEphIonBinaryMessageId(std::uint16_t message_id);

bool ConvertEphIonAsciiLineToBinary(const char* line,
                                    std::uint8_t** record,
                                    std::size_t* record_size,
                                    char* error_text,
                                    std::size_t error_text_size);

bool ConvertEphIonBinaryRecordToAscii(const std::uint8_t* record,
                                      std::size_t record_size,
                                      char** line,
                                      std::size_t* line_size,
                                      char* error_text,
                                      std::size_t error_text_size);

}  // namespace gnsslog

#endif  // GNSSLOGCONVERTER_EPH_ION_CONVERTER_H_
