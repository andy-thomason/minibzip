
# note: this makefile is purely for the examples and tests
# there is no need to build the library itself.


CCFLAGS = -I include
CC = clang

ifeq ($(OS),Windows_NT)
  # note that this makefile assumes you have basic unix utilities such as unxutils
  CCFLAGS += -fms-compatibility-version=19
  CC = "C:\Program Files\LLVM\bin\clang.exe"
  EXE = .exe
else
  CCFLAGS += -g -O2 -lstdc++
endif

# add your binary here
BINARIES = \
	decode_serial$(EXE)

all: $(BINARIES)

clean:
	rm -f $(BINARIES)

decode_serial$(EXE): examples/decode_serial.cpp include/minibzip/decoder.hpp 
	$(CC) $(CCFLAGS) $< -o $@

test:
	$(CC) $(CCFLAGS) -D MINIBZIP_TESTING examples/decode_serial.cpp -o test$(EXE)
	examples/pyflate.py examples/11-h.htm.bz2 > 1
	./test$(EXE) > 2
	diff 1 2 > 3
	diff out out2 > 4


