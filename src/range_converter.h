#ifndef GNSSLOGCONVERTER_RANGE_CONVERTER_H_
#define GNSSLOGCONVERTER_RANGE_CONVERTER_H_

#include <cstddef>
#include <cstdint>

namespace gnsslog {

bool IsSupportedRangeAsciiLine(const char* line);

bool ConvertRangeAsciiLineToBinary(const char* line,
                                   std::uint8_t** record,
                                   std::size_t* record_size,
                                   char* error_text,
                                   std::size_t error_text_size);

bool ConvertRangeBinaryRecordToAscii(const std::uint8_t* record,
                                     std::size_t record_size,
                                     char** line,
                                     std::size_t* line_size,
                                     char* error_text,
                                     std::size_t error_text_size);

void FreeConvertedBuffer(void* buffer);

}  // namespace gnsslog

#endif  // GNSSLOGCONVERTER_RANGE_CONVERTER_H_
