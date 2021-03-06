// Copyright (c) 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Contains utils for reading, writing and debug printing bit streams.

#ifndef LIBSPIRV_UTIL_BIT_STREAM_H_
#define LIBSPIRV_UTIL_BIT_STREAM_H_

#include <bitset>
#include <cstdint>
#include <string>
#include <sstream>
#include <vector>

namespace spvutils {

// Terminology:
// Bits - usually used for a uint64 word, first bit is the lowest.
// Stream - std::string of '0' and '1', read left-to-right,
//          i.e. first bit is at the front and not at the end as in
//          std::bitset::to_string().
// Bitset - std::bitset corresponding to uint64 bits and to reverse(stream).

// Converts number of bits to a respective number of chunks of size N.
// For example NumBitsToNumWords<8> returns how many bytes are needed to store
// |num_bits|.
template <size_t N>
inline size_t NumBitsToNumWords(size_t num_bits) {
  return (num_bits + (N - 1)) / N;
}

// Returns value of the same type as |in|, where all but the first |num_bits|
// are set to zero.
template <typename T>
inline T GetLowerBits(T in, size_t num_bits) {
  return sizeof(T) * 8 == num_bits ? in : in & T((T(1) << num_bits) - T(1));
}

// Encodes signed integer as unsigned in zigzag order:
//  0 -> 0
// -1 -> 1
//  1 -> 2
// -2 -> 3
//  2 -> 4
// Motivation: -1 is 0xFF...FF what doesn't work very well with
// WriteVariableWidth which prefers to have as many 0 bits as possible.
inline uint64_t EncodeZigZag(int64_t val) {
  return (val << 1) ^ (val >> 63);
}

// Decodes signed integer encoded with EncodeZigZag.
inline int64_t DecodeZigZag(uint64_t val) {
  if (val & 1) {
    // Negative.
    // 1 -> -1
    // 3 -> -2
    // 5 -> -3
    return -1 - (val >> 1);
  } else {
    // Non-negative.
    // 0 -> 0
    // 2 -> 1
    // 4 -> 2
    return val >> 1;
  }
}

// Encodes signed integer as unsigned. This is a generalized version of
// EncodeZigZag, designed to favor small positive numbers.
// Values are transformed in blocks of 2^|block_exponent|.
// If |block_exponent| is zero, then this degenerates into normal EncodeZigZag.
// Example when |block_exponent| is 1 (return value is the index):
// 0, 1, -1, -2, 2, 3, -3, -4, 4, 5, -5, -6, 6, 7, -7, -8
// Example when |block_exponent| is 2:
// 0, 1, 2, 3, -1, -2, -3, -4, 4, 5, 6, 7, -5, -6, -7, -8
inline uint64_t EncodeZigZag(int64_t val, size_t block_exponent) {
  assert(block_exponent < 64);
  const uint64_t uval = static_cast<uint64_t>(val >= 0 ? val : -val - 1);
  const uint64_t block_num = ((uval >> block_exponent) << 1) + (val >= 0 ? 0 : 1);
  const uint64_t pos = GetLowerBits(uval, block_exponent);
  return (block_num << block_exponent) + pos;
}

// Decodes signed integer encoded with EncodeZigZag. |block_exponent| must be
// the same.
inline int64_t DecodeZigZag(uint64_t val, size_t block_exponent) {
  assert(block_exponent < 64);
  const uint64_t block_num = val >> block_exponent;
  const uint64_t pos = GetLowerBits(val, block_exponent);
  if (block_num & 1) {
    // Negative.
    return -1LL - ((block_num >> 1) << block_exponent) - pos;
  } else {
    // Positive.
    return ((block_num >> 1) << block_exponent) + pos;
  }
}

// Converts |buffer| to a stream of '0' and '1'.
template <typename T>
std::string BufferToStream(const std::vector<T>& buffer) {
  std::stringstream ss;
  for (auto it = buffer.begin(); it != buffer.end(); ++it) {
    std::string str = std::bitset<sizeof(T) * 8>(*it).to_string();
    // Strings generated by std::bitset::to_string are read right to left.
    // Reversing to left to right.
    std::reverse(str.begin(), str.end());
    ss << str;
  }
  return ss.str();
}

// Converts a left-to-right input string of '0' and '1' to a buffer of |T|
// words.
template <typename T>
std::vector<T> StreamToBuffer(std::string str) {
  // The input string is left-to-right, the input argument of std::bitset needs
  // to right-to-left. Instead of reversing tokens, reverse the entire string
  // and iterate tokens from end to begin.
  std::reverse(str.begin(), str.end());
  const int word_size = static_cast<int>(sizeof(T) * 8);
  const int str_length = static_cast<int>(str.length());
  std::vector<T> buffer;
  buffer.reserve(NumBitsToNumWords<sizeof(T)>(str.length()));
  for (int index = str_length - word_size; index >= 0; index -= word_size) {
    buffer.push_back(static_cast<T>(std::bitset<sizeof(T) * 8>(
        str, index, word_size).to_ullong()));
  }
  const size_t suffix_length = str.length() % word_size;
  if (suffix_length != 0) {
    buffer.push_back(static_cast<T>(std::bitset<sizeof(T) * 8>(
        str, 0, suffix_length).to_ullong()));
  }
  return buffer;
}

// Adds '0' chars at the end of the string until the size is a multiple of N.
template <size_t N>
inline std::string PadToWord(std::string&& str) {
  const size_t tail_length = str.size() % N;
  if (tail_length != 0)
    str += std::string(N - tail_length, '0');
  return str;
}

// Adds '0' chars at the end of the string until the size is a multiple of N.
template <size_t N>
inline std::string PadToWord(const std::string& str) {
  return PadToWord<N>(std::string(str));
}

// Converts a left-to-right stream of bits to std::bitset.
template <size_t N>
inline std::bitset<N> StreamToBitset(std::string str) {
  std::reverse(str.begin(), str.end());
  return std::bitset<N>(str);
}

// Converts first |num_bits| of std::bitset to a left-to-right stream of bits.
template <size_t N>
inline std::string BitsetToStream(const std::bitset<N>& bits, size_t num_bits = N) {
  std::string str = bits.to_string().substr(N - num_bits);
  std::reverse(str.begin(), str.end());
  return str;
}

// Converts a left-to-right stream of bits to uint64.
inline uint64_t StreamToBits(std::string str) {
  std::reverse(str.begin(), str.end());
  return std::bitset<64>(str).to_ullong();
}

// Converts first |num_bits| stored in uint64 to a left-to-right stream of bits.
inline std::string BitsToStream(uint64_t bits, size_t num_bits = 64) {
  std::bitset<64> bitset(bits);
  return BitsetToStream(bitset, num_bits);
}

// Base class for writing sequences of bits.
class BitWriterInterface {
 public:
  BitWriterInterface() {}
  virtual ~BitWriterInterface() {}

  // Writes lower |num_bits| in |bits| to the stream.
  // |num_bits| must be no greater than 64.
  virtual void WriteBits(uint64_t bits, size_t num_bits) = 0;

  // Writes left-to-right string of '0' and '1' to stream.
  // String length must be no greater than 64.
  // Note: "01" will be writen as 0x2, not 0x1. The string doesn't represent
  // numbers but a stream of bits in the order they come from encoder.
  virtual void WriteStream(const std::string& bits) {
    WriteBits(StreamToBits(bits), bits.length());
  }

  // Writes lower |num_bits| in |bits| to the stream.
  // |num_bits| must be no greater than 64.
  template <size_t N>
  void WriteBitset(const std::bitset<N>& bits, size_t num_bits = N) {
    WriteBits(bits.to_ullong(), num_bits);
  }

  // Writes |val| in chunks of size |chunk_length| followed by a signal bit:
  // 0 - no more chunks to follow
  // 1 - more chunks to follow
  // for example 255 is encoded into 1111 1 1111 0 for chunk length 4.
  // The last chunk can be truncated and signal bit omitted, if the entire
  // payload (for example 16 bit for uint16_t has already been written).
  void WriteVariableWidthU64(uint64_t val, size_t chunk_length);
  void WriteVariableWidthU32(uint32_t val, size_t chunk_length);
  void WriteVariableWidthU16(uint16_t val, size_t chunk_length);
  void WriteVariableWidthU8(uint8_t val, size_t chunk_length);
  void WriteVariableWidthS64(
      int64_t val, size_t chunk_length, size_t zigzag_exponent);
  void WriteVariableWidthS32(
      int32_t val, size_t chunk_length, size_t zigzag_exponent);
  void WriteVariableWidthS16(
      int16_t val, size_t chunk_length, size_t zigzag_exponent);
  void WriteVariableWidthS8(
      int8_t val, size_t chunk_length, size_t zigzag_exponent);

  // Returns number of bits written.
  virtual size_t GetNumBits() const = 0;

  // Provides direct access to the buffer data if implemented.
  virtual const uint8_t* GetData() const {
    return nullptr;
  }

  // Returns buffer size in bytes.
  size_t GetDataSizeBytes() const {
    return NumBitsToNumWords<8>(GetNumBits());
  }

  // Generates and returns byte array containing written bits.
  virtual std::vector<uint8_t> GetDataCopy() const = 0;

  BitWriterInterface(const BitWriterInterface&) = delete;
  BitWriterInterface& operator=(const BitWriterInterface&) = delete;
};

// This class is an implementation of BitWriterInterface, using
// std::vector<uint64_t> to store written bits.
class BitWriterWord64 : public BitWriterInterface {
 public:
  explicit BitWriterWord64(size_t reserve_bits = 64);

  void WriteBits(uint64_t bits, size_t num_bits) override;

  size_t GetNumBits() const override {
    return end_;
  }

  const uint8_t* GetData() const override {
    return reinterpret_cast<const uint8_t*>(buffer_.data());
  }

  std::vector<uint8_t> GetDataCopy() const override {
    return std::vector<uint8_t>(GetData(), GetData() + GetDataSizeBytes());
  }

  // Returns written stream as std::string, padded with zeroes so that the
  // length is a multiple of 64.
  std::string GetStreamPadded64() const {
    return BufferToStream(buffer_);
  }

 private:
  std::vector<uint64_t> buffer_;
  // Total number of bits written so far. Named 'end' as analogy to std::end().
  size_t end_;
};

// Base class for reading sequences of bits.
class BitReaderInterface {
 public:
  BitReaderInterface() {}
  virtual ~BitReaderInterface() {}

  // Reads |num_bits| from the stream, stores them in |bits|.
  // Returns number of read bits. |num_bits| must be no greater than 64.
  virtual size_t ReadBits(uint64_t* bits, size_t num_bits) = 0;

  // Reads |num_bits| from the stream, stores them in |bits|.
  // Returns number of read bits. |num_bits| must be no greater than 64.
  template <size_t N>
  size_t ReadBitset(std::bitset<N>* bits, size_t num_bits = N) {
    uint64_t val = 0;
    size_t num_read = ReadBits(&val, num_bits);
    if (num_read) {
      *bits = std::bitset<N>(val);
    }
    return num_read;
  }

  // Reads |num_bits| from the stream, returns string in left-to-right order.
  // The length of the returned string may be less than |num_bits| if end was
  // reached.
  std::string ReadStream(size_t num_bits) {
    uint64_t bits = 0;
    size_t num_read = ReadBits(&bits, num_bits);
    return BitsToStream(bits, num_read);
  }

  // These two functions define 'hard' and 'soft' EOF.
  //
  // Returns true if the end of the buffer was reached.
  virtual bool ReachedEnd() const = 0;
  // Returns true if we reached the end of the buffer or are nearing it and only
  // zero bits are left to read. Implementations of this function are allowed to
  // commit a "false negative" error if the end of the buffer was not reached,
  // i.e. it can return false even if indeed only zeroes are left.
  // It is assumed that the consumer expects that
  // the buffer stream ends with padding zeroes, and would accept this as a
  // 'soft' EOF. Implementations of this class do not necessarily need to
  // implement this, default behavior can simply delegate to ReachedEnd().
  virtual bool OnlyZeroesLeft() const {
    return ReachedEnd();
  }

  // Reads value encoded with WriteVariableWidthXXX (see BitWriterInterface).
  // Reader and writer must use the same |chunk_length| and variable type.
  // Returns true on success, false if the bit stream ends prematurely.
  bool ReadVariableWidthU64(uint64_t* val, size_t chunk_length);
  bool ReadVariableWidthU32(uint32_t* val, size_t chunk_length);
  bool ReadVariableWidthU16(uint16_t* val, size_t chunk_length);
  bool ReadVariableWidthU8(uint8_t* val, size_t chunk_length);
  bool ReadVariableWidthS64(
      int64_t* val, size_t chunk_length, size_t zigzag_exponent);
  bool ReadVariableWidthS32(
      int32_t* val, size_t chunk_length, size_t zigzag_exponent);
  bool ReadVariableWidthS16(
      int16_t* val, size_t chunk_length, size_t zigzag_exponent);
  bool ReadVariableWidthS8(
      int8_t* val, size_t chunk_length, size_t zigzag_exponent);

  BitReaderInterface(const BitReaderInterface&) = delete;
  BitReaderInterface& operator=(const BitReaderInterface&) = delete;
};

// This class is an implementation of BitReaderInterface which accepts both
// uint8_t and uint64_t buffers as input. uint64_t buffers are consumed and
// owned. uint8_t buffers are copied.
class BitReaderWord64 : public BitReaderInterface {
 public:
  // Consumes and owns the buffer.
  explicit BitReaderWord64(std::vector<uint64_t>&& buffer);

  // Copies the buffer and casts it to uint64.
  // Consuming the original buffer and casting it to uint64 is difficult,
  // as it would potentially cause data misalignment and poor performance.
  explicit BitReaderWord64(const std::vector<uint8_t>& buffer);

  size_t ReadBits(uint64_t* bits, size_t num_bits) override;
  bool ReachedEnd() const override;
  bool OnlyZeroesLeft() const override;

  BitReaderWord64() = delete;
 private:
  const std::vector<uint64_t> buffer_;
  size_t pos_;
};

}  // namespace spvutils

#endif  // LIBSPIRV_UTIL_BIT_STREAM_H_
