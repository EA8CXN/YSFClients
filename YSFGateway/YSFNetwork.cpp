/*
 *   Copyright (C) 2009-2014,2016,2017,2018 by Jonathan Naylor G4KLX
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "YSFDefines.h"
#include "YSFNetwork.h"
#include "Utils.h"
#include "Log.h"

#include <cstdio>
#include <cassert>
#include <cstring>

const unsigned int BUFFER_LENGTH = 200U;

#define YSF_VERSION "YSFG-EA"

CYSFNetwork::CYSFNetwork(const std::string& address, unsigned int port, const std::string& callsign, bool debug) :
m_socket(address, port),
m_debug(debug),
m_address(),
m_port(0U),
m_poll(NULL),
m_options(NULL),
m_info(NULL),
m_unlink(NULL),
m_buffer(1000U, "YSF Network Buffer"),
m_pollTimer(1000U, 5U),
m_name(),
m_linked(false),
m_node()
{
	m_poll = new unsigned char[14U];
	::memcpy(m_poll + 0U, "YSFP", 4U);

	m_unlink = new unsigned char[14U];
	::memcpy(m_unlink + 0U, "YSFU", 4U);

	m_node = callsign;
	m_node.resize(YSF_CALLSIGN_LENGTH, ' ');
	// m_options = new unsigned char[50U];
	// ::memcpy(m_options + 0U, "YSFO", 4U);	

	for (unsigned int i = 0U; i < YSF_CALLSIGN_LENGTH; i++) {
		m_poll[i + 4U] = m_node.at(i);
		m_unlink[i + 4U] = m_node.at(i);
		// m_options[i + 4U] = node.at(i);			
	}

	m_id_response = false;
	m_room_id = 0;	
}

CYSFNetwork::CYSFNetwork(unsigned int port, const std::string& callsign, unsigned int rxFrequency, unsigned int txFrequency, const std::string& locator, const std::string& name, unsigned int id, bool debug) :
m_socket(port),
m_debug(debug),
m_address(),
m_port(0U),
m_poll(NULL),
m_options(NULL),
m_info(NULL),
m_unlink(NULL),
m_buffer(1000U, "YSF Network Buffer"),
m_pollTimer(1000U, 5U),
m_node()
{
	m_poll = new unsigned char[14U];
	::memcpy(m_poll + 0U, "YSFP", 4U);

	m_unlink = new unsigned char[14U];
	::memcpy(m_unlink + 0U, "YSFU", 4U);

	// m_options = new unsigned char[50U];
	// ::memcpy(m_options + 0U, "YSFO", 4U);	

	m_info = new unsigned char[80U];
	::sprintf((char*)m_info, "YSFI%-10.10s%9u%9u%-6.6s%-20.20s%-12.12s%7u ", callsign.c_str(), rxFrequency, txFrequency, locator.c_str(), name.c_str(), YSF_VERSION, id);

	m_node = callsign;
	m_node.resize(YSF_CALLSIGN_LENGTH, ' ');

	for (unsigned int i = 0U; i < YSF_CALLSIGN_LENGTH; i++) {
		m_poll[i + 4U]   = m_node.at(i);
		m_unlink[i + 4U] = m_node.at(i);
		// m_options[i + 4U] = node.at(i);		
	}

	m_id_response = false;
	m_room_id = 0;
}

CYSFNetwork::~CYSFNetwork()
{
	delete[] m_poll;
	delete[] m_info;	
	delete[] m_unlink;
	if (m_options) delete[] m_options;
}

bool CYSFNetwork::open()
{
	LogMessage("Opening YSF network connection");

	return m_socket.open();
}

void CYSFNetwork::setDestination(const std::string& name, const in_addr& address, unsigned int port)
{
	m_name    = name;
	m_address = address;
	m_port    = port;
	m_linked  = false;
	m_id_response = false;
	if (m_options) delete[] m_options;
	m_options = NULL;	
	m_room_id = 0;	
}

void CYSFNetwork::clearDestination()
{
	m_address.s_addr = INADDR_NONE;
	m_port           = 0U;
	m_linked         = false;
	m_id_response = false;
	if (m_options) delete[] m_options;
	m_options = NULL;
	m_room_id = 0;

	m_pollTimer.stop();
}

void CYSFNetwork::write(const unsigned char* data)
{
	assert(data != NULL);

	if (m_port == 0U)
		return;

	if (m_debug)
		CUtils::dump(1U, "YSF Network Data Sent", data, 155U);

	m_socket.write(data, 155U, m_address, m_port);
}

void CYSFNetwork::writePoll(unsigned int count)
{
	if (m_port == 0U)
		return;

	m_pollTimer.start();

	for (unsigned int i = 0U; i < count; i++)
		m_socket.write(m_poll, 14U, m_address, m_port);

	if (m_options != NULL)
		m_socket.write(m_options, 50U, m_address, m_port);		
}

void CYSFNetwork::setOptions(const std::string& options)
{
	std::string opt = "0," + options;

	if (options.size() < 1)
		return;

//	LogMessage("YSF Options: *%s*", opt.c_str());

	if (m_options == NULL) 
		m_options = new unsigned char[50U];
	::memcpy(m_options + 0U, "YSFO", 4U);	

	for (unsigned int i = 0U; i < YSF_CALLSIGN_LENGTH; i++) {
		m_options[i + 4U] = m_node.at(i);		
	}
	opt.resize(50, ' ');
	for (unsigned int i = 0U; i < (50 - 4 - YSF_CALLSIGN_LENGTH); i++) {
		m_options[i + 4U + YSF_CALLSIGN_LENGTH] = opt.at(i);
	}
}

void CYSFNetwork::writeUnlink(unsigned int count)
{
	m_pollTimer.stop();

	if (m_port == 0U)
		return;

	for (unsigned int i = 0U; i < count; i++)
		m_socket.write(m_unlink, 14U, m_address, m_port);

	m_linked = false;
	m_id_response = false;
	m_room_id = 0;	
}

unsigned char m_getid[] = "YSFQ";

void CYSFNetwork::clock(unsigned int ms)
{
	unsigned char buffer[BUFFER_LENGTH];
	in_addr address;
	unsigned int port;

	m_pollTimer.clock(ms);
	if (m_pollTimer.isRunning() && m_pollTimer.hasExpired())
		writePoll();

	int length = m_socket.read(buffer, BUFFER_LENGTH, address, port);
	if (length <= 0)
		return;

	if (m_port == 0U)
		return;

	if (address.s_addr != m_address.s_addr || port != m_port)
		return;

	if (::memcmp(buffer, "YSFP", 4U) == 0 && !m_linked) {
		if (strcmp(m_name.c_str(),"MMDVM")== 0)
			LogMessage("Link successful to %s", m_name.c_str());
		else 
			LogMessage("Linked to %s", m_name.c_str());

		m_linked = true;

		if (m_options != NULL)
			m_socket.write(m_options, 50U, m_address, m_port);

		if (m_info != NULL)
			m_socket.write(m_info, 80U, m_address, m_port);

		m_socket.write(m_getid, 4U, m_address, m_port);	
	}

	if (::memcmp(buffer, "YSFPONLINE", 10U) == 0 && m_linked) {
		if (m_options != NULL)
			m_socket.write(m_options, 50U, m_address, m_port);

		if (m_info != NULL)
			m_socket.write(m_info, 80U, m_address, m_port);
	}	

	if ((::memcmp(buffer, "YSFQ", 4U) == 0) && m_linked) {
		buffer[length]=0;
		m_room_id = atoi((const char*)(buffer+4));


		if (length>7) {
			int len = 5 + strlen((const char*)(buffer+4));
			m_room_connections = atoi((const char*)(buffer+len));
			len += strlen((const char*)(buffer+len));
			std::string s((const char*)(buffer+len+1), 10);
			m_room_name = s;
		//	if (m_debug)							
				LogMessage("DG-ID Packet: room: %d, connections: %d, name:%s",m_room_id, m_room_connections, m_room_name.c_str());
		} else {
			m_room_connections = 0;
			m_room_name = std::string("");
		//	if (m_debug)			
				LogMessage("DG-ID Packet: room: %d",m_room_id);
		}
		m_id_response = true;
	}

	if (m_debug)
		CUtils::dump(1U, "YSF Network Data Received", buffer, length);

	unsigned char len = length;
	m_buffer.addData(&len, 1U);

	m_buffer.addData(buffer, length);
}

unsigned int CYSFNetwork::read(unsigned char* data)
{
	assert(data != NULL);

	if (m_buffer.isEmpty())
		return 0U;

	unsigned char len = 0U;
	m_buffer.getData(&len, 1U);

	m_buffer.getData(data, len);

	return len;
}

void CYSFNetwork::close()
{
	m_socket.close();

	LogMessage("Closing YSF network connection");
}


bool CYSFNetwork::getRoomInfo(unsigned int& room_id, unsigned int& room_connections, std::string& room_name) {
	if (m_id_response) {
		room_id = m_room_id;
		room_connections = m_room_connections;
		room_name = m_room_name;
	}
	return m_id_response;
}

bool CYSFNetwork::connected() {
	return m_linked;
}

void CYSFNetwork::id_query_response() {
	m_id_response = false;

	if (m_linked) m_socket.write(m_getid, 4U, m_address, m_port);
}

bool CYSFNetwork::id_getresponse() {
	return m_id_response;
}