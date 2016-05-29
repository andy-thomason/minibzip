
#include <minibzip/decoder.hpp>

#include <fstream>
#include <iostream>

int main() {
  std::ifstream in("examples/11-h.htm.bz2", std::ifstream::ate | std::ifstream::binary);
  std::vector<uint8_t> test_data(in.tellg());
  in.seekg(0);
  in.read((char*)test_data.data(), test_data.size());
  
  std::ofstream os("out2", std::ios::binary);

  minibzip::decoder decoder;

  bool ok = decoder.decode_serial(
    test_data.data(), test_data.data() + test_data.size(),
    [&os](uint64_t location, const uint8_t *data, size_t size) {
      os.write((const char*)data, size);
    }
  );
  
  if (!ok) {
    std::cout << "failed\n";
    return 1;
  }
  return 0;
}


