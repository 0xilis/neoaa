# neoaa
Open-Source Apple Archive CLI tool for Linux+macOS using [libNeoAppleArchive](https://github.com/0xilis/libNeoAppleArchive).

# Building on OS X

`cd` to the directory and then `make`. You will need `clang` and `ar` installed.

* Static library: `build/usr/lib/libNeoAppleArchive.a`
* CLI tool: `build/usr/bin/neoaa`

# Building on Linux

Tested on Arch Linux. `cd` to the directory and then `make`. You will need `clang` and `ar` installed. `gcc` is untested but if you want to build with it instead theoretically you can just specify CC=gcc.

* Static library: `build/usr/lib/libNeoAppleArchive.a`
* CLI tool: `build/usr/bin/neoaa`

# CLI Usage

```
Usage: neoaa command <options>

Commands:

 archive: archive the contents of a directory.
 extract: extract files from an archive.
 list: list the contents of an archive.
 wrap: archive a singular file.
 unwrap: extract a singular file from an archive.
 version: display version of aa

Options:

 -i: path to the input file or directory.
 -o: path to the output file or directory.
 -a: algorithm for compression, lzfse (default), zlib, raw (no compression).
 -p: specify path of file in project to unwrap.
 -h: this ;-)

```
