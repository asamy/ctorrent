/*
 * Copyright (c) 2014, 2015 Ahmed Samy  <f.fallen45@gmail.com>
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

#include <boost/program_options.hpp>

static void print_help(const char *p)
{
	std::clog << "Usage: " << p << " <options...> <torrent...>" << std::endl;
	std::clog << "Mandatory arguments to long options are mandatory for short options too." << std::endl;
	std::clog << "\t\t--version (-v), --help (-h) 	print version and help respectively then exit" << std::endl;
	std::clog << "\t\t--nodownload (-n)		Just check pieces completed, torrent size, etc." << std::endl;
	std::clog << "\t\t--piecesize (-s)		Specify piece size in KB, this will be rounded to the nearest power of two.  Default is 16 KB" << std::endl;
	std::clog << "\t\t--port (-p)			Not yet fully implemented." << std::endl;
	std::clog << "\t\t--dldir (-d)			Specify downloads directory." << std::endl;
	std::clog << "\t\t--noseed (-e)			Do not seed after download has finished." << std::endl;
	std::clog << "\t\t--torrents (-t) 		Specify torrent file(s)." << std::endl;
	std::clog << "Example: " << p << " --nodownload --torrents a.torrent b.torrent c.torrent" << std::endl;
}

int main(int argc, char *argv[])
{
	bool noseed = false;
	int startport = 6881;
	std::string dldir = "Torrents";
	std::vector<std::string> files;

	namespace po = boost::program_options;
	po::options_description opts;
	opts.add_options()
		("version,v", "print version string")
		("help,h", "print this help message")
		("port,p", po::value(&startport), "specify start port; not currently used")
		("nodownload,n", "do not download anything, just print info about torrents")
		("piecesize,s", po::value(&maxRequestSize), "specify piece block size")
		("dldir,d", po::value(&dldir), "specify downloads directory")
		("noseed,e", po::value(&noseed), "do not seed after download has finished.")
		("torrents,t", po::value<std::vector<std::string>>(&files)->required()->multitoken(), "specify torrent file(s)");

	if (argc == 1) {
		print_help(argv[0]);
		return 1;
	}

	po::variables_map vm;
	try {
		po::store(po::parse_command_line(argc, argv, opts), vm);
		po::notify(vm);
	} catch (const std::exception &e) {
		std::cerr << argv[0] << ": error parsing command line arguments: " << e.what() << std::endl;
		print_help(argv[0]);
		return 1;
	}

	if (vm.count("help")) {
		print_help(argv[0]);
		return 0;
	}

	if (vm.count("version")) {
		std::clog << "CTorrent version 1.0" << std::endl;
		return 0;
	}

	bool nodownload = false;
	if (vm.count("nodownload"))
		nodownload = true;
	else {
		if (vm.count("piecesize"))
			maxRequestSize = 1 << (32 - __builtin_clz(maxRequestSize - 1));
		std::clog << "Using " << dldir << " as downloads directory and " 
			<< bytesToHumanReadable(maxRequestSize, true) << " piece block size" << std::endl;
	}

	size_t total = files.size();
	size_t completed = 0;
	size_t errors = 0;
	size_t eseed = 0;
	size_t started = 0;

	Torrent torrents[total];
	std::thread threads[total];
	for (size_t i = 0; i < files.size(); ++i) {
		std::string file = files[i];
		Torrent *t = &torrents[i];

		std::clog << "Opening: " << file << std::endl;
		if (!t->open(file, dldir)) {
			std::cerr << file << ": corrupted torrent file" << std::endl;
			++errors;
			continue;
		}

		std::clog << t->name() << ": Total size: " << bytesToHumanReadable(t->totalSize(), true) << std::endl;
		if (nodownload)
			continue;

		++started;
		threads[i] = std::thread([t, &startport, &completed, &errors, &eseed, noseed]() {
			uint16_t tport = startport++;
			Torrent::DownloadError error = t->download(tport);
			switch (error) {
			case Torrent::DownloadError::Completed:
				std::clog << t->name() << ": finished download" << std::endl;
				++completed;
				break;
			case Torrent::DownloadError::AlreadyDownloaded:
				std::clog << t->name() << ": was already downloaded" << std::endl;
				++completed;
				break;
			case Torrent::DownloadError::NetworkError:
				std::clog << t->name() << ": Network error was encountered, check your internet connection" << std::endl;
				++errors;
				break;
			case Torrent::DownloadError::TrackerQueryFailure:
				std::clog << t->name() << ": The tracker(s) has failed to respond in time or some internal error has occured" << std::endl;
				++errors;
				break;
			}

			std::clog << t->name() << ": Downloaded: " << bytesToHumanReadable(t->downloadedBytes(), true) << std::endl;
			std::clog << t->name() << ": Uploaded:   " << bytesToHumanReadable(t->uploadedBytes(),   true) << std::endl;

			if (!noseed
				&& (error == Torrent::DownloadError::Completed || error == Torrent::DownloadError::AlreadyDownloaded)
				&& !t->seed(tport))
			{
				std::clog << t->name() << ": unable to initiate seeding" << std::endl;
				++eseed;
			}
		});
		threads[i].detach();
	}

	if (!nodownload && started > 0) {
		int last = 0;
		while (completed != total - errors) {
			if (errors == total) {
				std::cerr << "All torrents failed to download" << std::endl;
				break;
			}

			Connection::poll();
			if (last == completed)
				continue;

			std::clog << "Completed " << completed << "/" << total - errors << " (" << errors << " errnoeous torrents)" << std::endl;
			last = completed;
		}
	}

	if (!noseed) {
		std::clog << "Now seeding" << std::endl;
		while (eseed != total)
			Connection::poll();
	}

	return 0;
}

