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

#include "Reflectors.h"
#include "Log.h"

#include <algorithm>
#include <functional>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <cctype>

char const *atext_type[6] = {"NONE","YSF ","FCS ","DMR ","NXDN","P25 "};

CReflectors::CReflectors(const std::string& hostsFile, TG_TYPE type, unsigned int reloadTime, bool makeUpper) :
m_hostsFile(hostsFile),
m_newReflectors(),
m_currReflectors(),
m_search(),
m_makeUpper(makeUpper),
m_timer(1000U, reloadTime * 60U),
m_type(type),
m_parrotAddress(NULL)
{
	if (reloadTime > 0U)
		m_timer.start();
	
	m_type_str = atext_type[(int)type];
}

CReflectors::~CReflectors()
{
	for (std::vector<CReflector*>::iterator it = m_newReflectors.begin(); it != m_newReflectors.end(); ++it)
		delete *it;

	for (std::vector<CReflector*>::iterator it = m_currReflectors.begin(); it != m_currReflectors.end(); ++it)
		delete *it;

	m_newReflectors.clear();
	m_currReflectors.clear();
}

static bool refComparison(const CReflector* r1, const CReflector* r2)
{
	assert(r1 != NULL);
	assert(r2 != NULL);

	std::string name1 = r1->m_name;
	std::string name2 = r2->m_name;

	for (unsigned int i = 0U; i < 16U; i++) {
		int c = ::toupper(name1.at(i)) - ::toupper(name2.at(i));
		if (c != 0)
			return c < 0;
	}

	return false;
}

bool CReflectors::load()
{
	for (std::vector<CReflector*>::iterator it = m_newReflectors.begin(); it != m_newReflectors.end(); ++it)
		delete *it;

	m_newReflectors.clear();

	FILE* fp = ::fopen(m_hostsFile.c_str(), "rt");
	if (fp != NULL) {
		char buffer[100U];
		while (::fgets(buffer, 100U, fp) != NULL) {
			if (buffer[0U] == '#')
				continue;

			if (m_type==YSF) {
				char* p1 = ::strtok(buffer, ";\r\n");
				char* p2 = ::strtok(NULL, ";\r\n");
				char* p3 = ::strtok(NULL, ";\r\n");
				char* p4 = ::strtok(NULL, ";\r\n");
				char* p5 = ::strtok(NULL, ";\r\n");
				char* p6 = ::strtok(NULL, ";\r\n");
				
				if (p1 != NULL && p2 != NULL && p3 != NULL && p4 != NULL && p5 != NULL && p6 != NULL) {
					std::string host = std::string(p4);

					in_addr address = CUDPSocket::lookup(host);
					if (address.s_addr != INADDR_NONE) {
						CReflector* refl = new CReflector;
						unsigned int tmp = atoi(p1);	
						
						refl->m_id 		= std::to_string(tmp);
						refl->m_name    = std::string(p2);
						refl->m_desc    = std::string(p3);
						refl->m_address = address;
						refl->m_port    = (unsigned int)::atoi(p5);
						refl->m_count   = "000";
						refl->m_type    = YSF;
						refl->m_opt 	= 0;						
						refl->m_name.resize(16U, ' ');
						refl->m_desc.resize(14U, ' ');
						m_newReflectors.push_back(refl);
					}
				}
			} else if (m_type==FCS) {
				char* p1 = ::strtok(buffer, ";\r\n");
				char* p2 = ::strtok(NULL, ";\r\n");
				char* p3 = ::strtok(NULL, ";\r\n");
				
				if (p1 != NULL && p2 != NULL && p3 != NULL) {
					CReflector* refl = new CReflector;
					char tmp1[20];
					unsigned int tmp;	
						
					strcpy(tmp1,p1+3);
					tmp=atoi(tmp1);
					refl->m_id = std::to_string(tmp);
					//LogMessage("ID: %s",refl->m_id.c_str());
					//LogMessage("Name: %s",p2);
					
					refl->m_name    = std::string(p2);
					refl->m_desc    = std::string(p3);
					refl->m_count   = "000";
					refl->m_type    = m_type;
					refl->m_opt 	= 0;					
					refl->m_name.resize(16U, ' ');
					refl->m_desc.resize(14U, ' ');
					m_newReflectors.push_back(refl);
					}			
			
			} else if ((m_type==NXDN) || (m_type==P25)) {	
				char* p1 = ::strtok(buffer, ";\r\n");
				char* p2 = ::strtok(NULL, ";\r\n");
				char* p3 = ::strtok(NULL, ";\r\n");
				//LogMessage("Ref: -%s-%s-%s",p1,p2,p3);
				
				if (p1 != NULL && p2 != NULL && p3 != NULL) {
					CReflector* refl = new CReflector;
					refl->m_id = std::string(p1);
					refl->m_name    = std::string(p2);
					refl->m_desc    = std::string(p3);
					refl->m_count   = "000";
					refl->m_type    = m_type;
					refl->m_opt 	= 0;
					refl->m_name.resize(16U, ' ');
					refl->m_desc.resize(14U, ' ');
					m_newReflectors.push_back(refl);
					}						
			} else if (m_type==DMR) {
				char* p1 = ::strtok(buffer, ";\r\n");
				char* p2 = ::strtok(NULL, ";\r\n");
				char* p3 = ::strtok(NULL, ";\r\n");
				char* p4 = ::strtok(NULL, ";\r\n");
				char* p5 = ::strtok(NULL, ";\r\n");
				
				if (p1 != NULL && p2 != NULL && p3 != NULL && p4 != NULL && p5 != NULL) {
					char tmp[6];
					CReflector* refl = new CReflector;
					
					sprintf(tmp,"%03d", atoi(p3));
					refl->m_count   = std::string(tmp);
					refl->m_id      = std::string(p1);
					refl->m_name    = std::string(p4);
					refl->m_desc    = std::string(p5);

					refl->m_type    = DMR;
					refl->m_opt 	= atoi(p2);					
					refl->m_name.resize(16U, ' ');
					refl->m_desc.resize(14U, ' ');
					m_newReflectors.push_back(refl);
					}
			}	
		}

		::fclose(fp);
	}

	size_t size = m_newReflectors.size();
	LogInfo("Loaded %u %s reflectors", size, m_type_str.c_str());

	// Add the Parrot entry
	if (m_parrotAddress != NULL) {
		//LogInfo("Parrot Entry");
		in_addr tmp;
		::memcpy(&tmp,m_parrotAddress,sizeof(tmp));
		CReflector* refl = new CReflector;
		refl->m_id      = "1";
		refl->m_name    = "ZZ Parrot       ";
		refl->m_desc    = "Parrot        ";
		refl->m_address = tmp;
		refl->m_port    = m_parrotPort;
		refl->m_count   = "000";
		refl->m_type    = YSF;
		refl->m_opt 	= 0;

		m_newReflectors.push_back(refl);

	//	LogInfo("Loaded YSF parrot");
	}

	size = m_newReflectors.size();
	if (size == 0U)
		return false;

	if (m_makeUpper) {
		for (std::vector<CReflector*>::iterator it = m_newReflectors.begin(); it != m_newReflectors.end(); ++it) {
			std::transform((*it)->m_name.begin(), (*it)->m_name.end(), (*it)->m_name.begin(), ::toupper);
			std::transform((*it)->m_desc.begin(), (*it)->m_desc.end(), (*it)->m_desc.begin(), ::toupper);
		}
	}

	std::sort(m_newReflectors.begin(), m_newReflectors.end(), refComparison);

	return true;
}

CReflector* CReflectors::findById(const std::string& id)
{
	for (std::vector<CReflector*>::const_iterator it = m_currReflectors.cbegin(); it != m_currReflectors.cend(); ++it) {
		if (id == (*it)->m_id)
			return *it;
	}

	LogMessage("Trying to find non existent %s reflector with an id of %s", m_type_str.c_str(), id.c_str());

	return NULL;
}

CReflector* CReflectors::findByName(const std::string& name)
{
	std::string fullName = name;
	if (m_makeUpper) {
                std::transform(fullName.begin(), fullName.end(), fullName.begin(), ::toupper);
        }
	fullName.resize(16U, ' ');

	for (std::vector<CReflector*>::const_iterator it = m_currReflectors.cbegin(); it != m_currReflectors.cend(); ++it) {
		if (fullName == (*it)->m_name)
			return *it;
	}

	LogMessage("Trying to find non existent %s reflector with a name of %s", m_type_str.c_str(), name.c_str());

	return NULL;
}

std::vector<CReflector*>& CReflectors::current()
{
	return m_currReflectors;
}

std::vector<CReflector*>& CReflectors::search(const std::string& name)
{
	m_search.clear();

	std::string trimmed = name;
	trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), trimmed.end());
	std::transform(trimmed.begin(), trimmed.end(), trimmed.begin(), ::toupper);

	// Removed now un-used variable
	// size_t len = trimmed.size();

	for (std::vector<CReflector*>::iterator it = m_currReflectors.begin(); it != m_currReflectors.end(); ++it) {
		std::string reflector = (*it)->m_name;
		reflector.erase(std::find_if(reflector.rbegin(), reflector.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), reflector.end());
		std::transform(reflector.begin(), reflector.end(), reflector.begin(), ::toupper);

		// Origional match function - only matches start of string.
		// if (trimmed == reflector.substr(0U, len))
		//	m_search.push_back(*it);
		
		// New match function searches the whole string
		unsigned int refSrcPos;
                for (refSrcPos=0;refSrcPos<reflector.length(); refSrcPos++)
                {
                        if (reflector.substr(refSrcPos,trimmed.length()) == trimmed)
                        {
                                m_search.push_back(*it);
                        }
                }
	}

	std::sort(m_search.begin(), m_search.end(), refComparison);

	return m_search;
}

bool CReflectors::reload()
{
	if (m_newReflectors.empty())
		return false;

	for (std::vector<CReflector*>::iterator it = m_currReflectors.begin(); it != m_currReflectors.end(); ++it)
		delete *it;

	m_currReflectors.clear();

	m_currReflectors = m_newReflectors;

	m_newReflectors.clear();

	return true;
}

void CReflectors::setParrot(in_addr *address, unsigned int port)
{
	m_parrotAddress = address;
	m_parrotPort    = port;
}

void CReflectors::clock(unsigned int ms)
{
	m_timer.clock(ms);

	if (m_timer.isRunning() && m_timer.hasExpired()) {
		load();
		m_timer.start();
	}
}
