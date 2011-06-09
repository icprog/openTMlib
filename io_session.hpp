/*
 * io_session.hpp
 * This file is part of an open-source test and measurement I/O library.
 * See documentation for details.
 *
 * Copyright (C) 2011, Stefan Kopp, Gechingen, Germany
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
#include "opentmlib.hpp"

using namespace std;

class io_session
{

// Basic I/O methods to be implemented by various session types/classes
public:
	virtual int write_buffer(char *buffer, int count) = 0; // Write <count> bytes from buffer to device
	virtual int read_buffer(char *buffer, int max) = 0; // Read up to <max> bytes from device to buffer
	virtual int set_attribute(unsigned int attribute, unsigned int value) = 0; // Set attribute
	virtual int get_attribute(unsigned int attribute, unsigned int *value) = 0; // Get attribute
	virtual int io_operation(unsigned int operation, unsigned int value) = 0; // Perform special I/O operation

// Higher-level I/O methods which session classes will inherit/share
public:
	int write_string(string message, bool eol); // Write string (with or without NL character)
	int read_string(string &message); // Read string

private:

protected:

};