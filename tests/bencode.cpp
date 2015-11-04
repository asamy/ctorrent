#include "bencode/bencode.h"

#include <iostream>
#include <stdio.h>

int main()
{
	Dictionary dict;
	dict["announce"] = (const char *)"http://bttracker.debian.org:6969/announce";
	dict["comment"] = (const char *)"\"Debian CD from cdimage.debian.org\"";
	dict["creation date"] = 1391870037;

	std::vector<boost::any> seeds;
	seeds.push_back((const char *)"http://cdimage.debian.org/cdimage/release/7.4.0/iso-cd/debian-7.4.0-amd64-netinst.iso");
	seeds.push_back((const char *)"http://cdimage.debian.org/cdimage/archive/7.4.0/iso-cd/debian-7.4.0-amd64-netinst.iso");
	dict["httpseeds"] = seeds;

	Dictionary info;
	info["length"] = 232783872;
	info["name"] = (const char *)"debian-7.4.0-amd64-netinst.iso";
	info["piece length"] = 262144;
	info["pieces"] = (const char *)"";
	dict["info"] = info;

	Bencode bencode;
	bencode.encode(dict);

	size_t size = 0;
	const uint8_t *buffer = (const uint8_t *)bencode.buffer(0, size);
	uint8_t expected[] = "d8:announce41:http://bttracker.debian.org:6969/announce7:comment35:\"Debian CD from cdimage.debian.org\"13:creation datei1391870037e9:httpseedsl85:http://cdimage.debian.org/cdimage/release/7.4.0/iso-cd/debian-7.4.0-amd64-netinst.iso85:http://cdimage.debian.org/cdimage/archive/7.4.0/iso-cd/debian-7.4.0-amd64-netinst.isoe4:infod6:lengthi232783872e4:name30:debian-7.4.0-amd64-netinst.iso12:piece lengthi262144e6:pieces0:ee\0";
	if (memcmp(buffer, expected, size) == 0)
		printf("yep\n");
	else
		printf("nope\n");

	printf("%*s\n", size, (const char *)buffer);
	printf("%s\n", expected);
	return 0;
}
