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
#include <ctorrent/torrent.h>
#include <net/connection.h>
#include <util/auxiliar.h>

#include <thread>
#include <functional>
#include <iostream>

#include <stdlib.h>
#include <getopt.h>

static void print_help(const char *p)
{
	std::clog << "Usage: " << p << " <options...> <torrent...>" << std::endl;
	std::clog << "Mandatory arguments to long options are mandatory for short options too." << std::endl;
	std::clog << "\t\t--version (-v), --help (-h) 	do not take any argument" << std::endl;
	std::clog << "\t\t--nodownload (-n)		Just check pieces completed, torrent size, etc." << std::endl;
	std::clog << "\t\t--piecesize (-s)		Specify piece size in KB, this will be rounded to the nearest power of two.  Default is 16 KB" << std::endl;
	std::clog << "\t\t--port (-p)			Not yet fully implemented." << std::endl;
	std::clog << "\t\t--dldir (-d)			Specify downloads directory." << std::endl;
	std::clog << "Example: " << p << " --nodownload a.torrent b.torrent c.torrent" << std::endl;
}

int main(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "version",    no_argument,	   0, 'v' },
		{ "help", 	no_argument,	   0, 'h' },
		{ "port",	required_argument, 0, 'p' },
		{ "nodownload", no_argument, 	   0, 'n' },
		{ "piecesize",  required_argument, 0, 's' },
		{ "dldir",      required_argument, 0, 'd' },
		{ 0, 		0,		   0, 0   }
	};

	if (argc == 1) {
		print_help(argv[0]);
		return 1;
	}

	char c;
	int optidx = 0;
	bool nodownload = false;
	int startport = 6881;
	std::string dldir = "Torrents";
	while ((c = getopt_long(argc, argv, "nvp:hs:d:", opts, &optidx)) != -1) {
		switch (c) {
		case 'n':
			nodownload = true;
			break;
		case 's':
			maxRequestSize = 1 << (32 - __builtin_clz(atoi(optarg) - 1));
			break;
		case 'p':
			startport = atoi(optarg);
			break;
		case 'd':
			dldir = optarg;
			break;
		case '?':
			return 1;
		case 'v':
			std::clog << "CTorrent v1.0" << std::endl;
			/* fallthru  */
		case 'h':
			print_help(argv[0]);
			return 0;
		default:
			if (optopt == 'c')
				std::cerr << "Option -" << optopt << " requires an argument." << std::endl;
			else if (isprint(optopt))
				std::cerr << "Unknown option -" << optopt << "." << std::endl;
			else
				std::cerr << "Unknown option character '\\x" << std::hex << optopt << "'." << std::endl;
			return 1;
		}
	}

	if (optind >= argc) {
		std::clog << argv[0] << ": Please specify torrent file(s) after arguments." << std::endl;
		return 1;
	}

	std::thread thread;
	if (!nodownload) {
		std::clog << "Using " << dldir << " as downloads directory and " 
			<< bytesToHumanReadable(maxRequestSize, true) << " piece block size" << std::endl;
		thread = std::thread([] () { while (true) Connection::poll(); });
		thread.detach();
	}

	int total = argc - optind;
	int completed = 0;
	int errors = 0;
	int started = 0;

	Torrent torrents[total];
	std::thread threads[total];
	for (int i = 0; optind < argc; ++i, ++optind) {
		const char *file = argv[optind];
		Torrent *t = &torrents[i];

		if (!t->open(file, dldir)) {
			std::clog << file << ": corrupted torrent file" << std::endl;
			++errors;
			continue;
		}

		std::clog << t->name() << ": Total size: " << bytesToHumanReadable(t->totalSize(), true) << std::endl;
		if (nodownload)
			continue;

		++started;
		threads[i] = std::thread([t, &startport, &completed, &errors]() {
			auto error = t->download(startport++);
			switch (error) {
			case Torrent::DownloadError::Completed:
				std::clog << t->name() << " finished download" << std::endl;
				++completed;
				break;
			case Torrent::DownloadError::AlreadyDownloaded:
				std::clog << t->name() << " was already downloaded" << std::endl;
				++completed;
				break;
			case Torrent::DownloadError::NetworkError:
				std::clog << "Network error was encountered, check your internet connection" << std::endl;
				++errors;
				break;
			case Torrent::DownloadError::TrackerQueryFailure:
				std::clog << "The tracker has failed to respond in time or some internal error has occured" << std::endl;
				++errors;
				break;
			}

			std::clog << "Downloaded: " << bytesToHumanReadable(t->downloadedBytes(), true) << std::endl;
			std::clog << "Uploaded:   " << bytesToHumanReadable(t->uploadedBytes(),   true) << std::endl;
		});
		threads[i].detach();
	}

	if (!nodownload && started > 0) {
		int last = 0;
		while (completed != total) {
			if (errors == total) {
				std::cerr << "All torrents failed to download" << std::endl;
				break;
			}

			if (last == completed) {
				sleep(5);
				continue;
			}

			std::clog << "Completed " << completed << "/" << total - errors << " (" << errors << " errnoeous torrents)" << std::endl;
			last = completed;
		}
	}

	return 0;
}

