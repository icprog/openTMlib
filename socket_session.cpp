/*
 * socket_session.cpp
 * This file is part of an open-source test and measurement I/O library.
 * See documentation for details.
 *
 * Copyright (C) 2011 Stefan Kopp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * The GNU General Public License is available at
 * http://www.gnu.org/copyleft/gpl.html.
 */

#include <string>
#include <iostream>
#include <stdexcept>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include "socket_session.hpp"

using namespace std;

socket_session::socket_session(string address, unsigned short int port, bool lock,
	unsigned int lock_timeout, io_monitor *monitor)
{

	if (lock == true)
	{
		throw_opentmlib_error(-OPENTMLIB_ERROR_LOCKING_NOT_SUPPORTED);
	}

	// Allocate memory for session buffer
	if ((session_buffer_ptr = (char *) malloc(SOCKET_SESSION_LOCAL_BUFFER_SIZE)) == NULL)
	{
		throw_opentmlib_error(-OPENTMLIB_ERROR_MEMORY_ALLOCATION);
	}

	if ((instrument_socket = socket(PF_INET, SOCK_STREAM, 0)) == -1)
	{
		free(session_buffer_ptr);
		throw_opentmlib_error(-OPENTMLIB_ERROR_SOCKET_CREATE);
	}

	struct in_addr
	{
		unsigned long s_addr;
	};

	struct sockaddr_in
	{
		short int sin_family; // Address family
		unsigned short int sin_port; // Port number
		struct in_addr sin_addr; // Internet address
		unsigned char sin_zero[8]; // Padding
	};

	struct sockaddr_in instrument_address;

	// Fill address structure
	memset(&instrument_address, 0, sizeof(struct sockaddr_in));
	instrument_address.sin_family = PF_INET; // IPv4
	instrument_address.sin_port = htons(port); // Port number
	instrument_address.sin_addr.s_addr = inet_addr(address.c_str()); // IP Address

	// Establish TCP connection
	if (connect(instrument_socket, (struct sockaddr *) &instrument_address,
		sizeof(struct sockaddr_in)) == -1)
	{
		free(session_buffer_ptr);
		throw_opentmlib_error(-OPENTMLIB_ERROR_SOCKET_CONNECT);
	}

	// Initialize member variables
	timeout = 5; // 5 s
	term_char_enable = 1; // Termination character enabled
	term_character = '\n';
	eol_char = '\n';
	write_index = 0;
	string_size = 200;
	throw_on_scpi_error = 1;
	tracing = 0;
	this->monitor = monitor;

	return;

}

socket_session::~socket_session()
{

	// Close socket
	if (close(instrument_socket) == -1)
	{
		throw_opentmlib_error(-OPENTMLIB_ERROR_SOCKET_CLOSE);
	}

	// Free session buffer memory
	free(session_buffer_ptr);

	return;

}

int socket_session::write_buffer(char *buffer, int count)
{

	int bytes_written, done;
	struct timeval timeout_s;
	fd_set writefdset;

	done = 0;

	// Set up timeout structure
	timeout_s.tv_sec = timeout;
	timeout_s.tv_usec = 0;

	// Set up file descriptor set for write
	FD_ZERO(&writefdset);
	FD_SET(instrument_socket, &writefdset);

	do
	{

		if (timeout != 0)
		{
			// Wait for write to become possible
			if (select(instrument_socket + 1, NULL, &writefdset, NULL, &timeout_s) != 1)
			{
				throw_opentmlib_error(-OPENTMLIB_ERROR_TIMEOUT);
			}
		}

		// Write buffer to socket
		if ((bytes_written = send(instrument_socket, buffer, count, 0)) == -1)
		{
			throw_opentmlib_error(-errno);
		}

		done += bytes_written;

	}
	while (done < count);

	return done;

}

int socket_session::read_buffer(char *buffer, int max)
{

	int bytes_read, done;
	struct timeval timeout_s;
	fd_set readfdset;

	// Set up timeout structure
	timeout_s.tv_sec = timeout;
	timeout_s.tv_usec = 0;

	// Set up file descriptor set for read
	FD_ZERO(&readfdset);
	FD_SET(instrument_socket, &readfdset);

	if (term_char_enable == 0)
	{

		// Not checking for term character, just read as much data as we get

		if (timeout != 0)
		{
			// Wait for data to become possible
			if (select(instrument_socket + 1, &readfdset, NULL, NULL, &timeout_s) != 1)
			{
				throw_opentmlib_error(-OPENTMLIB_ERROR_TIMEOUT);
			}
		}

		// No need to buffer data locally, read directly to target buffer
		if ((bytes_read = recv(instrument_socket, buffer, max, 0)) == -1)
		{
			throw_opentmlib_error(-errno);
		}

		return bytes_read;

	}
	else
	{

		// Checking for term character and using local buffer

		if (max > SOCKET_SESSION_LOCAL_BUFFER_SIZE)
		{
			// Potential buffer overflow
			throw_opentmlib_error(-OPENTMLIB_ERROR_SOCKET_REQUEST_TOO_MUCH);
		}

		// Check if data in local buffer includes term character
		// In this case, no recv() should be done
		int i = 0;
		while ((*(session_buffer_ptr + i) != term_character) && (i < write_index)) i++;
		if (i < write_index)
		{
			// Found term character, copy buffer contents up to term character to target buffer
			memcpy(buffer, session_buffer_ptr, i + 1);
			// Copy remaining data (if any) to beginning of buffer
			if (write_index - i - 1 > 0)
				memcpy(session_buffer_ptr, session_buffer_ptr + i + 1, write_index - i - 1);
			write_index -= i + 1;
			return i + 1;
		}

		done = write_index; // Start with what we have in local buffer already

		do
		{

			if (timeout != 0)
			{
				// Wait for data to become possible
				if (select(instrument_socket + 1, &readfdset, NULL, NULL, &timeout_s) != 1)
				{
					throw_opentmlib_error(-OPENTMLIB_ERROR_TIMEOUT);
				}
			}

			// Read data to local buffer
			if ((bytes_read = recv(instrument_socket, session_buffer_ptr + done, max - done, 0)) == -1)
			{
				throw_opentmlib_error(-errno);
			}

			// Check if data read includes term character
			int i = 0;
			while ((*(session_buffer_ptr + done + i) != term_character) && (i < bytes_read)) i++;
			if (i < bytes_read)
			{
				// Found term character, copy buffer up to term character to target
				memcpy(buffer, session_buffer_ptr, done + i + 1);
				// Copy remaining data (if any) to beginning of local buffer
				if (bytes_read - i - 1 > 0)
					memcpy(session_buffer_ptr, session_buffer_ptr + done + i + 1, bytes_read - i - 1);
				write_index = bytes_read - i - 1;
				return done + i + 1;
			}

			done += bytes_read;

		}
		while (done < max);

		// No termination character found but max number of bytes requested reached
		// Return what we have...
		memcpy(buffer, session_buffer_ptr, max);
		write_index = 0;
		throw_opentmlib_error(-OPENTMLIB_ERROR_BUFFER_OVERFLOW);

	}

}

void socket_session::set_attribute(unsigned int attribute, unsigned int value)
{

	// Check if attribute is known to parent class
	try
	{
		base_set_attribute(attribute, value);
		return;
	}

	catch (opentmlib_exception & e)
	{
		if (e.code != -OPENTMLIB_ERROR_BAD_ATTRIBUTE)
		{
			// Attribute was processed by parent class but an error was thrown. Pass up...
			throw e;
		}
	}

	switch (attribute)
	{

	case OPENTMLIB_ATTRIBUTE_TRACING:
		if (value > 1)
		{
			throw_opentmlib_error(-OPENTMLIB_ERROR_BAD_ATTRIBUTE_VALUE);
		}
		tracing = value;
		break;

	case OPENTMLIB_ATTRIBUTE_EOL_CHAR:
		if (value > 255)
		{
			throw_opentmlib_error(-OPENTMLIB_ERROR_BAD_ATTRIBUTE_VALUE);
		}
		eol_char = value;
		break;

	case OPENTMLIB_ATTRIBUTE_TIMEOUT:
		timeout = value;
		break;

	case OPENTMLIB_ATTRIBUTE_TERM_CHAR_ENABLE:
		if (value > 1)
		{
			throw_opentmlib_error(-OPENTMLIB_ERROR_BAD_ATTRIBUTE_VALUE);
		}
		term_char_enable = value;
		break;

	case OPENTMLIB_ATTRIBUTE_TERM_CHARACTER:
		if (value > 0xff)
		{
			throw_opentmlib_error(-OPENTMLIB_ERROR_BAD_ATTRIBUTE_VALUE);
		}
		term_character = value;
		break;

	default:
		throw_opentmlib_error(-OPENTMLIB_ERROR_BAD_ATTRIBUTE);

	}

	return; // No error

}

unsigned int socket_session::get_attribute(unsigned int attribute)
{

	// Check if attribute is known to parent class
	try
	{
		unsigned int value;
		value = base_get_attribute(attribute);
		return value;
	}

	catch (opentmlib_exception & e)
	{
		if (e.code != -OPENTMLIB_ERROR_BAD_ATTRIBUTE)
		{
			// Attribute was processed by parent class but an error was thrown. Pass up...
			throw e;
		}
	}

	switch (attribute)
	{

	case OPENTMLIB_ATTRIBUTE_TRACING:
		return tracing;

	case OPENTMLIB_ATTRIBUTE_EOL_CHAR:
		return eol_char;

	case OPENTMLIB_ATTRIBUTE_TIMEOUT:
		return timeout;

	case OPENTMLIB_ATTRIBUTE_TERM_CHAR_ENABLE:
		return term_char_enable;

	case OPENTMLIB_ATTRIBUTE_TERM_CHARACTER:
		return term_character;

	case OPENTMLIB_ATTRIBUTE_SOCKET_BUFFER_SIZE:
		return SOCKET_SESSION_LOCAL_BUFFER_SIZE;

	default:
		throw_opentmlib_error(-OPENTMLIB_ERROR_BAD_ATTRIBUTE);

	}

}

void socket_session::io_operation(unsigned int operation, unsigned int value)
{

	switch (operation)
	{

	default:
		throw_opentmlib_error(-OPENTMLIB_ERROR_BAD_OPERATION);

	}

	return;

}
