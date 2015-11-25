/*
 * Copyright (c) 2014 Ahmed Samy  <f.fallen45@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef __AUXILIAR_H
#define __AUXILIAR_H

#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

#ifdef _WIN32
#define PATH_SEP "\\"
#define MKDIR(name)		mkdir((name).c_str())
#include <io.h>
#include <windows.h>
#include <Shlwapi.h>
#else
#define PATH_SEP "/"
#define MKDIR(name)		mkdir((name).c_str(), 0700)
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#define CHDIR(name)		chdir((name).c_str())

#include <string>
#include <fstream>
#include <tuple>

union Mix {
	int sdat;
	char cdat[4];
};
static constexpr Mix mix { 0x1 };
constexpr bool isLittleEndian()
{
	return mix.cdat[0] == 1;
}

typedef std::tuple<std::string, std::string, std::string> UrlData;
#define URL_PROTOCOL(u)		std::get<0>((u))
#define URL_HOSTNAME(u)		std::get<1>((u))
#define URL_SERVNAME(u)		std::get<2>((u))

extern UrlData parseUrl(const std::string &str);
extern std::string bytesToHumanReadable(uint32_t bytes, bool si);
extern std::string ip2str(uint32_t ip);
extern uint32_t str2ip(const std::string &ip);
extern std::string getcwd();
extern std::string urlencode(const std::string& url);

extern bool validatePath(const std::string &base, const std::string &path);
extern bool nodeExists(const std::string &node);

static inline bool test_bit(uint32_t bits, uint32_t bit)
{
	return (bits & bit) == bit;
}

static inline bool starts_with(const std::string &s, const std::string &start)
{
	if (s.length() < start.length())
		return false;

	return s.substr(0, start.length()) == start;
}

static inline bool ends_with(const std::string &s, const std::string &end)
{
	if (s.length() < end.length())
		return false;

	return s.substr(s.length() - end.length()) == end;
}

#ifdef __MINGW32__
#include <boost/lexical_cast.hpp>

namespace std {
	template <typename T>
	string to_string(const T &value)
	{
		return boost::lexical_cast<string>(value);
	}
}
#endif
#endif

