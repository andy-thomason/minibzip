////////////////////////////////////////////////////////////////////////////////
//
// 
// based on:
//
// https://github.com/coruus/pyflate/blob/pypy/pyflate/pyflate.py
//

#ifndef MINIBZIP_INCLUDED
#define MINIBZIP_INCLUDED

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <vector>
#include <algorithm>

namespace minibzip {

struct writer {
  virtual void write(uint64_t location, const uint8_t *data, size_t size) = 0;
};

class decoder {
public:

  decoder() {
  }

  bool decode_serial(const uint8_t *src, const uint8_t *src_max, writer &wr) const {
    size_t bit = 0;
    size_t bit_max = (src_max - src) * 8;
    size_t lim = bit_max - 31;
    if (bit + 32 > bit_max) return fail(__LINE__, "");
    if (peek(src, lim, bit, 24) != 0x425a68) return fail(__LINE__, "not a bzip2 file");

    uint32_t blocksize = peek(src, lim, bit+24, 8);
    if (blocksize < '0' || blocksize > '9') return fail(__LINE__, "wrong block size");

    std::vector<uint8_t> out;
    out.reserve(1024 * (blocksize-'0'));

    bit += 32;
    uint64_t location = 0;
    for (;;) {
      if (bit + 48+32 > bit_max) return fail(__LINE__, "file too short");
      uint32_t sig0 = peek(src, lim, bit, 24);
      uint32_t sig1 = peek(src, lim, bit+24, 24);
      uint32_t crc = peek(src, lim, bit+48, 32);
      //printf("sig=%06x\n", sig0);
      bit += 48 + 32;
      if (sig0 == 0x314159 && sig1 == 0x265359) {
        out.resize(0);
        bool ok = decode_block(out, src, lim, bit);
        if (!ok) return false;
        wr.write(location, out.data(), out.size());
      } else if (sig0 == 0x177245 && sig1 == 0x385090) {
        //printf("bzip2 end-of-stream block\n");
        bit += (0-bit) & 7;
        return true;
      } else {
        return fail(__LINE__, "invalid signature");
      }
    }

  }

private:
  struct huffman_table;

  bool decode_block(std::vector<uint8_t> &out, const uint8_t *src, size_t lim, size_t &bitref) const {
    size_t bit = bitref;
    if (bit + 1+24+16 > lim) return fail(__LINE__, "file too short");
  
    uint32_t randomised = peek(src, lim, bit, 1);
    uint32_t pointer = peek(src, lim, bit+1, 24);
    uint32_t used16 = peek(src, lim, bit+1+24, 16);
    
    if (randomised) return fail(__LINE__, "randomized format not supported");
    
    bit += 1 + 24 + 16;
    
    uint32_t symbols_used = 0;
    uint8_t favourites[256];
    for (int i = 0, m = 0x8000; i != 16; m >>= 1, ++i) {
      if (used16 & m) {
        uint32_t v = read(src, lim, bit, 16);
        for (int j = 0, mj = 0x8000; j != 16; mj >>= 1, ++j) {
          if (v & mj) favourites[symbols_used++] = i*16 + j;
        }
      }
    }
    symbols_used += 2;

    uint32_t huffman_groups = read(src, lim, bit, 3);
    if (huffman_groups < 2 || huffman_groups > 6) return fail(__LINE__, "bad huffman groups");
    
    uint32_t selectors_used = read(src, lim, bit, 15);
    std::vector<uint8_t> selectors(selectors_used);

    // todo: could use 32 bit number and shifts
    uint8_t mtf[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    for (uint32_t i = 0; i != selectors_used; ++i) {
      int c = 0;
      while (read(src, lim, bit, 1)) {
        ++c;
        if (c >= huffman_groups) return fail(__LINE__, "bad selector huffman code");
      }
      uint8_t res = mtf[c];
      for (int j = c; j > 0; --j) {
        mtf[j] = mtf[j-1];
      }
      mtf[0] = res;
      selectors[i] = res;
    }

    std::vector<huffman_table> tables;
    tables.reserve(huffman_groups);
    std::vector<uint32_t> code_and_length;
    code_and_length.reserve(symbols_used);
    for (uint32_t j = 0; j != huffman_groups; ++j) {
      uint32_t length = read(src, lim, bit, 5);
      code_and_length.resize(0);
      for (uint32_t i = 0; i != symbols_used; ++i) {
        if (length > 20) return fail(__LINE__, "huffman length too long");
        while (read(src, lim, bit, 1)) {
          length -= (read(src, lim, bit, 1) * 2) - 1;
        }
        code_and_length.push_back(length * 0x10000 + i);
      }
      std::sort(code_and_length.data(), code_and_length.data()+symbols_used);

      tables.emplace_back(code_and_length.data(), symbols_used);
    }

    std::vector<uint8_t> bwt;
    bool huffman_ok = decode_huffman(bwt, selectors, tables, favourites, src, lim, bit, symbols_used);
    if (!huffman_ok) return false;
    
    std::vector<uint32_t> lfmap(bwt.size());
    uint32_t bases[256] = {};
    for (size_t i = 0; i != bwt.size(); ++i) {
      bases[bwt[i]]++;
    }
    
    uint32_t base = 0;
    for (size_t i = 0; i != 256; ++i) {
      uint32_t occ = bases[i];
      bases[i] = base;
      base += occ;
    }
    
    for (size_t i = 0; i != bwt.size(); ++i) {
      lfmap[bases[bwt[i]]] = i;
      bases[bwt[i]]++;
    }
    
    std::vector<uint8_t> almost;
    almost.resize(bwt.size());
    for (size_t i = 0; i != bwt.size(); ++i) {
      pointer = lfmap[pointer];
      almost[i] = bwt[pointer];
    }

    size_t almost_size = almost.size();
    for (size_t i = 0; i != almost_size; ) {
      uint8_t val = almost[i];
      if (i+4 < almost_size && val == almost[i+1] && val == almost[i+2] && val == almost[i+3]) {
        size_t repeat = almost[i+4] + 4;
        while (repeat--) {
          out.push_back(val);
        }
        i += 5;
      } else {
        out.push_back(val);
        i += 1;
      }
    }
    return true;
  }

  bool decode_huffman(
    std::vector<uint8_t> &bwt,
    const std::vector<uint8_t> &selectors,
    const std::vector<huffman_table> &tables,
    uint8_t favourites[],
    const uint8_t *src,
    size_t lim, size_t &bitref, uint32_t symbols_used
  ) const {
    size_t bit = bitref;
    int repeat = 0, repeat_power = 0;
    for (size_t i = 0; i != selectors.size(); ++i) {
      const huffman_table *table = &tables[selectors[i]];
      for (int i = 0; i != 50; ++i) {
        uint32_t symbol = peek(src, lim, bit, 24);
        unsigned bits = table->get_bits(symbol);
        //printf("%s\n", debug_bits(symbol>>(24-bits), bits));
        bit += bits;
        uint32_t offset = (symbol - table->first_symbols[bits]) >> (24-bits);
        uint16_t code = table->codes[table->starts[bits] + offset];
        //printf("symbol %d\n", code);
        if (code < 2) {
          printf("run %d\n", repeat);
          repeat += 1 << (repeat_power++ + code);
          continue;
        } else if (repeat != 0) {
          //printf("runfinal %d\n", repeat);
          
          // output favourites[0] x repeat
          while (repeat--) {
            bwt.push_back(favourites[0]);
          }
          //printf("output '%c'\n", output);
          repeat = repeat_power = 0;
        }
        if (code == symbols_used-1) {
          //printf("finished\n");
          bitref = bit;
          return true;
        } else {
          uint8_t output = favourites[code-1];
          //printf("output %d\n", output);
          for (int i = code-1; i > 0; --i) {
            favourites[i] = favourites[i-1];
          }
          favourites[0] = output;
          bwt.push_back(favourites[0]);
        }
      }
    }
    return fail(__LINE__, "no end symbol found");
  }
  
  static uint32_t peek(const uint8_t *src, size_t lim, size_t bit, size_t n) {
    bit = std::min(bit, lim);
    uint32_t acc = __builtin_bswap32((uint32_t&)src[bit/8]);
    return acc << (bit&7) >> (32-n);
  }

  static uint32_t read(const uint8_t *src, size_t lim, size_t &bit, size_t n) {
    uint32_t result = peek(src, bit, lim, n);
    bit += n;
    return result;
  }

  virtual bool fail(int line, const char *reason) const {
    printf("fail at line %d: %s\n", line, reason);
    return false;
  }

  static const char *debug_bits(unsigned value, unsigned bits) {
    static char str[33];
    for (unsigned i = 0, m = 1u << (bits-1); i != bits; ++i) {
      str[i] = value & m ? '1' : '0';
      m >>= 1;
    }
    str[bits] = 0;
    return str;
  }

  struct huffman_table {
    huffman_table() {
    }

    huffman_table(const uint32_t *code_and_length, uint32_t symbols_used) {
      unsigned symbol = 0;
      unsigned bits = 0;
      starts[0] = first_symbols[0] = 0;
      min_length = code_and_length[0] >> 16;
      max_length = code_and_length[symbols_used-1] >> 16;

      for (uint32_t i = 0; i != symbols_used; ++i) {
        unsigned new_bits = code_and_length[i] >> 16;
        codes[i] = (uint16_t)code_and_length[i];
        while (bits < new_bits) {
          ++bits;
          symbol <<= 1;
          starts[bits] = i;
          first_symbols[bits] = symbol << (24-bits);
          //printf("%2d %s\n", bits, debug_bits(first_symbols[bits], 24));
        }
        //printf("%s\n", debug_bits(symbol, bits));
        symbol++;
      }

      while (bits < 20) {
        symbol <<= 1;
        ++bits;
        starts[bits] = symbols_used;
        first_symbols[bits] = symbol << (24-bits);
      }
    }

    unsigned get_bits(uint32_t symbol) const {
      for (uint32_t bits = min_length; bits != max_length+1; ++bits) {
        if (symbol < first_symbols[bits+1]) {
          //printf("%2d %s %08x %08x\n", bits, debug_bits(first_symbols[bits], 24), symbol, first_symbols[bits]);
          return bits;
        }
      }
      return max_length+1;
    }

    uint32_t min_length;
    uint32_t max_length;
    uint32_t starts[21];
    uint32_t first_symbols[21];
    uint16_t codes[259];

    enum {
      debug = 1
    };
  };
};

} // namespace

#endif

