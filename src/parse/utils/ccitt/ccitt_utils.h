//-*-C++-*-

#ifndef CCITT_UTILS_H
#define CCITT_UTILS_H

// ---------------------------------------------------------------------------
// CCITT Group 3 (T.4) and Group 4 (T.6) decoder
//
// Handles /CCITTFaxDecode streams from PDF XObjects.
// Produces one 8-bit byte per pixel: 0 = black, 255 = white.
//
// Supported /K values (from /DecodeParms):
//   K < 0  : Group 4 / T.6 (pure 2-D) — the common PDF case
//   K = 0  : Group 3 / T.4 1-D (MH), with per-row EOL markers
//   K > 0  : treated as Group 3 1-D for simplicity
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstdint>
#include <vector>

#ifndef LOGURU_WITH_STREAMS
#define LOGURU_WITH_STREAMS 1
#endif
#include <loguru.hpp>

namespace pdflib
{
namespace ccitt
{

// ============================================================
// BitReader — reads individual bits MSB-first from a byte buffer
// ============================================================

class BitReader
{
public:
  BitReader(const uint8_t* data, size_t size);

  // Returns the next bit (0 or 1), or -1 when the buffer is exhausted.
  int  read_bit();
  bool at_end() const;

private:
  const uint8_t* data_;
  size_t         size_;
  size_t         byte_;   // index of current byte
  int            bit_;    // bit offset within current byte (0 = MSB)
};

// ============================================================
// HuffTree — simple binary Huffman decode tree
// ============================================================

struct HNode
{
  int child[2];
  int value;
};

class HuffTree
{
public:
  HuffTree();

  // Insert one code.  `code` is the bit pattern MSB-first, `nbits` is
  // the length, `value` is the symbol (must be >= 0).
  void insert(uint32_t code, int nbits, int value);

  // Feed one bit into the tree.  `state` must be 0 at the start of each
  // symbol and is updated by the call.
  // Returns: the decoded symbol (>= 0) when a leaf is reached and resets
  //          state to 0; -1 if more bits are needed; -2 on error.
  int step(int& state, int bit) const;

private:
  int add_node();

  std::vector<HNode> nodes_;
};

// ============================================================
// Canonical T.4 Huffman tables (white and black)
// Each returns a reference to a function-local static instance.
// ============================================================

const HuffTree& white_tree();
const HuffTree& black_tree();

// ============================================================
// G4 mode tree (T.6 Table 4)
// Values:  0=Pass, 1=H, 2=V0, 3=VR1, 4=VR2, 5=VR3,
//          6=VL1, 7=VL2, 8=VL3
// ============================================================

const HuffTree& mode_tree();

// ============================================================
// Decode one complete MH (1-D) run of the given color.
// Returns the run length (>= 0) or -1 on error.
// ============================================================

int decode_1d_run(BitReader& br, int color);

// ============================================================
// Decode one 1-D row.  Pixels are stored as 0=white, 1=black.
// Returns true on success.
// ============================================================

bool decode_1d_row(BitReader& br, std::vector<uint8_t>& row, int width);

// ============================================================
// G4 reference-line helpers
// ============================================================

// b1 = first changing element in `ref` after position `a0` whose color
//      differs from `a0col`.  Returns `width` when none is found.
int find_b1(const std::vector<uint8_t>& ref, int a0, int a0col, int width);

// b2 = first changing element in `ref` after `b1`.
//      Returns `width` when none is found.
int find_b2(const std::vector<uint8_t>& ref, int b1, int width);

// ============================================================
// Decode one Group 4 (T.6) row.
// `ref` = previous decoded row (pixel values 0/1); all-white for row 0.
// `cur` = output pixel buffer (size `width`), pre-zeroed.
// Returns true on success.
// ============================================================

bool decode_g4_row(BitReader& br,
                   const std::vector<uint8_t>& ref,
                   std::vector<uint8_t>& cur,
                   int width);

// ============================================================
// Main decode entry point
//
// raw_data / raw_size : compressed CCITT bytes
// width, height       : image dimensions in pixels
// k                   : /K parameter from /DecodeParms (-1 for Group 4)
//
// Returns width*height bytes (8-bit, top-to-bottom, left-to-right):
//   0 = black pixel, 255 = white pixel.
// Returns an empty vector on failure.
// ============================================================

std::vector<uint8_t> decode(const uint8_t* raw_data,
                             size_t         raw_size,
                             int            width,
                             int            height,
                             int            k = -1);

// ===========================================================
// Implementations
// ===========================================================

// --- BitReader ---

inline BitReader::BitReader(const uint8_t* data, size_t size)
  : data_(data), size_(size), byte_(0), bit_(0)
{}

inline int BitReader::read_bit()
{
  if(byte_ >= size_)
    {
      return -1;
    }
  int b = (data_[byte_] >> (7 - bit_)) & 1;
  if(++bit_ == 8)
    {
      bit_ = 0;
      ++byte_;
    }
  return b;
}

inline bool BitReader::at_end() const
{
  return byte_ >= size_;
}

// --- HuffTree ---

inline HuffTree::HuffTree()
{
  nodes_.push_back({ {-1, -1}, -1 });
}

inline int HuffTree::add_node()
{
  nodes_.push_back({ {-1, -1}, -1 });
  return static_cast<int>(nodes_.size()) - 1;
}

inline void HuffTree::insert(uint32_t code, int nbits, int value)
{
  int cur = 0;
  for(int b = nbits - 1; b >= 0; b--)
    {
      int bit = (code >> b) & 1;
      if(nodes_[cur].child[bit] < 0)
        {
          nodes_[cur].child[bit] = add_node();
        }
      cur = nodes_[cur].child[bit];
    }
  nodes_[cur].value = value;
}

inline int HuffTree::step(int& state, int bit) const
{
  int next = nodes_[state].child[bit];
  if(next < 0)
    {
      state = 0;
      return -2;
    }
  state = next;
  if(nodes_[state].value >= 0)
    {
      int v  = nodes_[state].value;
      state  = 0;
      return v;
    }
  return -1;
}

// --- Huffman tables ---

inline const HuffTree& white_tree()
{
  static const HuffTree t = []()
    {
      HuffTree h;
      // --- Terminating codes (runs 0-63) ---
      h.insert(0b00110101,  8,  0);  h.insert(0b000111,    6,  1);
      h.insert(0b0111,      4,  2);  h.insert(0b1000,      4,  3);
      h.insert(0b1011,      4,  4);  h.insert(0b1100,      4,  5);
      h.insert(0b1110,      4,  6);  h.insert(0b1111,      4,  7);
      h.insert(0b10011,     5,  8);  h.insert(0b10100,     5,  9);
      h.insert(0b00111,     5, 10);  h.insert(0b01000,     5, 11);
      h.insert(0b001000,    6, 12);  h.insert(0b000011,    6, 13);
      h.insert(0b110100,    6, 14);  h.insert(0b110101,    6, 15);
      h.insert(0b101010,    6, 16);  h.insert(0b101011,    6, 17);
      h.insert(0b0100111,   7, 18);  h.insert(0b0001100,   7, 19);
      h.insert(0b0001000,   7, 20);  h.insert(0b0010111,   7, 21);
      h.insert(0b0000011,   7, 22);  h.insert(0b0000100,   7, 23);
      h.insert(0b0101000,   7, 24);  h.insert(0b0101011,   7, 25);
      h.insert(0b0010011,   7, 26);  h.insert(0b0100100,   7, 27);
      h.insert(0b0011000,   7, 28);
      h.insert(0b00000010,  8, 29);  h.insert(0b00000011,  8, 30);
      h.insert(0b00011010,  8, 31);  h.insert(0b00011011,  8, 32);
      h.insert(0b00010010,  8, 33);  h.insert(0b00010011,  8, 34);
      h.insert(0b00010100,  8, 35);  h.insert(0b00010101,  8, 36);
      h.insert(0b00010110,  8, 37);  h.insert(0b00010111,  8, 38);
      h.insert(0b00101000,  8, 39);  h.insert(0b00101001,  8, 40);
      h.insert(0b00101010,  8, 41);  h.insert(0b00101011,  8, 42);
      h.insert(0b00101100,  8, 43);  h.insert(0b00101101,  8, 44);
      h.insert(0b00000100,  8, 45);  h.insert(0b00000101,  8, 46);
      h.insert(0b00001010,  8, 47);  h.insert(0b00001011,  8, 48);
      h.insert(0b01010010,  8, 49);  h.insert(0b01010011,  8, 50);
      h.insert(0b01010100,  8, 51);  h.insert(0b01010101,  8, 52);
      h.insert(0b00100100,  8, 53);  h.insert(0b00100101,  8, 54);
      h.insert(0b01011000,  8, 55);  h.insert(0b01011001,  8, 56);
      h.insert(0b01011010,  8, 57);  h.insert(0b01011011,  8, 58);
      h.insert(0b01001010,  8, 59);  h.insert(0b01001011,  8, 60);
      h.insert(0b00110010,  8, 61);  h.insert(0b00110011,  8, 62);
      h.insert(0b00110100,  8, 63);
      // --- White make-up codes ---
      h.insert(0b11011,      5,   64);  h.insert(0b10010,      5,  128);
      h.insert(0b010111,     6,  192);  h.insert(0b0110111,    7,  256);
      h.insert(0b00110110,   8,  320);  h.insert(0b00110111,   8,  384);
      h.insert(0b01100100,   8,  448);  h.insert(0b01100101,   8,  512);
      h.insert(0b01101000,   8,  576);  h.insert(0b01100111,   8,  640);
      h.insert(0b011001100,  9,  704);  h.insert(0b011001101,  9,  768);
      h.insert(0b011010010,  9,  832);  h.insert(0b011010011,  9,  896);
      h.insert(0b011010100,  9,  960);  h.insert(0b011010101,  9, 1024);
      h.insert(0b011010110,  9, 1088);  h.insert(0b011010111,  9, 1152);
      h.insert(0b011011000,  9, 1216);  h.insert(0b011011001,  9, 1280);
      h.insert(0b011011010,  9, 1344);  h.insert(0b011011011,  9, 1408);
      h.insert(0b010011000,  9, 1472);  h.insert(0b010011001,  9, 1536);
      h.insert(0b010011010,  9, 1600);  h.insert(0b011000,     6, 1664);
      h.insert(0b010011011,  9, 1728);
      // --- Extended make-up codes (shared white/black) ---
      h.insert(0b00000001000,   11, 1792);
      h.insert(0b00000001100,   11, 1856);
      h.insert(0b00000001101,   11, 1920);
      h.insert(0b000000010010,  12, 1984);
      h.insert(0b000000010011,  12, 2048);
      h.insert(0b000000010100,  12, 2112);
      h.insert(0b000000010101,  12, 2176);
      h.insert(0b000000010110,  12, 2240);
      h.insert(0b000000010111,  12, 2304);
      h.insert(0b000000011100,  12, 2368);
      h.insert(0b000000011101,  12, 2432);
      h.insert(0b000000011110,  12, 2496);
      h.insert(0b000000011111,  12, 2560);
      return h;
    }();
  return t;
}

inline const HuffTree& black_tree()
{
  static const HuffTree t = []()
    {
      HuffTree h;
      // --- Terminating codes (runs 0-63) ---
      h.insert(0b0000110111,    10,  0);
      h.insert(0b010,            3,  1);  h.insert(0b11,             2,  2);
      h.insert(0b10,             2,  3);  h.insert(0b011,            3,  4);
      h.insert(0b0011,           4,  5);  h.insert(0b0010,           4,  6);
      h.insert(0b00011,          5,  7);  h.insert(0b000101,         6,  8);
      h.insert(0b000100,         6,  9);  h.insert(0b0000100,        7, 10);
      h.insert(0b0000101,        7, 11);  h.insert(0b0000111,        7, 12);
      h.insert(0b00000100,       8, 13);  h.insert(0b00000111,       8, 14);
      h.insert(0b000011000,      9, 15);
      h.insert(0b0000010111,    10, 16);  h.insert(0b0000011000,    10, 17);
      h.insert(0b0000001000,    10, 18);
      h.insert(0b00001100111,   11, 19);  h.insert(0b00001101000,   11, 20);
      h.insert(0b00001101100,   11, 21);  h.insert(0b00000110111,   11, 22);
      h.insert(0b00000101000,   11, 23);  h.insert(0b00000010111,   11, 24);
      h.insert(0b00000011000,   11, 25);
      h.insert(0b000011001010,  12, 26);  h.insert(0b000011001011,  12, 27);
      h.insert(0b000011001100,  12, 28);  h.insert(0b000011001101,  12, 29);
      h.insert(0b000001101000,  12, 30);  h.insert(0b000001101001,  12, 31);
      h.insert(0b000001101010,  12, 32);  h.insert(0b000001101011,  12, 33);
      h.insert(0b000011010010,  12, 34);  h.insert(0b000011010011,  12, 35);
      h.insert(0b000011010100,  12, 36);  h.insert(0b000011010101,  12, 37);
      h.insert(0b000011010110,  12, 38);  h.insert(0b000011010111,  12, 39);
      h.insert(0b000001101100,  12, 40);  h.insert(0b000001101101,  12, 41);
      h.insert(0b000011011010,  12, 42);  h.insert(0b000011011011,  12, 43);
      h.insert(0b000001010100,  12, 44);  h.insert(0b000001010101,  12, 45);
      h.insert(0b000001010110,  12, 46);  h.insert(0b000001010111,  12, 47);
      h.insert(0b000001100100,  12, 48);  h.insert(0b000001100101,  12, 49);
      h.insert(0b000001010010,  12, 50);  h.insert(0b000001010011,  12, 51);
      h.insert(0b000000100100,  12, 52);  h.insert(0b000000110111,  12, 53);
      h.insert(0b000000111000,  12, 54);  h.insert(0b000000100111,  12, 55);
      h.insert(0b000000101000,  12, 56);  h.insert(0b000001011000,  12, 57);
      h.insert(0b000001011001,  12, 58);  h.insert(0b000000101011,  12, 59);
      h.insert(0b000000101100,  12, 60);  h.insert(0b000001011010,  12, 61);
      h.insert(0b000001100110,  12, 62);  h.insert(0b000001100111,  12, 63);
      // --- Black make-up codes ---
      h.insert(0b0000001111,     10,   64);
      h.insert(0b000011001000,   12,  128);  h.insert(0b000011001001,   12,  192);
      h.insert(0b000001011011,   12,  256);  h.insert(0b000000110011,   12,  320);
      h.insert(0b000000110100,   12,  384);  h.insert(0b000000110101,   12,  448);
      h.insert(0b0000001101100,  13,  512);  h.insert(0b0000001101101,  13,  576);
      h.insert(0b0000001001010,  13,  640);  h.insert(0b0000001001011,  13,  704);
      h.insert(0b0000001001100,  13,  768);  h.insert(0b0000001001101,  13,  832);
      h.insert(0b0000001110010,  13,  896);  h.insert(0b0000001110011,  13,  960);
      h.insert(0b0000001110100,  13, 1024);  h.insert(0b0000001110101,  13, 1088);
      h.insert(0b0000001110110,  13, 1152);  h.insert(0b0000001110111,  13, 1216);
      h.insert(0b0000001010010,  13, 1280);  h.insert(0b0000001010011,  13, 1344);
      h.insert(0b0000001010100,  13, 1408);  h.insert(0b0000001010101,  13, 1472);
      h.insert(0b0000001011010,  13, 1536);  h.insert(0b0000001011011,  13, 1600);
      h.insert(0b0000001100100,  13, 1664);  h.insert(0b0000001100101,  13, 1728);
      // --- Extended make-up codes (shared white/black) ---
      h.insert(0b00000001000,   11, 1792);
      h.insert(0b00000001100,   11, 1856);
      h.insert(0b00000001101,   11, 1920);
      h.insert(0b000000010010,  12, 1984);
      h.insert(0b000000010011,  12, 2048);
      h.insert(0b000000010100,  12, 2112);
      h.insert(0b000000010101,  12, 2176);
      h.insert(0b000000010110,  12, 2240);
      h.insert(0b000000010111,  12, 2304);
      h.insert(0b000000011100,  12, 2368);
      h.insert(0b000000011101,  12, 2432);
      h.insert(0b000000011110,  12, 2496);
      h.insert(0b000000011111,  12, 2560);
      return h;
    }();
  return t;
}

inline const HuffTree& mode_tree()
{
  // G4 (T.6 Table 4) mode codewords.
  // Values: 0=Pass, 1=H, 2=V0, 3=VR1, 4=VR2, 5=VR3, 6=VL1, 7=VL2, 8=VL3
  static const HuffTree t = []()
    {
      HuffTree h;
      h.insert(0b0001,     4, 0);  // Pass
      h.insert(0b001,      3, 1);  // H
      h.insert(0b1,        1, 2);  // V0
      h.insert(0b011,      3, 3);  // VR1
      h.insert(0b000011,   6, 4);  // VR2
      h.insert(0b0000011,  7, 5);  // VR3
      h.insert(0b010,      3, 6);  // VL1
      h.insert(0b000010,   6, 7);  // VL2
      h.insert(0b0000010,  7, 8);  // VL3
      return h;
    }();
  return t;
}

// --- decode_1d_run ---

inline int decode_1d_run(BitReader& br, int color)
{
  const HuffTree& tree = color ? black_tree() : white_tree();
  int total = 0;
  int state = 0;
  for(;;)
    {
      int bit = br.read_bit();
      if(bit < 0)
        {
          LOG_S(WARNING) << "ccitt: end of stream during 1D run decode";
          return -1;
        }
      int v = tree.step(state, bit);
      if(v == -2)
        {
          LOG_S(WARNING) << "ccitt: invalid Huffman code in 1D run (color=" << color << ")";
          return -1;
        }
      if(v >= 0)
        {
          total += v;
          if(v < 64)
            {
              return total;  // terminating code — done
            }
          // make-up code — accumulate and read the terminating code next
        }
    }
}

// --- decode_1d_row ---

inline bool decode_1d_row(BitReader& br, std::vector<uint8_t>& row, int width)
{
  int pos   = 0;
  int color = 0;  // starts white
  while(pos < width)
    {
      int run = decode_1d_run(br, color);
      if(run < 0)
        {
          return false;
        }
      int end = std::min(pos + run, width);
      for(int i = pos; i < end; ++i)
        {
          row[i] = static_cast<uint8_t>(color);
        }
      pos   = end;
      color ^= 1;
    }
  return true;
}

// --- find_b1 ---

inline int find_b1(const std::vector<uint8_t>& ref, int a0, int a0col, int width)
{
  // Scan for the first CHANGING ELEMENT in ref after position a0
  // whose color is opposite to a0col.
  // A position p is a changing element if ref[p] != ref[p-1]
  // (where ref[-1] is the imaginary white element = 0).
  for(int p = a0 + 1; p < width; ++p)
    {
      uint8_t prev = (p > 0) ? ref[p - 1] : 0u;
      if(ref[p] != prev and static_cast<int>(ref[p]) != a0col)
        {
          return p;
        }
    }
  return width;  // sentinel: no such element found
}

// --- find_b2 ---

inline int find_b2(const std::vector<uint8_t>& ref, int b1, int width)
{
  // First changing element in ref strictly after b1.
  // Since the reference is binary, any position where ref[p] != ref[p-1] qualifies.
  // We only need to find the next color transition after b1.
  if(b1 >= width)
    {
      return width;
    }
  uint8_t b1col = ref[b1];
  for(int p = b1 + 1; p < width; ++p)
    {
      if(ref[p] != b1col)
        {
          return p;  // first change: ref[p-1] was b1col, ref[p] is opposite — IS a changing element
        }
    }
  return width;
}

// --- decode_g4_row ---

inline bool decode_g4_row(BitReader&                    br,
                           const std::vector<uint8_t>&  ref,
                           std::vector<uint8_t>&         cur,
                           int                           width)
{
  // State:
  //   pos   = next pixel column to fill in `cur` (0-based)
  //   color = current fill color (0=white, 1=black)
  //   a0    = last filled column (= pos-1); starts at -1 (before first pixel)
  //
  // Fill semantics: fill cur[pos .. a1-1] with color, then pos=a1, color^=1.
  // (a1 is defined so that a1 >= pos always.)

  int a0    = -1;
  int color =  0;  // white

  // Offsets for vertical modes: index = mode value (2-8)
  // mode 2=V0, 3=VR1, 4=VR2, 5=VR3, 6=VL1, 7=VL2, 8=VL3
  static const int v_offset[9] = { 0, 0, 0, 1, 2, 3, -1, -2, -3 };

  int mode_state = 0;

  while(a0 < width - 1)
    {
      // --- Read one G4 mode codeword ---
      int mode = -1;
      for(;;)
        {
          int bit = br.read_bit();
          if(bit < 0)
            {
              // End of compressed data — fill remaining pixels with current color
              // and stop.  This can happen legitimately at the very last row.
              for(int i = a0 + 1; i < width; ++i)
                {
                  cur[i] = static_cast<uint8_t>(color);
                }
              return true;
            }
          int v = mode_tree().step(mode_state, bit);
          if(v >= 0)
            {
              mode = v;
              break;
            }
          if(v == -2)
            {
              LOG_S(WARNING) << "ccitt G4: invalid mode codeword";
              return false;
            }
        }

      // --- Find reference elements ---
      int b1 = find_b1(ref, a0, color, width);
      int b2 = find_b2(ref, b1, width);

      if(mode == 0)
        {
          // --- Pass mode ---
          // Fill a0+1 .. b2-1 with color; move a0 to b2; color unchanged.
          int end = std::min(b2, width);
          for(int i = a0 + 1; i < end; ++i)
            {
              cur[i] = static_cast<uint8_t>(color);
            }
          a0 = b2;
        }
      else if(mode == 1)
        {
          // --- Horizontal mode ---
          // Two 1-D runs: first of `color`, then of `color^1`.
          int run1 = decode_1d_run(br, color);
          if(run1 < 0)
            {
              return false;
            }
          int run2 = decode_1d_run(br, color ^ 1);
          if(run2 < 0)
            {
              return false;
            }
          int pos = a0 + 1;
          int end1 = std::min(pos + run1, width);
          for(int i = pos; i < end1; ++i)
            {
              cur[i] = static_cast<uint8_t>(color);
            }
          int end2 = std::min(pos + run1 + run2, width);
          for(int i = end1; i < end2; ++i)
            {
              cur[i] = static_cast<uint8_t>(color ^ 1);
            }
          a0 = pos + run1 + run2 - 1;
        }
      else
        {
          // --- Vertical mode (modes 2-8) ---
          // a1 = b1 + offset; fill a0+1 .. a1-1 with color; flip color.
          int offset = v_offset[mode];
          int a1     = b1 + offset;
          // Clamp to valid range
          if(a1 < a0 + 1)
            {
              a1 = a0 + 1;
            }
          if(a1 > width)
            {
              a1 = width;
            }
          int end = a1;
          for(int i = a0 + 1; i < end; ++i)
            {
              cur[i] = static_cast<uint8_t>(color);
            }
          a0    = a1;
          color ^= 1;
        }
    }

  // Fill any pixels that were not reached (e.g. final run of zero-offset V mode)
  for(int i = a0 + 1; i < width; ++i)
    {
      cur[i] = static_cast<uint8_t>(color);
    }

  return true;
}

// --- decode ---

inline std::vector<uint8_t> decode(const uint8_t* raw_data,
                                    size_t         raw_size,
                                    int            width,
                                    int            height,
                                    int            k)
{
  if(not raw_data or raw_size == 0 or width <= 0 or height <= 0)
    {
      LOG_S(WARNING) << "ccitt::decode: invalid parameters";
      return {};
    }

  LOG_S(INFO) << "ccitt::decode: " << width << "x" << height
              << " k=" << k << " raw=" << raw_size << " bytes";

  BitReader br(raw_data, raw_size);

  std::vector<uint8_t> ref(width, 0);  // previous row; all-white initially
  std::vector<uint8_t> cur(width, 0);  // current row being decoded

  std::vector<uint8_t> output;
  output.reserve(static_cast<size_t>(width) * static_cast<size_t>(height));

  for(int row = 0; row < height; ++row)
    {
      std::fill(cur.begin(), cur.end(), 0u);

      bool ok = false;

      if(k < 0)
        {
          // Group 4: all rows use 2-D coding
          ok = decode_g4_row(br, ref, cur, width);
        }
      else
        {
          // Group 3 1-D: skip EOL marker (12-bit: 000000000001) before each row
          bool found_eol = false;
          int  zeros     = 0;
          while(not br.at_end() and not found_eol)
            {
              int b = br.read_bit();
              if(b == 0)
                {
                  ++zeros;
                }
              else if(b == 1 and zeros >= 11)
                {
                  found_eol = true;
                }
              else
                {
                  zeros = 0;
                }
            }
          ok = decode_1d_row(br, cur, width);
        }

      if(not ok and row == 0)
        {
          LOG_S(WARNING) << "ccitt::decode: failed on first row";
          return {};
        }

      // Map internal representation (0=white, 1=black) to 8-bit grayscale.
      // PDF default for /CCITTFaxDecode: sample 0 = white.
      for(int x = 0; x < width; ++x)
        {
          output.push_back(cur[x] ? 0u : 255u);  // black=0, white=255
        }

      std::swap(ref, cur);
    }

  LOG_S(INFO) << "ccitt::decode: produced " << output.size() << " bytes";
  return output;
}

}  // namespace ccitt
}  // namespace pdflib

#endif  // CCITT_UTILS_H
