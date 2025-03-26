buildDir = build
CC = clang
CFLAGS += -Os -Isrc/lib/libNeoAppleArchive -Isrc/lib/build/lzfse/include

LZFSE_DIR = src/lib/libNeoAppleArchive/compression/lzfse
BUILD_LZFSE_DIR = ../../../build/lzfse

NEOAPPLEARCHIVE_DIR = src/lib

output: $(buildDir)
	@ # Build libNeoAppleArchive submodule
	@echo "building libNeoAppleArchive..."
	$(MAKE) -C $(NEOAPPLEARCHIVE_DIR)
	@mv src/lib/build/usr/lib/libNeoAppleArchive.a build/usr/lib/libNeoAppleArchive.a

	@ # Build neoaa CLI tool
	@echo "building neoaa..."
	@$(CC) src/cli/*.c -Lbuild/usr/lib -Lsrc/lib/build/lzfse/lib -Lsrc/lib/build/libzbitmap/lib -o build/usr/bin/neoaa -lNeoAppleArchive -llzfse -lzbitmap -lz $(CFLAGS)

$(buildDir):
	@echo "Creating Build Directory"
	mkdir -p build/usr/lib
	mkdir -p build/usr/bin
	mkdir -p build/obj