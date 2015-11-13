/*
 * Copyright (c) 2013-2015 Ahmed Samy  <f.fallen45@gmail.com>
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
#ifndef __CONNECTION_H
#define __CONNECTION_H

#ifdef _WIN32
#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif
#define _WIN32_WINNT 0x0501
#define WINVER 0x0501
#endif
#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>

#include <util/auxiliar.h>

#include <mutex>
#include <list>

#include "outputmessage.h"

namespace asio = boost::asio;

class Connection
{
	typedef std::function<void(const uint8_t *, size_t)> ReadCallback;
	typedef std::function<void()> ConnectCallback;
	typedef std::function<void(const std::string &)> ErrorCallback;

public:
	Connection();
	~Connection();

	static void poll();

	void connect(const std::string &host, const std::string &port, const ConnectCallback &cb);
	void close(bool warn = true);	/// Pass false in ErrorCallback otherwise possible infinite recursion
	bool isConnected() const { return m_socket.is_open(); }

	inline void write(const OutputMessage &o) { write(o.data(0), o.size()); }
	inline void write(const std::string &str) { return write((const uint8_t *)str.c_str(), str.length()); }
	void write(const uint8_t *data, size_t bytes);
	void read_partial(size_t bytes, const ReadCallback &rc);
	void read(size_t bytes, const ReadCallback &rc);

	std::string getIPString() const { return ip2str(getIP()); }
	uint32_t getIP() const;

	void setErrorCallback(const ErrorCallback &ec) { m_eh = ec; }

protected:
	void internalWrite(const boost::system::error_code &);
	void handleRead(const boost::system::error_code &, size_t);
	void handleWrite(const boost::system::error_code &, size_t, std::shared_ptr<asio::streambuf>);
	void handleConnect(const boost::system::error_code &);
	void handleResolve(const boost::system::error_code &, asio::ip::basic_resolver<asio::ip::tcp>::iterator);
	void handleError(const boost::system::error_code &);

private:
	asio::deadline_timer m_delayedWriteTimer;
	asio::ip::tcp::resolver m_resolver;
	asio::ip::tcp::socket m_socket;

	ReadCallback m_rc;
	ConnectCallback m_cb;
	ErrorCallback m_eh;

	std::shared_ptr<asio::streambuf> m_outputStream;
	asio::streambuf m_inputStream;

	friend class Server;
};
extern asio::io_service g_service;

#endif
