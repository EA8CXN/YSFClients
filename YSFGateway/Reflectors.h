/*
*   Copyright (C) 2016-2019 by Jonathan Naylor G4KLX
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

#if !defined(Reflectors_H)
#define	Reflectors_H

#include "UDPSocket.h"
#include "Timer.h"
#include <vector>
#include <string>

enum TG_TYPE {
	NONE,
	YSF,
	FCS,
	DMR,
	DMRP,
	P25,
	NXDN,
};

class CReflector {
public:
	CReflector() :
	m_id(),
	m_name(),
	m_desc(),
	m_count("000"),
	m_address(),
	m_port(0U),
	m_type(NONE)
	{
	}

	std::string  m_id;
	std::string  m_name;
	std::string  m_desc;
	std::string  m_count;	
	in_addr      m_address;
	unsigned int m_port;
	TG_TYPE 	 m_type;
	unsigned int m_opt;	
};

class CReflectors {
public:
	CReflectors(const std::string& hostsFile, TG_TYPE type, unsigned int reloadTime, bool makeUpper);
	~CReflectors();

	bool load();

	CReflector* findById(const std::string& id);
	CReflector* findByName(const std::string& name);

	std::vector<CReflector*>& current();

	std::vector<CReflector*>& search(const std::string& name);

	bool reload();

	void clock(unsigned int ms);
	void setParrot(in_addr *address, unsigned int port);

private:
	std::string                 m_hostsFile;
	std::vector<CReflector*> 	m_newReflectors;
	std::vector<CReflector*> 	m_currReflectors;
	std::vector<CReflector*> 	m_search;
	bool                        m_makeUpper;
	CTimer                      m_timer;
	TG_TYPE      			    m_type;
	std::string					m_type_str;
	in_addr  					*m_parrotAddress;
	unsigned int 				m_parrotPort;	
};

#endif
