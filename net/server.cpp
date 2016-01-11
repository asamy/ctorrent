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
#include "server.h"

Server::Server(uint16_t port)
	: m_acceptor(g_service, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
{
	m_acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
}

Server::~Server()
{
	boost::system::error_code ec;
	m_acceptor.cancel(ec);
	m_acceptor.close();
}

void Server::stop()
{
	m_acceptor.cancel();
	m_acceptor.close();
	m_stopped = true;
}

void Server::accept(const Acceptor &ac)
{
	ConnectionPtr conn(new Connection());
	m_acceptor.async_accept(conn->m_socket,
		[=] (const boost::system::error_code &error) {
			if (!error)
				return ac(conn);
		}
	);
}

