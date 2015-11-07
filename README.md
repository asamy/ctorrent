# CTorrent

CTorrent is a torrent client written in C++11 with some help from Boost.

## Requirements

- A C++ compiler with C++11 support (GCC or clang are preferred)
- Boost C++ Libraries (for ASIO, SHA1, programoptions and any)

## Developer information

- net/ Contains classes which are responsible for establishing connections, etc.
- bencode/ Contains the decoder and encoder which can decode or encode torrent files.
- ctorrent/ Contains the core classes responsibile for downloading torrents etc.
- util/ Contains various utility functions which are used in bencode/ and ctorrent/
- main.cpp Makes use of all the above (Also is the main for the console application)

## License

MIT (The Expat License).
