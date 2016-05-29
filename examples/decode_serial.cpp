
#include <minibzip/decoder.hpp>

#include <fstream>
#include <iostream>

int main() {
  uint8_t test_data[8000];

  std::ifstream("examples/11-h.htm.bz2").read((char*)test_data, sizeof(test_data));
  
  std::ofstream os("out2");

  minibzip::decoder decoder;

  struct writer : public minibzip::writer {
    std::ofstream &os;
    
    writer(std::ofstream &os) : os(os) {
    }
    
    void write(uint64_t location, const uint8_t *data, size_t size) {
      os.write((const char*)data, size);
    }
  };

  writer wr(os);
  
  bool ok = decoder.decode_serial(
    test_data + 0, test_data + sizeof(test_data), wr
  );
  
  if (!ok) {
    std::cout << "failed\n";
    return 1;
  }
  return 0;
}


