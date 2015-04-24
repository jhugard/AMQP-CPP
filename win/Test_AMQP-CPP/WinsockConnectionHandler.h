#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include "Formatter.h"
#include <iomanip>


struct WinsockError : public std::runtime_error
{
	WinsockError(int ec, const std::string& msg)
		:m_ec(ec), std::runtime_error(Formatter() << msg << ": 0x" << std::hex << ec << std::dec)
	{
	}
	int m_ec;
};

struct WinsockConnectionHandler : public AMQP::ConnectionHandler
{
private:
	WSADATA	m_wsaData;
	SOCKET  m_socket;
	std::string m_buffer;

public:

#define DefaultRabbitMQPort "5672"

	/** Construct a Winsock Connection Handler & connect via TCP,
	  * over ipv4 or ipv6 (whichever succeeds first).
	  *
	  * Uses RIIA to connect on construction and disconnect on dtor.
	  *
	  * @param host		DNS name or IP address of AQMP host.
	  * @param port		Port number to connect to.
	  *
	  * @note This call will block until the connection has completed,
	  * or failed.
	 */
	WinsockConnectionHandler(const std::string& host, const std::string& port = DefaultRabbitMQPort)
		:m_socket(INVALID_SOCKET)
	{
		int iResult = WSAStartup(MAKEWORD(2, 2), &m_wsaData);
		if (0 != iResult)
			throw WinsockError(iResult, "Unable to initialize Winsock");

		// Resolve host address
		struct addrinfo *result = nullptr, *ptr = nullptr, hints;
		ZeroMemory(&hints, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		iResult = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
		if (0 != iResult)
		{
			WSACleanup();
			throw WinsockError(iResult, "Unable to resolve hostname");
		}

		// Create socket
		ptr = result;
		m_socket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

		if (INVALID_SOCKET == m_socket)
		{
			WSACleanup();
			throw WinsockError(WSAGetLastError(), "Failed to create socket");
		}

		// Try to connect to server on each returned address
		for (; nullptr != ptr; ptr = ptr->ai_next)
		{
			iResult = connect(m_socket, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen));
			if (0 == iResult)
				break;
		}

		// Fail if unable to connect
		if (0 != iResult)
		{
			int ec = WSAGetLastError();
			if (INVALID_SOCKET != m_socket)
			{
				closesocket(m_socket);
				m_socket = INVALID_SOCKET;
			}
			WSACleanup();
			throw WinsockError(ec, "Failed to connect");
		}
	}

	~WinsockConnectionHandler()
	{
		if (INVALID_SOCKET != m_socket)
		{
			shutdown(m_socket, SD_SEND);
			closesocket(m_socket);
			m_socket = INVALID_SOCKET;
		}
		WSACleanup();
	}

	// Process any data available on the socket
	bool pump(AMQP::Connection* connection)
	{
		// While socket has received data
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(m_socket, &readfds);

		struct timeval timeout = { 0, 0 };

		bool bPumped = false;

		while (select(0, &readfds, nullptr, nullptr, &timeout) > 0)
		{
			// Read up to 16K of data
			const size_t BUFSIZE = 16 * 1024;
			char buf[BUFSIZE];
			int cnt = recv(m_socket, buf, BUFSIZE, 0);

			if (0 == cnt)
				; // do nothing

			else if (SOCKET_ERROR != cnt)
			{
				// Append new data to left over data
				m_buffer.append(buf, cnt);
				// Parse the whole thing
				cnt = connection->parse(m_buffer.c_str(), m_buffer.size());
				// Retain left over data
				m_buffer = m_buffer.substr(cnt);

				bPumped = true;
			}
			else
				throw WinsockError(WSAGetLastError(), "failed to read data");
		}
		return bPumped;
	}

	/*--- ConnectionHandler Interface Methods ---*/

	/**
	*  Method that is called by the AMQP library every time it has data
	*  available that should be sent to RabbitMQ.
	*  @param  connection  pointer to the main connection object
	*  @param  data        memory buffer with the data that should be sent to RabbitMQ
	*  @param  size        size of the buffer
	*
	*  @note This call will block until all data is sent.
	*/
	virtual void onData(AMQP::Connection *connection, const char *data, size_t size)
	{
		if (size == 0)
			return;

		if (connection == nullptr || data == nullptr)
			throw std::invalid_argument("connection and/or data cannot be null");

		// While socket has received data
		fd_set writefds;
		FD_ZERO(&writefds);
		FD_SET(m_socket, &writefds);
		struct timeval timeout = { 15, 0 };

		size_t totalSent = 0;
		int sent = 0;


		while (sent > SOCKET_ERROR && (totalSent < size))
		{
			if (select(0, nullptr, &writefds, nullptr, &timeout) == 0)
			{
				throw WinsockError(WSAETIMEDOUT, "failed to write data");
			}

			sent = send(m_socket, &data[totalSent], static_cast<int>(size), 0);
			totalSent += sent;
		}

		if (SOCKET_ERROR == sent)
		{
			throw WinsockError(WSAGetLastError(), "failed to send data");
		}
	}

	/**
	*  Method that is called by the AMQP library when the login attempt
	*  succeeded. After this method has been called, the connection is ready
	*  to use.
	*  @param  connection      The connection that can now be used
	*/
	virtual void onConnected(AMQP::Connection *connection)
	{
		// @todo
		//  add your own implementation, for example by creating a channel
		//  instance, and start publishing or consuming

		//todo: release a condition variable?
	}

	/**
	*  Method that is called by the AMQP library when a fatal error occurs
	*  on the connection, for example because data received from RabbitMQ
	*  could not be recognized.
	*  @param  connection      The connection on which the error occured
	*  @param  message         A human readable error message
	*/
	virtual void onError(AMQP::Connection *connection, const char *message)
	{
		throw WinsockError(WSAGetLastError(), message);
	}

	/**
	*  Method that is called when the connection was closed. This is the
	*  counter part of a call to Connection::close() and it confirms that the
	*  connection was correctly closed.
	*
	*  @param  connection      The connection that was closed and that is now unusable
	*/
	virtual void onClosed(AMQP::Connection *connection)
	{
	}

};
