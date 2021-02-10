/*
*   Copyright (C) 2016,2017,2018,2019 by Jonathan Naylor G4KLX
*   Copyright (C) 2019 by Manuel Sanchez EA7EE
*   Copyright (C) 2018,2019 by Andy Uribe CA6JAU
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

#include "WiresX.h"
#include "YSFPayload.h"
#include "YSFFICH.h"
#include "Utils.h"
#include "Sync.h"
#include "CRC.h"
#include "Log.h"
#include "ModeConv.h"

#include <algorithm>
#include <functional>
#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cctype>

//#define ANALYZER

const unsigned char DX_REQ[]    = {0x5DU, 0x71U, 0x5FU};
const unsigned char CONN_REQ[]  = {0x5DU, 0x23U, 0x5FU};
const unsigned char DISC_REQ[]  = {0x5DU, 0x2AU, 0x5FU};
const unsigned char ALL_REQ[]   = {0x5DU, 0x66U, 0x5FU};
const unsigned char NEWS_REQ[]  = {0x5DU, 0x63U, 0x5FU};
const unsigned char CAT_REQ[]   = {0x5DU, 0x67U, 0x5FU};

const unsigned char BEACON_REQ_GPS[]   = {0x47U, 0x64U, 0x5FU};
const unsigned char BEACON_REQ_NOGPS[]   = {0x47U, 0x63U, 0x5FU};

const unsigned char DX_RESP[]   = {0x5DU, 0x51U, 0x5FU, 0x25U};
const unsigned char DX_BEACON[]   = {0x5DU, 0x42U, 0x5FU, 0x25U};

const unsigned char CONN_RESP[] = {0x5DU, 0x41U, 0x5FU, 0x25U};
const unsigned char DISC_RESP[] = {0x5DU, 0x41U, 0x5FU, 0x25U};
const unsigned char ALL_RESP[]  = {0x5DU, 0x46U, 0x5FU, 0x25U};

const unsigned char LNEWS_RESP[]  = {0x5DU, 0x46U, 0x5FU, 0x25U};
const unsigned char NEWS_RESP[]  = {0x5DU, 0x43U, 0x5FU, 0x25U};

const unsigned char DEFAULT_FICH[] = {0x20U, 0x00U, 0x01U, 0x00U};
const unsigned char NET_HEADER[] = "YSFD                    ALL       ";

const unsigned char LIST_REQ[]  = {0x5DU, 0x6CU, 0x5FU};
const unsigned char LIST_RESP[]  = {0x5DU, 0x4CU, 0x5FU, 0x25U};

const unsigned char GET_RSC[]  = {0x5DU, 0x72U, 0x5FU};
const unsigned char GET_MSG_RESP[]  = {0x5DU, 0x54U, 0x5FU, 0x25U};

const unsigned char MESSAGE_REC_GPS[]  = {0x47U, 0x66U, 0x5FU};
const unsigned char MESSAGE_REC[]  = {0x47U, 0x65U, 0x5FU};

const unsigned char MESSAGE_SEND[]  = {0x47U, 0x66U, 0x5FU, 0x25U};

const unsigned char VOICE_RESP[]  = {0x5DU, 0x56U, 0x5FU, 0x25U};
const unsigned char VOICE_ACK[]  = {0x5DU, 0x30U, 0x5FU, 0x25U};

const unsigned char PICT_REC_GPS[]  = {0x47U, 0x68U, 0x5FU};
const unsigned char PICT_REC[]  = {0x47U, 0x67U, 0x5FU};

const unsigned char PICT_DATA[]  = {0x4EU, 0x62U, 0x5FU};
const unsigned char PICT_BEGIN[]  = {0x4EU, 0x63U, 0x5FU};
const unsigned char PICT_BEGIN2[]  = {0x4EU, 0x64U, 0x5FU};

const unsigned char PICT_END[]  = {0x4EU, 0x65U, 0x5FU};

const unsigned char PICT_DATA_RESP[]  = {0x4EU, 0x62U, 0x5FU, 0x25U};
const unsigned char PICT_BEGIN_RESP[]  = {0x4EU, 0x63U, 0x5FU, 0x25U};
const unsigned char PICT_BEGIN_RESP_GPS[]  = {0x4EU, 0x64U, 0x5FU, 0x25U};
const unsigned char PICT_END_RESP[]  = {0x4EU, 0x65U, 0x5FU, 0x25U};

const unsigned char PICT_PREAMB_RESP[]  = {0x5DU, 0x50U, 0x5FU, 0x25U};

const unsigned char UP_ACK[] = {0x47U, 0x30U, 0x5FU, 0x25U};

const unsigned char voice_mark[] = {0x5A,0x4C,0x5A,0x5A,0x5A,0x4C,0x76,0x58,0x1C,0x6C,0x20,0x1C,0x30,0x57};

CWiresX::CWiresX(CWiresXStorage* storage, const std::string& callsign, std::string& location, CYSFNetwork* network, bool makeUpper, CModeConv *mconv) :
m_storage(storage),
m_callsign(),
m_location(location),
m_node(),
m_network(network),
m_reflectors(NULL),
m_reflector(),
m_id(),
m_name(),
m_command(NULL),
m_txFrequency(0U),
m_rxFrequency(0U),
m_dstID(0),
m_count(-1),
m_timer(500U, 1U),
m_ptimer(1000U, 1U),
m_timeout(1000U, 10U),
m_seqNo(20U),
m_header(NULL),
m_csd1(NULL),
m_csd2(NULL),
m_csd3(NULL),
m_status(WXSI_NONE),
m_start(0U),
m_category(),
m_makeUpper(makeUpper),
m_busy(false),
m_busyTimer(1000U, 1U),
m_txWatch(),
m_bufferTX(10000U, "YSF Wires-X TX Buffer"),
m_type(0U),
m_number(0U),
m_news_source(),
m_source(),
m_serial(),
m_talky_key(),
m_picture_state(WXPIC_NONE),
m_offset(0),
m_pcount(0),
m_end_picture(true),
error_upload(false)
{
	char tmp[20U];

	assert(network != NULL);
	m_enable = false;

	m_node = callsign;
	m_node.resize(YSF_CALLSIGN_LENGTH, ' ');
	// LogMessage("m_node = %s",m_node.c_str());	
	strcpy(tmp,callsign.c_str());
	unsigned int i=0;
	while (i<strlen(tmp) && isalnum(tmp[i])) {
		i++;
	}
	i--;
	while ((i>0) && isdigit(tmp[i])) {
		i--;
	}
	tmp[i+1]=0U;
	m_callsign = std::string(tmp);
	m_callsign.resize(YSF_CALLSIGN_LENGTH, ' ');
	strcpy((char *)m_source,"          ");
	// LogMessage("m_callsign = %s",m_callsign.c_str());
	m_location.resize(14U, ' ');	

	m_command = new unsigned char[1100U];

	m_header = new unsigned char[34U];
	m_csd1   = new unsigned char[20U];
	m_csd2   = new unsigned char[20U];
	m_csd3   = new unsigned char[20U];
	
	m_picture_state = WXPIC_NONE;
	m_end_picture=true;
	m_last_news = 0;
	m_ambefile = NULL;
	m_conv = mconv;
	m_no_store_picture=true;
	
	m_txWatch.start();
}

CWiresX::~CWiresX()
{
	delete[] m_csd3;
	delete[] m_csd2;
	delete[] m_csd1;
	delete[] m_header;
	delete[] m_command;
}

std::string CWiresX::getCallsign() {

	return m_callsign;
	
}

void CWiresX::setInfo(const std::string& name, unsigned int txFrequency, unsigned int rxFrequency)
{
	assert(txFrequency > 0U);
	assert(rxFrequency > 0U);
	
	m_name        = name;
	m_txFrequency = txFrequency;
	m_rxFrequency = rxFrequency;

	m_name.resize(14U, ' ');

	unsigned int hash = 0U;

	for (unsigned int i = 0U; i < name.size(); i++) {
		hash += name.at(i);
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	// Final avalanche
	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	char id[10U];
	::sprintf(id, "%05u", hash % 100000U);

	LogInfo("The ID of this repeater is %s", id);

	m_id = std::string(id);
	m_id.resize(YSF_CALLSIGN_LENGTH,' ');

	::memset(m_csd1, '*', 20U);
	::memset(m_csd2, ' ', 20U);
	::memset(m_csd3, ' ', 20U);

	for (unsigned int i = 0U; i < 10U; i++)
		m_csd1[i + 10U] = m_node.at(i);

	for (unsigned int i = 0U; i < 10U; i++)
		m_csd2[i + 0U] = m_callsign.at(i);

	for (unsigned int i = 0U; i < 5U; i++) {
		m_csd3[i + 0U]  = m_id.at(i);
		m_csd3[i + 15U] = m_id.at(i);
	}

	for (unsigned int i = 0U; i < 34U; i++)
		m_header[i] = NET_HEADER[i];

	for (unsigned int i = 0U; i < 10U; i++)
		m_header[i + 4U] = m_callsign.at(i);

	for (unsigned int i = 0U; i < 10U; i++)
		m_header[i + 14U] = m_node.at(i);
}

bool CWiresX::start()
{
	m_enable=true;

	return true;
} 

WX_STATUS CWiresX::processVDMODE1(std::string& callsign, const unsigned char* data, unsigned char fi, unsigned char dt, unsigned char fn, unsigned char ft, unsigned char bn, unsigned char bt){
static FILE *fileambe;
static unsigned int size;
static std::string filename;
static unsigned char dch[20];

	size = atoi(m_id.c_str());
	if (m_last_news != size) return WXS_FAIL;

	if (fi == YSF_FI_HEADER) {
		// open file		
		filename = m_storage->StoreVoice(data,(char *)m_source,m_last_news,false);
		fileambe = fopen(filename.c_str(),"wb");
		size=0;
		if (!fileambe) {
			LogMessage("Error writing fileambe.");
			return WXS_NONE;
		} else LogMessage("Writing AMBE file: %s", filename.c_str());
	} else if (fi == YSF_FI_COMMUNICATIONS) {
		// convert and write file
		if (fileambe) m_conv->putYSF_Mode1(data + 35U,fileambe);
		if (fn==3) {
			CYSFPayload payload;
			payload.readVDMode1Data(data + 35U, dch);
		}
		size+=40U;
	} else if (fi == YSF_FI_TERMINATOR) {
		// close file
		m_storage->VoiceEnd(size, dch);
		if (fileambe) fclose(fileambe);
		processVoiceACK();
		m_last_news = 0;
		// // Play message mode 1
		// LogMessage("Playing Voice Message file: %s",filename.c_str());
		// m_ambefile = fopen(filename.c_str(),"rb");
		// if (m_ambefile) {
		// 	LogMessage("File open successfully.");
		// 	m_status = WXSI_PLAY_AMBE;
		// 	return WXS_PLAY;
		// }

	}
	return WXS_NONE;
}


WX_STATUS CWiresX::process(const unsigned char* data, const unsigned char* source, unsigned char fi, unsigned char dt, unsigned char fn, unsigned char ft, unsigned char bn, unsigned char bt)
{
	unsigned char prueba[20];
	unsigned int block_size;
	static unsigned char last_ref=1;
	unsigned char act_ref;
	
	assert(data != NULL);
	assert(source != NULL);
	
	if (!m_enable) 
		return WXS_NONE;

	if (dt != YSF_DT_DATA_FR_MODE)
		return WXS_NONE;

	if (fi != YSF_FI_COMMUNICATIONS)
		return WXS_NONE;

	CYSFPayload payload;
 
	if (fn == 0U) {
		bool valid = payload.readDataFRModeData1(data, prueba);
		if (valid) {	
			::memcpy((char *) m_talky_key, (char *) (prueba+5U),5U);
			::memcpy((char *) m_source, (char *) (prueba+10U),10U);
			m_source[10U]=0;			
			//LogMessage("Source = *%s*",m_source);		
		}
		return WXS_NONE;		
	}

	if (fn == 1U) {
		bool valid = payload.readDataFRModeData2(data, m_command  + (bn * 260U) + 0U);
		if (!valid)
			return WXS_NONE;		
	} else {
		bool valid = payload.readDataFRModeData1(data, m_command + (bn * 260U) + (fn - 2U) * 40U + 20U);
		if (!valid)
			return WXS_NONE;

		valid = payload.readDataFRModeData2(data, m_command + (bn * 260U) + (fn - 2U) * 40U + 40U);
		if (!valid)
			return WXS_NONE;
	}

	if ((fn == ft) && (bn == bt)) {
		unsigned char crc;		
		bool valid = false;

		// Find the end marker
		unsigned int cmd_len = (bn * 260U) + (fn - 1U) * 40U + 20U;
		for (unsigned int i = cmd_len-1; i > 0U; i--) {
			if (m_command[i] == 0x03U) {
				crc = CCRC::addCRC(m_command, i + 1U);
				if (crc == m_command[i + 1U])
					valid = true;
				block_size = i-10U;
				break;
			}
		}

		if (!valid) {
			m_sendNetwork = false;
			if (::memcmp(m_command + 1U, PICT_DATA, 3U) == 0) {
				LogMessage("Error Data Packet receiving picture");
				error_upload= true;
			}
//			CUtils::dump("Not Valid block", m_command, cmd_len);
			LogMessage("Not Valid block");		
			return WXS_NONE;
		}

		m_sendNetwork = true;

#ifdef ANALYZER
		CUtils::dump(" Wires-X command", m_command, cmd_len);
		m_sendNetwork = false;
		return WXS_NONE;
#endif		
		if (::memcmp(m_command + 1U, DX_REQ, 3U) == 0) {
			m_sendNetwork = false;
			//CUtils::dump("DX Command", m_command, cmd_len);
			LogMessage("DX Command");
			processDX(source);
			return WXS_DX;
		} else if (::memcmp(m_command + 1U, ALL_REQ, 3U) == 0) {
			m_sendNetwork = false;
			//CUtils::dump("ALL command", m_command, cmd_len);
			LogMessage("ALL command");
			processAll(source, m_command + 5U);
			return WXS_ALL;
		} else if (::memcmp(m_command + 1U, CONN_REQ, 3U) == 0) {
			m_sendNetwork = false;
//			if (m_noChange) return WXS_NONE;
			//CUtils::dump("Connect command", m_command, cmd_len);
			LogMessage("Connect command");
			return processConnect(source, m_command + 5U);
		} else if (::memcmp(m_command + 1U, NEWS_REQ, 3U) == 0) {
			
			//CUtils::dump("News command", m_command, cmd_len);
			LogMessage("News command");
			if (processNews(source, m_command + 5U)) m_sendNetwork = true;
			else m_sendNetwork = false;
			return WXS_NEWS;
		} else if (::memcmp(m_command + 1U, LIST_REQ, 3U) == 0) {
			//m_sendNetwork = false;
			//CUtils::dump("List for Download command", m_command, cmd_len);
			LogMessage("List for Download command");
			if (processListDown(source, m_command + 5U)) m_sendNetwork = true;
			else m_sendNetwork = false;
			return WXS_LIST;
		} else if (::memcmp(m_command + 1U, GET_RSC, 3U) == 0) {
			//m_sendNetwork = false;
			//CUtils::dump("Get Message command", m_command, cmd_len);
			//LogMessage("Get Message command");
			return processGetMessage(source, m_command + 5U);
			//return WXS_GET_MESSAGE;
		} else if (::memcmp(m_command + 1U, MESSAGE_REC, 3U) == 0) {
			//CUtils::dump("Message Uploading", m_command, cmd_len);
			LogMessage("Message Uploading");
			return processUploadMessage(source, m_command + 5U,0);
		} else if (::memcmp(m_command + 1U, MESSAGE_REC_GPS, 3U) == 0) {
			//CUtils::dump("Message Uploading with GPS", m_command, cmd_len);
			LogMessage("Message Uploading with GPS");
			return processUploadMessage(source, m_command + 5U,1);
		} else if (::memcmp(m_command + 1U, PICT_REC_GPS, 3U) == 0) {
			//CUtils::dump("Picture Uploading with GPS", m_command, cmd_len);
			LogMessage("Picture Uploading with GPS");
			return processUploadPicture(source, m_command + 5U,1);
		} else if (::memcmp(m_command + 1U, PICT_REC, 3U) == 0) {
			//CUtils::dump("Picture Uploading", m_command, cmd_len);
			LogMessage("Picture Uploading");
			return processUploadPicture(source, m_command + 5U,0);
		} else if (::memcmp(m_command + 1U, PICT_BEGIN2, 3U) == 0) {
			LogMessage("Received second picture header.");
			if (m_no_store_picture) m_sendNetwork = true;
			else m_sendNetwork = false;
			last_ref=m_command[25U];			
			return WXS_NONE;			
		}   else if (::memcmp(m_command + 1U, PICT_END, 3U) == 0) {
			LogMessage("Received end of picture.");			
			if (m_no_store_picture) m_sendNetwork = true;
			else {
				m_sendNetwork = false;
				act_ref=m_command[7U];
				if ((last_ref+1)!=act_ref) {
					LogMessage("Out of order picture block: %d!=%d.",last_ref+1,act_ref);
					error_upload= true;
				}
			}	
			return WXS_NONE;			
		} else if (::memcmp(m_command + 1U, PICT_DATA, 3U) == 0) {
			if (m_end_picture) return WXS_NONE;
			act_ref=m_command[7U];
			if ((last_ref+1)!=act_ref) {
				LogMessage("Out of order picture block: %d!=%d.",last_ref+1,act_ref);
				error_upload= true;
			}
			last_ref=act_ref;
			//CUtils::dump("Picture Data", m_command, cmd_len);
			LogMessage("Picture Data. Block size: %u.",block_size);
			if (m_no_store_picture) return WXS_NONE;
			m_sendNetwork = false;
			processDataPicture(m_command + 5U, block_size);
			if (block_size<1027U) {
				processPictureACK(source, m_command + 5U);
			}
			return WXS_NONE;
		} else if (::memcmp(m_command + 1U, DISC_REQ, 3U) == 0) {
			m_sendNetwork = false;
			processDisconnect(source);
			return WXS_DISCONNECT;
		} else if (::memcmp(m_command + 1U, CAT_REQ, 3U) == 0) {
			m_sendNetwork = false;
			processCategory(source, m_command + 5U);
			return WXS_NONE;
		} else if (::memcmp(m_command + 1U, BEACON_REQ_GPS, 3U) == 0) {
			LogMessage("Detected GM with GPS Information Beacon.");
			m_sendNetwork = false;
			return WXS_NONE;
		} else if (::memcmp(m_command + 1U, BEACON_REQ_NOGPS, 3U) == 0) {
			LogMessage("Detected GM without GPS Information Beacon.");
			m_sendNetwork = false;
			return WXS_NONE;			
		} else {
			CUtils::dump("Unknown Wires-X command", m_command, cmd_len);
			return WXS_FAIL;
		}
	}

	return WXS_NONE;
}

unsigned int CWiresX::getDstID()
{
	return m_dstID;
}

unsigned int CWiresX::getTgCount()
{
	return m_count;
}

void CWiresX::setTgCount(int count, std::string name) {
	char tmp[20];
	unsigned int i;

	for (i=0;i<strlen(name.c_str());i++) tmp[i]=name.at(i);
	for (i=strlen(name.c_str());i<16U;i++) tmp[i]=' ';
	tmp[16U]=0x00;

	room_name.assign(tmp);
	m_count = count;
}

std::string CWiresX::getReflector() const
{
	return m_reflector;
}

void CWiresX::setReflectors(CReflectors* reflectors)
{
	m_reflectors = reflectors;
}

void CWiresX::setReflector(std::string reflector, int dstID)
{
	m_reflector = reflector;
	m_dstID = dstID;
	m_count = -1;
}

void CWiresX::processDX(const unsigned char* source)
{
	m_busy = true;
	m_busyTimer.start();
	::LogMessage("Received DX from %10.10s", source);

	m_status = WXSI_DX;
	m_timer.start();
}

void CWiresX::processCategory(const unsigned char* source, const unsigned char* data)
{
	::LogDebug("Received CATEGORY request from %10.10s", source);
	m_busy = true;
	m_busyTimer.start();
	
	char buffer[6U];
	::memcpy(buffer, data + 5U, 2U);
	buffer[3U] = 0x00U;

	unsigned int len = atoi(buffer);

	if (len == 0U)
		return;

	if (len > 20U)
		return;

	m_category.clear();

	for (unsigned int j = 0U; j < len; j++) {
		::memcpy(buffer, data + 7U + j * 5U, 5U);
		buffer[5U] = 0x00U;

		std::string id = std::string(buffer, 5U);
		char tmp_id[6];
		sprintf(tmp_id,"%d",atoi(id.c_str()));

		CReflector* refl = m_reflectors->findById(std::string(tmp_id));
		if (refl)
			m_category.push_back(refl);
	}

	m_status = WXSI_CATEGORY;
	m_timer.start();
}

void CWiresX::processAll(const unsigned char* source, const unsigned char* data)
{
	char buffer[4U];
	::memcpy(buffer, data + 2U, 3U);
	buffer[3U] = 0x00U;
	m_busy = true;
	m_busyTimer.start();
	
	if (data[0U] == '0' && data[1] == '1') {
		::LogDebug("Received ALL for \"%3.3s\" from %10.10s", data + 2U, source);

		m_start = ::atoi(buffer);
		if (m_start > 0U)
			m_start--;

		m_status = WXSI_ALL;

		m_timer.start();
	} else if (data[0U] == '1' && data[1U] == '1') {
		::LogDebug("Received SEARCH for \"%16.16s\" from %10.10s", data + 5U, source);

		m_start = ::atoi(buffer);
		if (m_start > 0U)
			m_start--;

		m_search = std::string((char*)(data + 5U), 16U);

		m_status = WXSI_SEARCH;

		m_timer.start();
	} else if (data[0U] == 'A' && data[1U] == '1') {
		::LogMessage("Received LOCAL NEWS for \"%16.16s\" from %10.10s", data + 0U, source);

		m_start = ::atoi(buffer);
		if (m_start > 0U)
			m_start--;

		m_status = WXSI_LNEWS;

		m_timer.start();
	}
}

bool CWiresX::NewsForMe(const unsigned char* data, const unsigned int offset) {
	char local[6];
	char news_source[6];

    ::memcpy(m_news_source,((const char *)data) + offset,5U);
	::memcpy(news_source,m_news_source,5U);
	news_source[5]=0;
	m_last_news = atoi(news_source);	
	
	sprintf(local,"%05u",atoi(m_id.c_str()));
	if (::strcmp(news_source,local)==0) {
		LogMessage("News for local node.");
		return true;
	} else return false;
}

bool CWiresX::processNews(const unsigned char* source, const unsigned char* data)
{
	if (NewsForMe(data,0U)) {
		m_busy = true;
		m_busyTimer.start();

		::LogMessage("Received NEWS for \"%05d\" from %10.10s",m_last_news, source);		
		m_status = WXSI_NEWS;
		m_timer.start();
		return false;
	}
	else return true;
}

bool CWiresX::processListDown(const unsigned char* source, const unsigned char* data)
{
	char buffer[4U];


	if (NewsForMe(data,0U)) {
		m_busy = true;
		m_busyTimer.start();

		m_type = *(data+10U);
		::memcpy(buffer, data + 16U, 2U);
		buffer[2U] = 0x00U;
		m_start = ::atoi(buffer);

		LogMessage("Received Download resource list item (%u) from \"%05d\", type %c from %10.10s",m_start, m_last_news, m_type, source);
		m_status = WXSI_LIST;
		m_timer.start();
		return false;
	}
	else return true;

}

unsigned char voice_data[200U];

WX_STATUS CWiresX::processGetMessage(const unsigned char* source, const unsigned char* data)
{
	char tmp[6];

	if (NewsForMe(data,0U)) {
		m_busy = true;
		m_busyTimer.start();
			
		::memcpy(tmp,(const char *)(data+10U),5U);
		tmp[5]=0;
		m_number = atoi(tmp);
		::LogMessage("Received Get Message number %s from %10.10s", tmp, source);
		m_sendNetwork = false;

		m_status = WXSI_GET_MESSAGE;
		m_end_picture=false;
		m_timer.start();

		m_storage->GetMessage(voice_data,m_number,m_news_source);
		if (voice_data[0] == 'V') {
			return WXS_PLAY;
		}
		else return WXS_NONE;
	} else {
		m_sendNetwork = true;		
		return WXS_NONE;
	}



}

WX_STATUS CWiresX::processUploadPicture(const unsigned char* source, const unsigned char* data, unsigned int gps)
{
	unsigned int offset;

	m_end_picture=false;
	error_upload=false;

	if (gps) offset = 48U;
	else offset=30U;

	if (NewsForMe(data,offset)) {
		::LogMessage("Received Picture Upload from %10.10s", source);
		m_sendNetwork = false;
		m_timeout.start();
		m_busy = true;
		m_busyTimer.start();
		m_no_store_picture=false;

		if (gps) ::memcpy(m_serial,data+18U,6U);
		else ::memcpy(m_serial,data,6U);

		m_storage->StorePicture(data,source,gps);
		return WXS_PICTURE;
	} else {
		m_sendNetwork = true;
		m_no_store_picture=true;		
		return WXS_NONE;
	}

}

void CWiresX::processDataPicture(const unsigned char* data, unsigned int size)
{
	m_timeout.start();
	m_storage->AddPictureData(data+5U,size,m_news_source);
}

void CWiresX::processPictureACK(const unsigned char* source, const unsigned char* data)
{
	m_busy = true;
	m_busyTimer.start();
	
	m_status = WXSI_UPLOAD_PIC;
//	m_timeout.start();
	m_timer.start(1U,500U);
	m_timeout.stop();
}

void CWiresX::processVoiceACK()
{
	m_busy = true;
	m_busyTimer.start();
	
	m_status = WXSI_SEND_VREPLY;
//	m_timeout.start();
	m_timer.start();
//	m_timeout.stop();
}


WX_STATUS CWiresX::processUploadMessage(const unsigned char* source, const unsigned char* data, unsigned int gps)
{
	unsigned int offset;

	m_end_picture=false;
	error_upload=false;

	if (gps) offset = 48U;
	else offset=30U;

	if (NewsForMe(data,offset)) {
		::LogMessage("Received Message Upload from %10.10s", source);
		m_sendNetwork = false;
		m_busy = true;
		m_busyTimer.start();
		if (gps) ::memcpy(m_serial,data+18U,6U);
		else ::memcpy(m_serial,data,6U);
		m_storage->StoreTextMessage(data,source,gps);
		m_status = WXSI_UPLOAD_TXT;
		m_timer.start();
		return WXS_UPLOAD;
	} else {
		m_sendNetwork = true;		
		return WXS_NONE;
	}
}

WX_STATUS CWiresX::processConnect(const unsigned char* source, const unsigned char* data)
{
	m_busy = true;
	m_busyTimer.start();
	::LogDebug("Received Connect to %5.5s from %10.10s", data, source);
	std::string id = std::string((char*)data, 6U);
	m_dstID = atoi(id.c_str());
	
	return WXS_CONNECT;
}

void CWiresX::SendCReply(void) {
	m_busy = true;
	m_busyTimer.start();
	m_status = WXSI_CONNECT;
	m_timer.start();
}

void CWiresX::SendDReply(void) {
	m_busy = true;
	m_busyTimer.start();	
	m_status = WXSI_DISCONNECT;
	m_timer.start();
}

void CWiresX::processDisconnect(const unsigned char* source)
{
	m_busy = true;
	m_busyTimer.start();
	
	if (source != NULL)
		::LogDebug("Received Disconect from %10.10s", source);

	m_status = WXSI_NONE;
	m_timer.start();
}

void CWiresX::clock(unsigned int ms)
{
	m_timer.clock(ms);
	m_ptimer.clock(ms);
	m_timeout.clock(ms);

	if ((m_ysfWatch.elapsed()>90U) && (m_ambefile!=NULL)) {
		sendAMBEMode1();
		return;
	}
	
	if (m_timer.isRunning() && m_timer.hasExpired()) {
		switch (m_status) {
		case WXSI_DX:
			sendDXReply();
			break;
		case WXSI_LNEWS:
			sendLocalNewsReply();
			break;
		case WXSI_NEWS:
			sendNewsReply();
			break;
		case WXSI_ALL:
			sendAllReply();
			break;
		case WXSI_SEARCH:
			sendSearchReply();
			break;
		case WXSI_CONNECT:
			sendConnectReply();
			break;
		case WXSI_DISCONNECT:
			sendDisconnectReply();
			break;
		case WXSI_CATEGORY:
			sendCategoryReply();
			break;
		case WXSI_LIST:
			sendListReply();
			break;
		case WXSI_UPLOAD_PIC:
			sendUploadReply(true);
			break;
		case WXSI_UPLOAD_TXT:
			sendUploadReply(false);
			break;
		case WXSI_GET_MESSAGE:
			sendGetMessageReply();
			break;
	    // case WXSI_SEND_RCONNECT:
		// 	makeConnect();
		// 	break;
	    // case WXSI_SEND_PREPLY:
		// 	makeEndPicture();
		// 	break;	
		case WXSI_SEND_VREPLY:
			sendUploadVoiceReply();
			break;							
		default:
			break;
		}

		m_status = WXSI_NONE;
		m_timer.stop();
	}
	
	if (m_ptimer.isRunning() && m_ptimer.hasExpired()) {
		switch (m_picture_state) {
		case WXPIC_BEGIN:
				sendPictureBegin();
			break;
		case WXPIC_DATA:
				sendPictureData();
			break;
		case WXPIC_END:
				sendPictureEnd();
				m_end_picture=true;
			break;
		case WXPIC_NONE:
		default:
				m_end_picture=false;
				m_ptimer.stop();
			break;
		}

	}

	if (!(m_no_store_picture) && m_timeout.isRunning() && m_timeout.hasExpired()) {
		LogMessage("Error Timeout receiving Picture");
		m_end_picture=true;
		error_upload= true;
		m_no_store_picture = true;		
		m_storage->PictureEnd(error_upload);
		m_timeout.stop();
	}

	if (m_txWatch.elapsed() > 90U) {
		unsigned char buffer[155U];

		if (!m_bufferTX.isEmpty() && m_bufferTX.dataSize() >= 155U) {
			unsigned char len = 0U;
			m_bufferTX.getData(&len, 1U);
			if (len == 155U) {
				m_bufferTX.getData(buffer, 155U);
				m_network->write(buffer);
			}
		} 
		m_txWatch.start();
	} 

	m_busyTimer.clock(ms);
	if (m_busyTimer.isRunning() && m_busyTimer.hasExpired()) {
		m_busy = false;
		m_busyTimer.stop();
	}
}

void CWiresX::createReply(const unsigned char* data, unsigned int length, const unsigned char* dst_callsign)
{
	assert(data != NULL);
	assert(length > 0U);

	unsigned char bt = 0U;

	if (length > 260U) {
		bt = 1U;
		bt += (length - 260U) / 259U;
	}

	unsigned char ft = calculateFT(length, 0U, 0U);

	unsigned char seqNo = 0U;

	// Write the header
	unsigned char buffer[200U];
	::memcpy(buffer, m_header, 34U);
	
	 if (dst_callsign)
	 	::memcpy(buffer+24U,dst_callsign,10U);	

	CSync::addYSFSync(buffer + 35U);

	CYSFFICH fich;
	fich.load(DEFAULT_FICH);
	fich.setFI(YSF_FI_HEADER);
	fich.setBT(bt);
	fich.setFT(ft);
	fich.encode(buffer + 35U);

	CYSFPayload payload;
	payload.writeDataFRModeData1(m_csd1, buffer + 35U);
	payload.writeDataFRModeData2(m_csd2, buffer + 35U);

	buffer[34U] = seqNo;
	seqNo += 2U;

	writeData(buffer);

	fich.setFI(YSF_FI_COMMUNICATIONS);

	unsigned char fn = 0U;
	unsigned char bn = 0U;
	unsigned int offset = 0U;

	while (offset < length) {		
		switch (fn) {
		case 0U: {
				ft = calculateFT(length, offset, bn);
				payload.writeDataFRModeData1(m_csd1, buffer + 35U);
				payload.writeDataFRModeData2(m_csd2, buffer + 35U);
			}
			break;
		case 1U:
			payload.writeDataFRModeData1(m_csd3, buffer + 35U);
			if (bn == 0U) {
				payload.writeDataFRModeData2(data + offset, buffer + 35U);
				offset += 20U;
			} else {
				// All subsequent entries start with 0x00U
				unsigned char temp[20U];
				::memcpy(temp + 1U, data + offset, 19U);
				temp[0U] = 0x00U;
				payload.writeDataFRModeData2(temp, buffer + 35U);
				offset += 19U;
			}
			break;
		default:
			payload.writeDataFRModeData1(data + offset, buffer + 35U);
			offset += 20U;
			payload.writeDataFRModeData2(data + offset, buffer + 35U);
			offset += 20U;
			break;
		}

		fich.setFT(ft);
		fich.setFN(fn);
		fich.setBT(bt);
		fich.setBN(bn);
		fich.encode(buffer + 35U);

		buffer[34U] = seqNo;
		seqNo += 2U;

		writeData(buffer);

		fn++;
		if (fn >= 8U) {
			fn = 0U;
			bn++;
		}
	}

	// Write the trailer
	fich.setFI(YSF_FI_TERMINATOR);
	fich.setFN(fn);
	fich.setBN(bn);
	fich.encode(buffer + 35U);

	payload.writeDataFRModeData1(m_csd1, buffer + 35U);
	payload.writeDataFRModeData2(m_csd2, buffer + 35U);

	buffer[34U] = seqNo | 0x01U;

	writeData(buffer);
}

void CWiresX::writeData(const unsigned char* buffer)
{
	// Send host Wires-X reply using ring buffer
	unsigned char len = 155U;
	m_bufferTX.addData(&len, 1U);
	m_bufferTX.addData(buffer, len);
//	::LogMessage("Writing buffer.");
	//m_network->write(buffer);
}

unsigned char CWiresX::calculateFT(unsigned int length, unsigned int offset, unsigned int bn) const
{
	unsigned int tmp = length - offset;

	if (bn > 0) tmp++;

	if (length > 220U) return 7U;

	if (length > 180U) return 6U;

	if (length > 140U) return 5U;

	if (length > 100U) return 4U;

	if (length > 60U)  return 3U;

	if (length > 20U)  return 2U;

	return 1U;
}

void CWiresX::sendDXReply()
{
	unsigned char data[150U];
	char tmp[10];
	std::string str;

	//::memset(data, 0x00U, 150U);
	::memset(data, ' ', 128U);

	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
		data[i + 1U] = DX_RESP[i];

	for (unsigned int i = 0U; i < 5U; i++)
		data[i + 5U] = m_id.at(i);

	for (unsigned int i = 0U; i < 10U; i++)
		data[i + 10U] = m_node.at(i);

	for (unsigned int i = 0U; i < 14U; i++)
		data[i + 20U] = m_name.at(i);
	
	CReflector* reflector = m_reflectors->findByName(m_reflector);
	if ((m_reflector.empty()) || (reflector == NULL)) {
		std::string count("000");
		std::string description("              ");
		// data[34U] = '1';
		// data[35U] = '2';

		// data[57U] = '0';
		// data[58U] = '0';
		// data[59U] = '0';
		data[34U] = '1';
		data[35U] = '5';
	
		char tmp1[16];
		snprintf(tmp, sizeof(tmp), "%05d",m_dstID);
		std::string buffAsStdStr = tmp;
		snprintf(tmp1, sizeof(tmp1), "TG%05d",m_dstID);
		std::string name = tmp1;
		name.resize(16U, ' ');

		for (unsigned int i = 0U; i < 5U; i++)
			data[i + 36U] = buffAsStdStr.at(i);

		for (unsigned int i = 0U; i < 16U; i++)
			data[i + 41U] = name.at(i);

		for (unsigned int i = 0U; i < 3U; i++)
			data[i + 57U] = count.at(i);

		for (unsigned int i = 0U; i < 14U; i++)
			data[i + 70U] = description.at(i);

	} else {
		data[34U] = '1';
		data[35U] = '5';


		snprintf(tmp, sizeof(tmp), "%05d",atoi(reflector->m_id.c_str()));
		std::string buffAsStdStr = tmp;
		for (unsigned int i = 0U; i < 5U; i++)
			data[i + 36U] = buffAsStdStr.at(i);

		if (m_count!=-1) {
			// str = std::string(room_name);
			// str.resize(16U, ' ');			
			//LogMessage("Outputing name: *%s*",room_name.c_str());			
			for (unsigned int i = 0U; i < 16U; i++)
				data[i + 41U] = room_name.at(i);

			sprintf(tmp,"%03d",m_count);
			str = std::string(tmp);
			//LogMessage("Outputing m_count: %s",str.c_str());				
			for (unsigned int i = 0U; i < 3U; i++)
				data[i + 57U] = str.at(i);

		} else {

			for (unsigned int i = 0U; i < 16U; i++)
				data[i + 41U] = reflector->m_name.at(i);

			for (unsigned int i = 0U; i < 3U; i++)
				data[i + 57U] = reflector->m_count.at(i);
		}

		for (unsigned int i = 0U; i < 14U; i++)
			data[i + 70U] = reflector->m_desc.at(i);
	}

	unsigned int offset;
	char sign;
	if (m_txFrequency >= m_rxFrequency) {
		offset = m_txFrequency - m_rxFrequency;
		sign = '-';
	} else {
		offset = m_rxFrequency - m_txFrequency;
		sign = '+';
	}

	unsigned int freqHz = m_txFrequency % 1000000U;
	unsigned int freqkHz = (freqHz + 500U) / 1000U;

	char freq[30U];
	::sprintf(freq, "%05u.%03u000%c%03u.%06u", m_txFrequency / 1000000U, freqkHz, sign, offset / 1000000U, offset % 1000000U);

	for (unsigned int i = 0U; i < 23U; i++)
		data[i + 84U] = freq[i];

	data[127U] = 0x03U;			// End of data marker
	data[128U] = CCRC::addCRC(data, 128U);

	//CUtils::dump(1U, "DX Reply", data, 129U);
	LogMessage("DX Reply");

	createReply(data, 129U, NULL);

	m_seqNo++;
}

void CWiresX::sendConnectReply()
{
	char tmp[10];
//	assert(m_reflector != NULL);

	unsigned char data[110U];
	::memset(data, 0x00U, 110U);
	::memset(data, ' ', 90U);

	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
		data[i + 1U] = CONN_RESP[i];

	for (unsigned int i = 0U; i < 5U; i++)
		data[i + 5U] = m_id.at(i);

	for (unsigned int i = 0U; i < 10U; i++)
		data[i + 10U] = m_node.at(i);

	for (unsigned int i = 0U; i < 14U; i++)
		data[i + 20U] = m_name.at(i);

	data[34U] = '1';
	data[35U] = '5';

	CReflector* reflector = m_reflectors->findByName(m_reflector);	
	if ((m_reflector.empty()) || (reflector == NULL)) {
		std::string count("000");
		std::string description("              ");
		char tmp[15];

		snprintf(tmp, sizeof(tmp), "%05d",m_dstID);
		std::string buffAsStdStr = tmp;
		snprintf(tmp, sizeof(tmp), "TG%05d",m_dstID);
		std::string name = tmp;
		name.resize(16U, ' ');

		for (unsigned int i = 0U; i < 5U; i++)
			data[i + 36U] = buffAsStdStr.at(i);

		for (unsigned int i = 0U; i < 16U; i++)
			data[i + 41U] = name.at(i);

		for (unsigned int i = 0U; i < 3U; i++)
			data[i + 57U] = count.at(i);

		for (unsigned int i = 0U; i < 14U; i++)
			data[i + 70U] = description.at(i);
	} else {
		snprintf(tmp, sizeof(tmp), "%05d",atoi(reflector->m_id.c_str()));
		std::string buffAsStdStr = std::string(tmp);	

		for (unsigned int i = 0U; i < 5U; i++)
			data[i + 36U] = buffAsStdStr.at(i);

		for (unsigned int i = 0U; i < 16U; i++)
			data[i + 41U] = reflector->m_name.at(i);

		for (unsigned int i = 0U; i < 3U; i++)
			data[i + 57U] = reflector->m_count.at(i);

		for (unsigned int i = 0U; i < 14U; i++)
			data[i + 70U] = reflector->m_desc.at(i);
	} 

	data[84U] = '0';
	data[85U] = '0';
	data[86U] = '0';
	data[87U] = '0';
	data[88U] = '0';

	data[89U] = 0x03U;			// End of data marker
	data[90U] = CCRC::addCRC(data, 90U);

	//CUtils::dump(1U, "CONNECT Reply", data, 91U);
	LogMessage("CONNECT Reply");

	createReply(data, 91U, NULL);

	m_seqNo++;
}

void CWiresX::sendDisconnectReply()
{
	unsigned char data[110U];
	::memset(data, 0x00U, 110U);
	::memset(data, ' ', 90U);

	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
		data[i + 1U] = DISC_RESP[i];

	for (unsigned int i = 0U; i < 5U; i++)
		data[i + 5U] = m_id.at(i);

	for (unsigned int i = 0U; i < 10U; i++)
		data[i + 10U] = m_node.at(i);

	for (unsigned int i = 0U; i < 14U; i++)
		data[i + 20U] = m_name.at(i);

	data[34U] = '1';
	data[35U] = '2';

	data[57U] = '0';
	data[58U] = '0';
	data[59U] = '0';

	data[89U] = 0x03U;			// End of data marker
	data[90U] = CCRC::addCRC(data, 90U);

	//CUtils::dump(1U, "DISCONNECT Reply", data, 91U);
	LogMessage("DISCONNECT Reply");

	createReply(data, 91U, NULL);

	m_seqNo++;
}

void CWiresX::sendAllReply()
{
	unsigned char data[1100U];

	if (m_start == 0U)
		m_reflectors->reload();

	std::vector<CReflector*>& curr = m_reflectors->current();


	::memset(data, 0x00U, 1100U);

	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
		data[i + 1U] = ALL_RESP[i];

	data[5U] = '2';
	data[6U] = '1';

	for (unsigned int i = 0U; i < 5U; i++)
		data[i + 7U] = m_id.at(i);

	for (unsigned int i = 0U; i < 10U; i++)
		data[i + 12U] = m_node.at(i);

	unsigned int total = curr.size();
	if (total > 999U) total = 999U;
	LogMessage("Tamano: %d",total);

	unsigned int n = curr.size() - m_start;
	if (n > 20U) n = 20U;

	::sprintf((char*)(data + 22U), "%03u%03u", n, total);

	data[28U] = 0x0DU;

	unsigned int offset = 29U;
	for (unsigned int j = 0U; j < n; j++, offset += 50U) {
		CReflector* refl = curr.at(j + m_start);
		
		char tmp_id[6];
		sprintf(tmp_id,"%05d",atoi(refl->m_id.c_str()));		

		::memset(data + offset, ' ', 50U);

		data[offset + 0U] = '5';

		for (unsigned int i = 0U; i < 5U; i++)
			data[i + offset + 1U] = tmp_id[i];

		for (unsigned int i = 0U; i < 16U; i++)
			data[i + offset + 6U] = refl->m_name.at(i);

		for (unsigned int i = 0U; i < 3U; i++)
			data[i + offset + 22U] = refl->m_count.at(i);

		for (unsigned int i = 0U; i < 10U; i++)
			data[i + offset + 25U] = ' ';

		for (unsigned int i = 0U; i < 14U; i++)
			data[i + offset + 35U] = refl->m_desc.at(i);

		data[offset + 49U] = 0x0DU;
	}

	unsigned int k = 1029U - offset;
	for(unsigned int i = 0U; i < k; i++)
		data[i + offset] = 0x20U;

	offset += k;

	data[offset + 0U] = 0x03U;			// End of data marker
	data[offset + 1U] = CCRC::addCRC(data, offset + 1U);

	//CUtils::dump(1U, "ALL Reply", data, offset + 2U);
	LogMessage("ALL Reply");

	createReply(data, offset + 2U, NULL);

	m_seqNo++;
}

void CWiresX::sendLocalNewsReply()
{
	unsigned char data[1100U];
	::memset(data, 0x00U, 1100U);

	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
		data[i + 1U] = LNEWS_RESP[i];

	unsigned int n=1U;
	unsigned int total=1U;

	::sprintf((char*)(data + 5U), "%02u", total+1U);

	for (unsigned int i = 0U; i < 5U; i++)
		data[i + 7U] = m_id.at(i);

	for (unsigned int i = 0U; i < 10U; i++)
		data[i + 12U] = m_node.at(i);

	::sprintf((char*)(data + 22U), "A%02u%03u", n, total);

	data[28U] = 0x0DU;

	unsigned int offset = 29U;

		::memset(data + offset, ' ', 50U);

		data[offset + 0U] = '3';


			for (unsigned int i = 0U; i < 5U; i++)
				 data[i + offset + 1U] = m_id.at(i);


			for (unsigned int i = 0U; i < 10U; i++)
					data[i + offset + 6U] = m_node.at(i);

		for (unsigned int i = 0U; i < 6U; i++)
			data[i + offset + 16U] = ' ';

		::sprintf((char*)(data + offset + 22U), "%03u", 1);
		for (unsigned int i = 0U; i < 10U; i++)
				data[i + offset + 25U] = m_callsign.at(i);		
		//::sprintf((char*)(data + offset + 25U), "EA7EE     ");
		for (unsigned int i = 0U; i < 14U; i++)
				data[i + offset + 35U] = m_location.at(i);		
		//::sprintf((char*)(data + offset + 35U), "Huelva        ");

		data[offset + 49U] = 0x0DU;
		offset+=50U;

	//::LogMessage("Sending Local News Request");

	data[offset + 0U] = 0x03U;			// End of data marker
	data[offset + 1U] = CCRC::addCRC(data, offset + 1U);

//	CUtils::dump("Local NEWS Reply", data, offset + 2U);
	LogMessage("Local NEWS Reply");

	createReply(data, offset + 2U, m_source);

	m_seqNo++;
}

void CWiresX::sendNewsReply()
{
	unsigned char data[1100U];
	::memset(data, 0x00U, 1100U);

	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
		data[i + 1U] = NEWS_RESP[i];

	::sprintf((char*)(data + 5U), "01");

	for (unsigned int i = 0U; i < 5U; i++)
		data[i + 7U] = m_news_source[i];

	::sprintf((char*)(data + 12U), "     00000");
	data[22U] = 0x0DU;

	unsigned int offset = 23U;

	::LogMessage("Sending News Request");

	data[offset + 0U] = 0x03U;			// End of data marker
	data[offset + 1U] = CCRC::addCRC(data, offset + 1U);

	//CUtils::dump("NEWS Reply", data, offset + 2U);
	LogMessage("NEWS Reply");

	createReply(data, offset + 2U, m_source);

	m_seqNo++;
}

void CWiresX::sendListReply()
{
	unsigned int offset;
	unsigned char data[1100U];
	::memset(data, 0x00U, 1100U);

	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
		data[i + 1U] = LIST_RESP[i];

	offset=5U;
	::LogMessage("Sending Download List for type %c.",m_type);
	offset+=m_storage->GetList(data+5U,m_type,m_news_source,m_start);

	data[offset + 0U] = 0x03U;			// End of data marker
	data[offset + 1U] = CCRC::addCRC(data, offset + 1U);

//	CUtils::dump("Message List Reply", data, offset + 2U);
	LogMessage("Message List Reply");

	createReply(data, offset + 2U, m_source);

	m_seqNo++;
}

void CWiresX::sendGetMessageReply()
{
	unsigned char data[1100U];
	char name[40U];
	char tmp[20U];	
	unsigned int offset;
	bool valid;

	::memset(data, 0x00U, 1100U);

	if (voice_data[0] =='V') {
		m_end_picture=true;

		strcpy(name,(char *)(voice_data+1));
		// Play message mode 1
		LogMessage("Playing Voice Message file: %s",name);
		m_ambefile = fopen(name,"rb");
		if (m_ambefile) {
			LogMessage("File open successfully.");
			m_status = WXSI_PLAY_AMBE;
		} else m_status = WXSI_NONE;

		offset = 100U;
		voice_data[offset] = m_seqNo;
		m_seqNo++;
		memcpy(voice_data+offset+1,VOICE_RESP,4U);
		offset+=5U;
		memcpy(voice_data+offset,voice_mark,14U);
		offset+=14U;
		sprintf(tmp,"    %05d",atoi((char *)m_news_source));
		memcpy(voice_data+offset,tmp,9U);
		offset+=9U;
		sprintf(tmp,"     %05d",m_number);
		memcpy(voice_data+offset,tmp,10U);


		offset=194U;
		voice_data[offset] = 0x03U;			// End of data marker
		voice_data[offset+1] = CCRC::addCRC(voice_data+100U, 95U);

//		CUtils::dump(1U,"Voice Data Block",voice_data+100U,100U);

	} else {
		offset=5U;
		offset+=m_storage->GetMessage(data,m_number,m_news_source);
		if (offset!=5U) valid=true;
		else valid=false;

		if (data[0]=='T') {
			m_end_picture=true;
			data[0U] = m_seqNo;

			for (unsigned int i = 0U; i < 4U; i++)
				data[i + 1U] = GET_MSG_RESP[i];

//			::LogMessage("Sending Message Request");

			data[offset + 0U] = 0x03U;			// End of data marker
			data[offset + 1U] = CCRC::addCRC(data, offset + 1U);

			//CUtils::dump("Message Reply", data, offset + 2U);
			LogMessage("Message Reply");

			createReply(data, offset + 2U, m_source);
			m_seqNo++;
		} else {
			data[0U] = m_seqNo;

			for (unsigned int i = 0U; i < 4U; i++)
				data[i + 1U] = PICT_PREAMB_RESP[i];

			::LogMessage("Sending Picture Header Request");

			data[offset + 0U] = 0x03U;			// End of data marker
			data[offset + 1U] = CCRC::addCRC(data, offset + 1U);

//			CUtils::dump("First Picture Preamble Reply", data, offset + 2U);
			LogMessage("First Picture Preamble Reply");
			m_seqNo+=2;

			createReply(data, offset + 2U, m_source);

			// Return if not valid resource
			if (!valid) m_picture_state = WXPIC_NONE;
			else m_picture_state = WXPIC_BEGIN;
			m_ptimer.start(1,500);
		}
	}
}


void CWiresX::sendPictureBegin()
{
	unsigned char data[100U];
	unsigned int offset;

	::memset(data, 0x00U, 100U);

	offset=5U;
	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
		data[i + 1U] = PICT_BEGIN_RESP_GPS[i];

	offset+=m_storage->GetPictureHeader(data,m_number,m_news_source);
	if (offset==5U) return;

	data[offset + 0U] = 0x03U;			// End of data marker
	data[offset + 1U] = CCRC::addCRC(data, offset + 1U);

//	CUtils::dump("Second Picture Preamble Reply", data, offset + 2U);
	LogMessage("Second Picture Preamble Reply");
	m_seqNo++;

	createReply(data, offset + 2U, m_source);

	m_pcount=0;
	m_picture_state = WXPIC_DATA;
	m_ptimer.start(1,500);
}

void CWiresX::sendPictureData()
{
	unsigned char data[1100U];
	::memset(data, 0x00U, 1100U);

	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
	data[i + 1U] = PICT_DATA_RESP[i];

	m_offset=m_storage->GetPictureData(data+5U,m_pcount);
	m_pcount+=m_offset;

	data[m_offset + 10U] = 0x03U;			// End of data marker
	data[m_offset + 11U] = CCRC::addCRC(data, m_offset + 11U);

	LogMessage("Block size: %d",m_offset+3);
//	CUtils::dump("Picture Data Reply", data, m_offset + 12U);
	//LogMessage("Picture Data Reply");
	m_seqNo++;

	createReply(data, m_offset + 12U, m_source);

   if (m_offset == 1024U) {
	   m_picture_state = WXPIC_DATA;
	   m_ptimer.start(3,500);
   }
   else {
	   m_picture_state = WXPIC_END;
	   //int time = (m_offset*5000U)/1024U;
	   //m_ptimer.start(time/1000U,time%1000U);
	   m_ptimer.start(3,500);	   
   }
}

void CWiresX::sendPictureEnd()
{
	unsigned char data[20U];
	unsigned int sum;

	::memset(data, 0x00U, 20U);

	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
		data[i + 1U] = PICT_END_RESP[i];

	unsigned char num_seq=m_storage->GetPictureSeq();

	char tmp[7]={0x50,0x00,0x01,0x00,0x05,0xCA,0x82};
	tmp[2U]=num_seq+1;
	// Put sum of all bytes
	sum = m_storage->GetSumCheck();
	tmp[4]=(sum>>16)&0xFF;
	tmp[5]=(sum>>8)&0xFF;
	tmp[6]=sum&0xFF;
	LogMessage("Sum of bytes: %u.",sum);
	::memcpy(data+5U,tmp,7U);

	data[12] = 0x03U;			// End of data marker
	data[13] = CCRC::addCRC(data, 13U);

//	CUtils::dump("Picture End Reply", data, 14U);
	LogMessage("Picture End Reply");
	m_seqNo++;

	createReply(data, 14U, m_source);
	m_picture_state=WXPIC_NONE;
	m_ptimer.start(1);
}


void CWiresX::sendUploadReply(bool pict)
{
	unsigned char data[1100U];
	::memset(data, 0x00U, 1100U);



	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
		data[i + 1U] = UP_ACK[i];

	if (pict) {
		m_end_picture=true;		
		m_storage->PictureEnd(error_upload);
		if (error_upload) data[2U]=0x31;
	}

	::memcpy(data+5U,m_serial,6U);

	::memcpy(data+11U,m_talky_key,5U);
	::memcpy(data+16U,m_source,10U);

	unsigned int offset = 26U;

	data[offset + 0U] = 0x03U;			// End of data marker
	data[offset + 1U] = CCRC::addCRC(data, offset + 1U);

//	CUtils::dump("Upload ACK", data, offset + 2U);
	LogMessage("Upload ACK");

	createReply(data, offset + 2U, m_source);

	m_seqNo++;
}

void CWiresX::sendUploadVoiceReply()
{
	unsigned char data[30U];
	static unsigned int index=1;
	::memset(data, 0x00U, 30U);

	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
		data[i + 1U] = VOICE_ACK[i];

	::sprintf((char *)(data+5U),"01");  //num of items

	for (unsigned int i = 0U; i < 5U; i++)
		data[i + 7U] = m_id.at(i);	

	::sprintf((char *)(data+12U),"      %05d",index); //select item of items
	data[23U]=0x0DU;
	index++;

	::LogMessage("Sending Voice Upload ACK");

	data[24U] = 0x03U;			// End of data marker
	data[25U] = CCRC::addCRC(data, 25U);

//	CUtils::dump("Upload Voice ACK", data, 26U);
	LogMessage("Upload Voice ACK");

	createReply(data, 26U, m_source);

	m_seqNo++;
}

void CWiresX::sendSearchReply()
{
	if (m_search.size() == 0U) {
		sendSearchNotFoundReply();
		return;
	}

	std::vector<CReflector*>& search = m_reflectors->search(m_search);
	if (search.size() == 0U) {
		sendSearchNotFoundReply();
		return;
	}

	unsigned char data[1100U];
	::memset(data, 0x00U, 1100U);

	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
		data[i + 1U] = ALL_RESP[i];

	data[5U] = '0';
	data[6U] = '2';

	for (unsigned int i = 0U; i < 5U; i++)
		data[i + 7U] = m_id.at(i);

	for (unsigned int i = 0U; i < 10U; i++)
		data[i + 12U] = m_node.at(i);

	data[22U] = '1';

	unsigned int total = search.size();
	if (total > 999U) total = 999U;

	unsigned int n = search.size() - m_start;
	if (n > 20U) n = 20U;

	::sprintf((char*)(data + 23U), "%02u%03u", n, total);

	data[28U] = 0x0DU;

	unsigned int offset = 29U;
	for (unsigned int j = 0U; j < n; j++, offset += 50U) {
		CReflector* refl = search.at(j + m_start);
		char tmp_id[6];
		sprintf(tmp_id,"%05d",atoi(refl->m_id.c_str()));
		
		::memset(data + offset, ' ', 50U);

		data[offset + 0U] = '1';

		for (unsigned int i = 0U; i < 5U; i++)
			data[i + offset + 1U] = tmp_id[i];

		for (unsigned int i = 0U; i < 16U; i++)
			data[i + offset + 6U] = refl->m_name.at(i);

		for (unsigned int i = 0U; i < 3U; i++)
			data[i + offset + 22U] = refl->m_count.at(i);

		for (unsigned int i = 0U; i < 10U; i++)
			data[i + offset + 25U] = ' ';

		for (unsigned int i = 0U; i < 14U; i++)
			data[i + offset + 35U] = refl->m_desc.at(i);

		data[offset + 49U] = 0x0DU;
	}

	unsigned int k = 1029U - offset;
	for(unsigned int i = 0U; i < k; i++)
		data[i + offset] = 0x20U;

	offset += k;

	data[offset + 0U] = 0x03U;			// End of data marker
	data[offset + 1U] = CCRC::addCRC(data, offset + 1U);

	//CUtils::dump(1U, "SEARCH Reply", data, offset + 2U);
	LogMessage("SEARCH Reply");

	createReply(data, offset + 2U, NULL);

	m_seqNo++;
}

void CWiresX::sendSearchNotFoundReply()
{
	unsigned char data[70U];
	::memset(data, 0x00U, 70U);

	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
		data[i + 1U] = ALL_RESP[i];

	data[5U] = '0';
	data[6U] = '1';

	for (unsigned int i = 0U; i < 5U; i++)
		data[i + 7U] = m_id.at(i);

	for (unsigned int i = 0U; i < 10U; i++)
		data[i + 12U] = m_node.at(i);

	data[22U] = '1';
	data[23U] = '0';
	data[24U] = '0';
	data[25U] = '0';
	data[26U] = '0';
	data[27U] = '0';

	data[28U] = 0x0DU;

	data[29U] = 0x03U;			// End of data marker
	data[30U] = CCRC::addCRC(data, 30U);

	//CUtils::dump(1U, "SEARCH Reply", data, 31U);
	LogMessage("SEARCH Not Found Reply");

	createReply(data, 31U, NULL);

	m_seqNo++;
}

void CWiresX::sendCategoryReply()
{
	unsigned char data[1100U];
	::memset(data, 0x00U, 1100U);

	data[0U] = m_seqNo;

	for (unsigned int i = 0U; i < 4U; i++)
		data[i + 1U] = ALL_RESP[i];

	data[5U] = '2';
	data[6U] = '1';

	for (unsigned int i = 0U; i < 5U; i++)
		data[i + 7U] = m_id.at(i);

	for (unsigned int i = 0U; i < 10U; i++)
		data[i + 12U] = m_node.at(i);

	unsigned int n = m_category.size();
	if (n > 20U)
		n = 20U;

	::sprintf((char*)(data + 22U), "%03u%03u", n, n);

	data[28U] = 0x0DU;

	unsigned int offset = 29U;
	for (unsigned int j = 0U; j < n; j++, offset += 50U) {
		CReflector* refl = m_category.at(j);
		char tmp_id[6];
		sprintf(tmp_id,"%05d",atoi(refl->m_id.c_str()));
		
		::memset(data + offset, ' ', 50U);

		data[offset + 0U] = '5';

		for (unsigned int i = 0U; i < 5U; i++)
			data[i + offset + 1U] = tmp_id[i];

		for (unsigned int i = 0U; i < 16U; i++)
			data[i + offset + 6U] = refl->m_name.at(i);

		for (unsigned int i = 0U; i < 3U; i++)
			data[i + offset + 22U] = refl->m_count.at(i);

		for (unsigned int i = 0U; i < 10U; i++)
			data[i + offset + 25U] = ' ';

		for (unsigned int i = 0U; i < 14U; i++)
			data[i + offset + 35U] = refl->m_desc.at(i);

		data[offset + 49U] = 0x0DU;
	}

	unsigned int k = 1029U - offset;
	for(unsigned int i = 0U; i < k; i++)
		data[i + offset] = 0x20U;

	offset += k;

	data[offset + 0U] = 0x03U;			// End of data marker
	data[offset + 1U] = CCRC::addCRC(data, offset + 1U);

	//CUtils::dump(1U, "CATEGORY Reply", data, offset + 2U);
	LogMessage("CATEGORY Reply");

	createReply(data, offset + 2U, NULL);

	m_seqNo++;
}

bool CWiresX::isBusy() const
{
	return m_busy;
}

bool CWiresX::EndPicture()
{
	return m_end_picture;
}

// void CWiresX::SendPReply(CYSFNetwork* ysfNetwork) {
// 	m_ysfNetwork = ysfNetwork;
// 	m_status = WXSI_SEND_PREPLY;
// 	m_timer.start();
// } 

// void CWiresX::makeEndPicture()
// {
// 	unsigned char data[20U];
// 	unsigned int sum;

// 	::memset(data, 0x00U, 20U);

// 	data[0U] = m_seqNo;

// 	for (unsigned int i = 0U; i < 4U; i++)
// 		data[i + 1U] = PICT_END_RESP[i];

// 	unsigned char num_seq=m_storage->GetPictureSeq();

// 	char tmp[7]={0x50,0x00,0x01,0x00,0x05,0xCA,0x82};
// 	tmp[2U]=num_seq+1;
// 	// Put sum of all bytes
// 	sum = m_storage->GetSumCheck();
// 	tmp[4]=(sum>>16)&0xFF;
// 	tmp[5]=(sum>>8)&0xFF;
// 	tmp[6]=sum&0xFF;
// 	LogMessage("Sum of bytes: %u.",sum);
// 	::memcpy(data+5U,tmp,7U);

// 	data[12] = 0x03U;			// End of data marker
// 	data[13] = CCRC::addCRC(data, 13U);

// 	//CUtils::dump("Picture End Reply", data, 14U);
// 	LogMessage("Picture End Reply");
// 	m_seqNo++;

// 	createReply(data, 14U, m_source);
// 	m_picture_state=WXPIC_NONE;
// 	m_ptimer.start(1);
// }

// char block_TG[]={0x00U,0x5DU,0x23U,0x5FU,0x25U,0x30U,0x30U,0x30U,0x30U,0x30U,0x20U,0x20U,0x20U,0x20U,0x20U,0x03,0xD0};

// void CWiresX::makeConnect() {
// unsigned char buf[20];
// char tmp[6];

//   memcpy(buf,block_TG,17U);
//   sprintf(tmp,"%05d",m_dstID);
//   memcpy(buf+5U,tmp,5U);
 
//   buf[15U] = 0x03U;			// End of data marker
//   buf[16U] = CCRC::addCRC(buf, 16U);
//   makePacket(m_ysfNetwork,buf,17U);
// }

// void CWiresX::makePacket(CYSFNetwork* ysfNetwork, unsigned char *data, unsigned int length)
// {
// 	assert(ysfNetwork != NULL);
// 	assert(data != NULL);
// 	assert(length > 0U);

// 	unsigned char bt = 0U;

// 	if (length > 260U) {
// 		bt = 1U;
// 		bt += (length - 260U) / 259U;

// 		length += bt;
// 	}

// 	if (length > 20U) {
// 		unsigned int blocks = (length - 20U) / 40U;
// 		if ((length % 40U) > 0U) blocks++;
// 		length = blocks * 40U + 20U;
// 	} else {
// 		length = 20U;
// 	}

// 	unsigned char ft = calculateFT(length, 0U);

// 	unsigned char seqNo = 0U;

// 	// Write the header
// 	unsigned char buffer[200U];
// 	::memcpy(buffer, m_header, 34U);

// 	CSync::addYSFSync(buffer + 35U);

// 	CYSFFICH fich;
// 	fich.load(DEFAULT_FICH);
// 	fich.setFI(YSF_FI_HEADER);
// 	fich.setBT(bt);
// 	fich.setFT(ft);
// 	fich.encode(buffer + 35U);

// 	CYSFPayload payload;
// 	payload.writeDataFRModeData1(m_csd1, buffer + 35U);
// 	payload.writeDataFRModeData2(m_csd2, buffer + 35U);

// 	buffer[34U] = seqNo;
// 	seqNo += 2U;

// 	writeData(buffer);
// //	ysfNetwork->write(buffer);

// 	fich.setFI(YSF_FI_COMMUNICATIONS);

// 	unsigned char fn = 0U;
// 	unsigned char bn = 0U;

// 	unsigned int offset = 0U;
// 	while (offset < length) {
// 		switch (fn) {
// 		case 0U: {
// 				ft = calculateFT(length, offset);
// 				payload.writeDataFRModeData1(m_csd1, buffer + 35U);
// 				payload.writeDataFRModeData2(m_csd2, buffer + 35U);
// 			}
// 			break;
// 		case 1U:
// 			payload.writeDataFRModeData1(m_csd3, buffer + 35U);
// 			if (bn == 0U) {
// 				payload.writeDataFRModeData2(data + offset, buffer + 35U);
// 				offset += 20U;
// 			} else {
// 				// All subsequent entries start with 0x00U
// 				unsigned char temp[20U];
// 				::memcpy(temp + 1U, data + offset, 19U);
// 				temp[0U] = 0x00U;
// 				payload.writeDataFRModeData2(temp, buffer + 35U);
// 				offset += 19U;
// 			}
// 			break;
// 		default:
// 			payload.writeDataFRModeData1(data + offset, buffer + 35U);
// 			offset += 20U;
// 			payload.writeDataFRModeData2(data + offset, buffer + 35U);
// 			offset += 20U;
// 			break;
// 		}

// 		fich.setFT(ft);
// 		fich.setFN(fn);
// 		fich.setBT(bt);
// 		fich.setBN(bn);
// 		fich.encode(buffer + 35U);

// 		buffer[34U] = seqNo;
// 		seqNo += 2U;
		
// 	writeData(buffer);
// //		ysfNetwork->write(buffer);

// 		fn++;
// 		if (fn >= 8U) {
// 			fn = 0U;
// 			bn++;
// 		}
// 	}

// 	// Write the trailer
// 	fich.setFI(YSF_FI_TERMINATOR);
// 	fich.setFN(fn);
// 	fich.setBN(bn);
// 	fich.encode(buffer + 35U);

// 	payload.writeDataFRModeData1(m_csd1, buffer + 35U);
// 	payload.writeDataFRModeData2(m_csd2, buffer + 35U);

// 	buffer[34U] = seqNo | 0x01U;
	
// 	writeData(buffer);
// //	ysfNetwork->write(buffer);
// }

bool CWiresX::sendNetwork(void) {
		
	return m_sendNetwork;
}

void CWiresX::sendAMBEMode1(void) {
unsigned char buffer[40U];
unsigned char dch[20U];
unsigned int count,fn;
static bool start=true;
static unsigned int ysf_cnt,offset;

    //LogMessage("Send AMBE");
	if (start) {
		ysf_cnt=1;
		LogMessage("Start NEWS AUDIO Playing...");
		m_conv->putDMRHeaderV1();
		fread(buffer,1U,40U,m_ambefile);
		memset(dch,'*',YSF_CALLSIGN_LENGTH);
		memcpy(dch+YSF_CALLSIGN_LENGTH,m_node.c_str(),YSF_CALLSIGN_LENGTH);
		m_conv->AMB2YSF_Mode1(buffer);
		m_conv->AMB2YSF_Mode1(buffer+8U);
		m_conv->AMB2YSF_Mode1(buffer+16U);
		m_conv->AMB2YSF_Mode1(buffer+24U);
		m_conv->AMB2YSF_Mode1(buffer+32U);	
		m_conv->putDCHV1(dch);
		m_ysfWatch.start();
		start = false;
		return;
	} else {
		//LogMessage("New Packet...");
		count = fread(buffer,1U,40U,m_ambefile);
		if (count < 40U) {
			LogMessage("Finish voice playing.");
			fclose(m_ambefile);
			m_ambefile = NULL;
			m_conv->putDMREOTV1(true);
			m_status = WXSI_NONE;
			//m_ysfWatch.stop();
			start = true;
			return;
		}
		fn = ysf_cnt % 8U;
		switch (fn) {
			case 0:
			// Callsign of node
				memset(dch,'*',YSF_CALLSIGN_LENGTH);
				memcpy(dch+YSF_CALLSIGN_LENGTH,m_node.c_str(),YSF_CALLSIGN_LENGTH);
				break;
			case 1:
			// Callsign of source
				memcpy(dch,m_callsign.c_str(),YSF_CALLSIGN_LENGTH);
				memset(dch+YSF_CALLSIGN_LENGTH,0x20,YSF_CALLSIGN_LENGTH);
				//memcpy(dch+YSF_CALLSIGN_LENGTH,m_callsign.c_str(),YSF_CALLSIGN_LENGTH);
				break;
			case 2:
			// Number of repeater
				memcpy(dch,m_id.c_str(),YSF_CALLSIGN_LENGTH);
				memcpy(dch+YSF_CALLSIGN_LENGTH,m_id.c_str(),YSF_CALLSIGN_LENGTH);			
				break; 
			case 3:
				offset = 100U;
				memcpy(dch,(char *)(voice_data+offset),YSF_CALLSIGN_LENGTH*2);
				offset += 20U;
				break;									
			default:	
				memcpy(dch,(char *)(voice_data+offset),YSF_CALLSIGN_LENGTH*2);
				offset += 20U;
				break;
		}
		m_conv->AMB2YSF_Mode1(buffer);
		m_conv->AMB2YSF_Mode1(buffer+8U);
		m_conv->AMB2YSF_Mode1(buffer+16U);
		m_conv->AMB2YSF_Mode1(buffer+24U);
		m_conv->AMB2YSF_Mode1(buffer+32U);
		m_conv->putDCHV1(dch);
		ysf_cnt++;
		m_ysfWatch.start();
	}
}

// void CWiresX::getMode1DCH(unsigned char *dch,unsigned int fn) {
// static unsigned int offset;

// 	switch (fn) {
// 		case 0:
// 		// Callsign of node
// 			memset(dch,'*',YSF_CALLSIGN_LENGTH);
// 			memcpy(dch+YSF_CALLSIGN_LENGTH,m_node.c_str(),YSF_CALLSIGN_LENGTH);
// 			break;
// 		case 1:
// 		// Callsign of source
// 			memcpy(dch,m_callsign.c_str(),YSF_CALLSIGN_LENGTH);
// 			memset(dch+YSF_CALLSIGN_LENGTH,0x20,YSF_CALLSIGN_LENGTH);
// 			//memcpy(dch+YSF_CALLSIGN_LENGTH,m_callsign.c_str(),YSF_CALLSIGN_LENGTH);
// 			break;
// 		case 2:
// 		// Number of repeater
// 			memcpy(dch,m_id.c_str(),YSF_CALLSIGN_LENGTH);
// 			memcpy(dch+YSF_CALLSIGN_LENGTH,m_id.c_str(),YSF_CALLSIGN_LENGTH);			
// 			break; 
// 		case 3:
// 			offset = 100U;
// 			memcpy(dch,(char *)(voice_data+offset),YSF_CALLSIGN_LENGTH*2);
// 			offset += 20U;
// 			break;									
// 		default:	
//  			memcpy(dch,(char *)(voice_data+offset),YSF_CALLSIGN_LENGTH*2);
// 			offset += 20U;
// 			break;
// 	}					
	
// }
