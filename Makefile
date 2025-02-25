buildDir = build
CC = clang

LZFSE_DIR = src/lib/libNeoAppleArchive/compression/lzfse
BUILD_LZFSE_DIR = ../../../build/lzfse

NEOAPPLEARCHIVE_DIR = src/lib

output: $(buildDir)
	@ # Build libNeoAppleArchive submodule
	@echo "building libNeoAppleArchive..."
	$(MAKE) -C $(NEOAPPLEARCHIVE_DIR)
	@mv src/lib/build/usr/lib/libNeoAppleArchive.a build/usr/lib/libNeoAppleArchive.a

	@ # Build neoaa CLI tool
	@$(CC) src/cli/*.c build/usr/lib/libNeoAppleArchive.a src/lib/build/lzfse/lib/liblzfse.a -o build/usr/bin/neoaa -lz -Os

$(buildDir):
	@echo "Creating Build Directory"
	mkdir -p build/usr/lib
	mkdir build/usr/bin
	mkdir build/obj