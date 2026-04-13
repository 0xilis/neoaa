buildDir = build
CC = clang
CFLAGS += -Os -Isrc/lib/libNeoAppleArchive -Isrc/lib/build/lzfse/include -I/opt/homebrew/opt/openssl@3/include

LZFSE_DIR = src/lib/libNeoAppleArchive/compression/lzfse
BUILD_LZFSE_DIR = ../../../build/lzfse

NEOAPPLEARCHIVE_DIR = src/lib

output: $(buildDir)
	@ # Build libNeoAppleArchive submodule
	@echo "building libNeoAppleArchive..."

        # If you are having trouble building due to OpenSSL,
        # uncomment EXCLUDE_AEA_SUPPORT=1 and report the issue.

	$(MAKE) -C $(NEOAPPLEARCHIVE_DIR) # EXCLUDE_AEA_SUPPORT=1
	@mv src/lib/build/usr/lib/libNeoAppleArchive.a build/usr/lib/libNeoAppleArchive.a

	@ # Build neoaa CLI tool
	@echo "building neoaa..."
	@$(CC) src/cli/*.c -Lbuild/usr/lib -Lsrc/lib/build/lzfse/lib -Lsrc/lib/build/libzbitmap/lib -o build/usr/bin/neoaa -lNeoAppleArchive -llzfse -lzbitmap -lz $(CFLAGS) -L/opt/homebrew/opt/openssl@3/lib

$(buildDir):
	@echo "Creating Build Directory"
	mkdir -p build/usr/lib
	mkdir -p build/usr/bin
	mkdir -p build/obj