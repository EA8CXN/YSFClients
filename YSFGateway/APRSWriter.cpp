/*
 *   Copyright (C) 2010-2014,2016,2017,2018,2020 by Jonathan Naylor G4KLX
 *   Copyright (C) 2019 by Manuel Sanchez EA7EE
 *   Copyright (C) 2018 by Andy Uribe CA6JAU 
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

#include "APRSWriter.h"

#include "YSFDefines.h"

#include <cstdio>
#include <cassert>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
using namespace std;

CAPRSWriter::CAPRSWriter(const std::string& callsign, const std::string& password, const std::string& address, unsigned int port, bool follow) :
m_thread(NULL),
m_idTimer(1000U, 20U * 60U),		// 20 minutes
m_callsign(callsign),
m_server(),
m_txFrequency(0U),
m_rxFrequency(0U),
m_latitude(0.0F),
m_longitude(0.0F),
fm_latitude(0.0F),
fm_longitude(0.0F),
m_follow(follow),
m_height(0),
m_desc(),
m_mobileGPSAddress(),
m_mobileGPSPort(0U),
m_socket(NULL)
{
	assert(!callsign.empty());
	assert(!password.empty());
	assert(!address.empty());
	assert(port > 0U);
	m_thread = new CAPRSWriterThread(callsign, password, address, port);
}

CAPRSWriter::~CAPRSWriter()
{
}

void CAPRSWriter::setInfo(const std::string& node_callsign, unsigned int txFrequency, unsigned int rxFrequency, const std::string& desc, const std::string& icon, const std::string& beacon_text, int beacon_time, bool follow)
{
	m_txFrequency = txFrequency;
	m_rxFrequency = rxFrequency;
	m_desc        = desc;
	m_server	  = node_callsign;
	m_icon		  = icon;
	m_beacon_text = beacon_text;
}

void CAPRSWriter::setStaticLocation(float latitude, float longitude, int height)
{
	m_latitude  = latitude;
	m_longitude = longitude;
	m_height    = height;
}

void CAPRSWriter::setMobileLocation(const std::string& address, unsigned int port)
{
	assert(!address.empty());
	assert(port > 0U);

	m_mobileGPSAddress = CUDPSocket::lookup(address);
	m_mobileGPSPort    = port;

	m_socket = new CUDPSocket;
}

bool CAPRSWriter::open()
{
	if (m_socket != NULL) {
		bool ret = m_socket->open();
		if (!ret) {
			delete m_socket;
			m_socket = NULL;
			return false;
		}

		// Poll the GPS every minute
		m_idTimer.setTimeout(60U);
	} else {
		m_idTimer.setTimeout(20U * 60U);
	}

	m_idTimer.start();

	return m_thread->start();
}

void CAPRSWriter::write(const unsigned char* source, const char* type, unsigned char radio, float fLatitude, float fLongitude, unsigned int tg_type, unsigned int tg_qrv, std::string m_netDst)
{
	char callsign[15U];
	char cad_tmp[20];
	char suffix[3];
	char s_type[20];
	char *ptr;
	
	assert(source != NULL);
	assert(type != NULL);

	
	strcpy(callsign, (const char *)source);
	unsigned int i=0;
	while ((callsign[i]!=' ') && (i<strlen(callsign))) i++;
	callsign[i]=0;	
	
	if (m_follow && (strcmp(callsign,m_node_callsign.c_str())==0)) {
		LogMessage("Catching %s position.",m_node_callsign.c_str());
		fm_latitude = fLatitude;
		fm_longitude = fLongitude;
	} 

	::memcpy(callsign, source, YSF_CALLSIGN_LENGTH);
	callsign[YSF_CALLSIGN_LENGTH] = 0x00U;

	size_t n = ::strspn(callsign, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
	callsign[n] = 0x00U;

	double tempLat = ::fabs(fLatitude);
	double tempLong = ::fabs(fLongitude);

	double latitude = ::floor(tempLat);
	double longitude = ::floor(tempLong);

	latitude = (tempLat - latitude)  * 60.0 + latitude  * 100.0;
	longitude = (tempLong - longitude) * 60.0 + longitude * 100.0;

	char lat[20U];
	::sprintf(lat, "%07.2lf", latitude);

	char lon[20U];
	::sprintf(lon, "%08.2lf", longitude);

	char symbol;
	switch (radio) {
	case 0x24U:
	case 0x28U:
	case 0x30U:			
		symbol = '[';
		strcpy(suffix, "-9");		
		break;
	case 0x25U:
	case 0x29U:
	case 0x2AU:
	case 0x2DU: 	
	case 0x31U:
	case 0x33U:	
		symbol = '>';
		strcpy(suffix, "-9");		
		break;
	case 0x26U:
		symbol = 'r';
		strcpy(suffix, "-9");		
		break;
	default:
		symbol = '-';
		strcpy(suffix, "-9");		
		break;
	}

	switch (tg_type) {
		case 1U: 
		if (tg_qrv == 7U) strcpy(s_type, "@EuropeLink");	
		else strcpy(s_type, "@YSF");		
		break;	
		case 2U:
		strcpy(s_type, "@FCS");		
		break;
		case 3U:
		strcpy(s_type, "@DMR");		
		break;
		case 4U:
		strcpy(s_type, "@P25");		
		break;
		case 5U:
		strcpy(s_type, "@NXDN");		
		break;
		default:
		strcpy(s_type, " ");		
		break;
	}

	strcpy(cad_tmp,m_netDst.c_str());
	ptr=((char *) cad_tmp)+strlen(cad_tmp)-1;
	while (*ptr==' ') ptr--;
	*(ptr+1)=0;

	char output[300U];
	::sprintf(output, "%s%s>APDPRS,C4FM*,qAR,%s:!%s%c/%s%c%c %s QRV %s%s via MMDVM",
		callsign, suffix, m_callsign.c_str(),
		lat, (fLatitude < 0.0F) ? 'S' : 'N',
		lon, (fLongitude < 0.0F) ? 'W' : 'E',
		symbol, type, cad_tmp, s_type);

	m_thread->write(output);
}

bool first_time=true;

void CAPRSWriter::clock(unsigned int ms)
{
	m_idTimer.clock(ms);

	m_thread->clock(ms);

    if ((m_idTimer.getTimer()>10U) && first_time) {
		sendIdFrameFixed();
		first_time=false;
	}

	if (m_socket != NULL) {
		if (m_idTimer.hasExpired()) {
			pollGPS();
			m_idTimer.start();
		}
		sendIdFrameMobile();
	} else {
		if (m_idTimer.hasExpired()) {
			sendIdFrameFixed();
			m_idTimer.start();
		}
	}
}

void CAPRSWriter::close()
{
	if (m_socket != NULL) {
		m_socket->close();
		delete m_socket;
	}

	m_thread->stop();
}

bool CAPRSWriter::pollGPS()
{
	assert(m_socket != NULL);

	return m_socket->write((unsigned char*)"YSFGateway", 10U, m_mobileGPSAddress, m_mobileGPSPort);
}

void CAPRSWriter::sendIdFrameFixed()
{
	if (!m_thread->isConnected())
		return;

	// Default values aren't passed on
	if (m_latitude == 0.0F && m_longitude == 0.0F)
		return;

	double tempLat, tempLong;

	if (fm_latitude == 0.0F) tempLat  = ::fabs(m_latitude);
	else {
		//LogMessage("lat: %f",fm_latitude);
		tempLat  = ::fabs(fm_latitude);
	}

	if (fm_longitude == 0.0F) tempLong = ::fabs(m_longitude);
	else {
	//	LogMessage("lon: %f",fm_longitude);
		tempLong = ::fabs(fm_longitude);
	}

	double latitude  = ::floor(tempLat);
	double longitude = ::floor(tempLong);

	latitude  = (tempLat  - latitude)  * 60.0 + latitude  * 100.0;
	longitude = (tempLong - longitude) * 60.0 + longitude * 100.0;

	char lat[20U];
	::sprintf(lat, "%07.2f", latitude);

	char lon[20U];
	::sprintf(lon, "%08.2f", longitude);

	char output[500U];
	char mobile[10];
	if (fm_latitude == 0.0F) strcpy(mobile,"");
	else strcpy(mobile," /mobile");	
	::sprintf(output, "%s>APDG03,TCPIP*,qAC,%s:!%s%c%c%s%c%c%s%s",
		m_callsign.c_str(), m_server.c_str(),
		lat, (m_latitude < 0.0F)  ? 'S' : 'N',m_icon.at(0),
		lon, (m_longitude < 0.0F) ? 'W' : 'E',m_icon.at(1),
		m_beacon_text.c_str(), mobile);

	m_thread->write(output);
	m_idTimer.start();	
}

void CAPRSWriter::sendIdFrameMobile()
{
	// Grab GPS data if it's available
	unsigned char buffer[200U];
	in_addr address;
	unsigned int port;
	int ret = m_socket->read(buffer, 200U, address, port);
	if (ret <= 0)
		return;

	if (!m_thread->isConnected())
		return;

	buffer[ret] = '\0';

	// Parse the GPS data
	char* pLatitude  = ::strtok((char*)buffer, ",\n");	// Latitude
	char* pLongitude = ::strtok(NULL, ",\n");		// Longitude
	char* pAltitude  = ::strtok(NULL, ",\n");		// Altitude (m)
	char* pVelocity  = ::strtok(NULL, ",\n");		// Velocity (kms/h)
	char* pBearing   = ::strtok(NULL, "\n");		// Bearing

	if (pLatitude == NULL || pLongitude == NULL || pAltitude == NULL)
		return;

	float rawLatitude  = float(::atof(pLatitude));
	float rawLongitude = float(::atof(pLongitude));
	float rawAltitude  = float(::atof(pAltitude));

	double tempLat  = ::fabs(rawLatitude);
	double tempLong = ::fabs(rawLongitude);

	double latitude  = ::floor(tempLat);
	double longitude = ::floor(tempLong);

	latitude  = (tempLat  - latitude)  * 60.0 + latitude  * 100.0;
	longitude = (tempLong - longitude) * 60.0 + longitude * 100.0;

	char lat[20U];
	::sprintf(lat, "%07.2lf", latitude);

	char lon[20U];
	::sprintf(lon, "%08.2lf", longitude);

	char output[500U];
	::sprintf(output, "%s>APDG03,TCPIP*,qAC,%s:!%s%c%c%s%c%c",
		m_callsign.c_str(), m_server.c_str(),
		lat, (rawLatitude < 0.0F)  ? 'S' : 'N',m_icon.at(0),
		lon, (rawLongitude < 0.0F) ? 'W' : 'E',m_icon.at(1));

	if (pBearing != NULL && pVelocity != NULL) {
		float rawBearing   = float(::atof(pBearing));
		float rawVelocity  = float(::atof(pVelocity));

		::sprintf(output + ::strlen(output), "%03.0f/%03.0f", rawBearing, rawVelocity * 0.539957F);
	}

	::sprintf(output + ::strlen(output), "/A=%06.0f %s", float(rawAltitude) * 3.28F, m_beacon_text.c_str());

	m_thread->write(output);
}
