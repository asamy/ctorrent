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
#include "connection.h"

asio::io_service g_service;
static std::mutex g_connectionLock;
static std::list<std::shared_ptr<asio::streambuf>> g_outputStreams;

Connection::Connection() :
	m_delayedWriteTimer(g_service),
	m_connectTimer(g_service),
	m_resolver(g_service),
	m_socket(g_service)
{

}

Connection::~Connection()
{
	boost::system::error_code ec;
	m_socket.cancel(ec);

	m_delayedWriteTimer.cancel();
	if (m_socket.is_open())
		m_socket.close();
}

void Connection::poll()
{
	g_service.reset();
	g_service.poll();
}

void Connection::connect(const std::string &host, const std::string &port, const ConnectCallback &cb)
{
	asio::ip::tcp::resolver::query query(host, port);

	m_connectTimer.cancel();
	m_cb = cb;
	m_resolver.async_resolve(
		query,
		std::bind(&Connection::handleResolve, shared_from_this(), std::placeholders::_1, std::placeholders::_2)
	);
}

void Connection::close(bool warn)
{
	m_delayedWriteTimer.cancel();
	m_connectTimer.cancel();

	if (!isConnected()) {
		if (m_eh && warn)
			m_eh("Connection::close(): Called on an already closed connection!");
		return;
	}

	boost::system::error_code ec;
	m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
	m_socket.close();

	if (ec && warn && m_eh)
		m_eh(ec.message());
}

void Connection::write(const uint8_t *bytes, size_t size)
{
	if (!isConnected())
		return;

	if (!m_outputStream) {
		g_connectionLock.lock();
		if (!g_outputStreams.empty()) {
			m_outputStream = g_outputStreams.front();
			g_outputStreams.pop_front();
		} else
			m_outputStream = std::shared_ptr<asio::streambuf>(new asio::streambuf);
		g_connectionLock.unlock();

		m_delayedWriteTimer.cancel();
		m_delayedWriteTimer.expires_from_now(boost::posix_time::milliseconds(10));
		m_delayedWriteTimer.async_wait(std::bind(&Connection::internalWrite, shared_from_this(), std::placeholders::_1));
	}

	std::ostream os(m_outputStream.get());
	os.write((const char *)bytes, size);
	os.flush();
}

void Connection::read_partial(size_t bytes, const ReadCallback &rc)
{
	if (!isConnected())
		return;

	m_rc = rc;
	m_socket.async_read_some(
		asio::buffer(m_inputStream.prepare(bytes)),
		std::bind(&Connection::handleRead, shared_from_this(), std::placeholders::_1, std::placeholders::_2)
	);
}

void Connection::read(size_t bytes, const ReadCallback &rc)
{
	if (!isConnected())
		return;

	m_rc = rc;
	asio::async_read(
		m_socket, asio::buffer(m_inputStream.prepare(bytes)),
		std::bind(&Connection::handleRead, shared_from_this(), std::placeholders::_1, std::placeholders::_2)
	);
}

void Connection::internalWrite(const boost::system::error_code &e)
{
	m_delayedWriteTimer.cancel();

	if (e == asio::error::operation_aborted || !m_socket.is_open())
		return;

	std::shared_ptr<asio::streambuf> outputStream = m_outputStream;
	m_outputStream = nullptr;

	asio::async_write(
		m_socket, *outputStream,
		std::bind(&Connection::handleWrite, shared_from_this(), std::placeholders::_1, std::placeholders::_2, outputStream)
	);
}

void Connection::handleWrite(const boost::system::error_code &e, size_t bytes, std::shared_ptr<asio::streambuf> outputStream)
{
	if (e == asio::error::operation_aborted)
		return;

	outputStream->consume(outputStream->size());
	g_outputStreams.push_back(outputStream);
	if (e)
		handleError(e);
}

void Connection::handleRead(const boost::system::error_code &e, size_t readSize)
{
	if (e)
		return handleError(e);

	if (m_rc) {
		const uint8_t *data = asio::buffer_cast<const uint8_t *>(m_inputStream.data());
		m_rc(data, readSize);
	}

	m_inputStream.consume(readSize);
}

void Connection::handleResolve(
	const boost::system::error_code &e,
	asio::ip::basic_resolver<asio::ip::tcp>::iterator endpoint
)
{
	m_connectTimer.cancel();
	if (e)
		return handleError(e);

	m_socket.async_connect(*endpoint, std::bind(&Connection::handleConnect, shared_from_this(), std::placeholders::_1));
	m_connectTimer.expires_from_now(boost::posix_time::seconds(30));
	m_connectTimer.async_wait(std::bind(&Connection::handleTimeout, shared_from_this(), std::placeholders::_1));
}

void Connection::handleConnect(const boost::system::error_code &e)
{
	m_connectTimer.cancel();
	if (e)
		return handleError(e);
	else if (m_cb)
		m_cb();
}

void Connection::handleError(const boost::system::error_code &error)
{
	if (error != asio::error::operation_aborted && m_eh)
		m_eh(error.message());
}

void Connection::handleTimeout(const boost::system::error_code &error)
{
	if (error != asio::error::operation_aborted)
		return handleError(asio::error::timed_out);
}

std::string Connection::getIPString() const
{
	if (!isConnected())
		return "0000.000.00.0";

	boost::system::error_code error;
	const asio::ip::tcp::endpoint ip = m_socket.remote_endpoint(error);
	if (!error)
		return ip.address().to_string();

	return "0.00.000.0000";
}

uint32_t Connection::getIP() const
{
	if (!isConnected())
		return 0;

	boost::system::error_code error;
	const asio::ip::tcp::endpoint ip = m_socket.remote_endpoint(error);
	if (!error)
		return asio::detail::socket_ops::host_to_network_long(ip.address().to_v4().to_ulong());

	return 0;
}

