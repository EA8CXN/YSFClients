/*
*   Copyright (C) 2016,2017 by Jonathan Naylor G4KLX
*   Copyright (C) 2018 by Andy Uribe CA6JAU
*   Copyright (C) 2018,2020 by Manuel Sanchez EA7EE
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

#include "Streamer.h"
//#include "Timer.h"
#include "StopWatch.h"
#include "YSFFICH.h"
#include "Sync.h"
#include "Log.h"
#include "Utils.h"

#include <unistd.h>

// #include <cstdio>
// #include <cstdlib>
// #include <cstring>
// #include <cctype>
// #include <time.h>

// DT1 and DT2, suggested by Manuel EA7EE
const unsigned char dt1_temp[] = {0x31, 0x22, 0x62, 0x5F, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char dt2_temp[] = {0x00, 0x00, 0x00, 0x00, 0x6C, 0x20, 0x1C, 0x20, 0x03, 0x08};
unsigned char m_gps_buffer[20U];

char std_ysf_radioid[] = {'*', '*', '*', '*', '*'};
char ysf_radioid[5];

CStreamer::CStreamer(CConf *conf) :
m_conf(conf),
m_writer(NULL),
m_gps(NULL),
m_APRS(NULL),

m_beacon(false),
m_beacon_name("/usr/local/sbin/beacon.amb"),

m_conv(),

m_modemNetwork(NULL),
m_ysfNetwork(NULL),
m_fcsNetwork(NULL),
m_dmrNetwork(NULL),
m_saveAMBE(false),
m_lookup(NULL),
m_storage(NULL),
m_wiresX(NULL),
m_dtmf(NULL),
m_colorcode(1U),
m_srcid(1U),
m_defsrcid(1U),
m_dstid(1U),
m_EmbeddedLC(),
m_dmrFrames(0U),
m_ysfFrames(0U),
m_rpt_buffer(50000U, "RPTGATEWAY"),
m_networkWatchdog(1000U, 0U, 500U)
{
	//m_conv.reset();
    //m_conf = conf;

	m_ysfFrame = new unsigned char[200U];
	m_dmrFrame = new unsigned char[50U];
	
	::memset(m_ysfFrame, 0U, 200U);
	::memset(m_dmrFrame, 0U, 50U);

    unsigned int lev_a = m_conf->getAMBECompA();
	unsigned int lev_b = m_conf->getAMBECompB();
	m_conv.LoadTable(lev_a,lev_b);
    m_ysfNetworkEnabled = m_conf->getYSFNetworkEnabled();
	m_fcsNetworkEnabled = m_conf->getFCSNetworkEnabled();
	m_dmrNetworkEnabled = m_conf->getDMRNetworkEnabled();
    m_saveAMBE		 	  = m_conf->getSaveAMBE();	
	m_dstid 	= 		atoi(m_conf->getNetworkStartup().c_str());
	
	LogInfo("    AMBE Recording: %s", m_saveAMBE ? "yes" : "no");   

	std::string lookupFile = m_conf->getDMRIdLookupFile();
	if (lookupFile.empty()) lookupFile  = "/usr/local/etc/DMRIds.dat";
	m_lookup = new CDMRLookup(lookupFile,m_conf->getNetworkReloadTime());
	m_lookup->read();		   
}

CStreamer::~CStreamer() {

	if (m_APRS != NULL) {
		m_APRS->stop();
		delete m_APRS;
	}

	if (m_gps != NULL) {
		m_writer->close();
		delete m_writer;
		delete m_gps;
	}

	delete m_wiresX;
    delete m_storage;

    delete[] m_ysfFrame;
	delete[] m_dmrFrame;
}

void CStreamer::setBeacon(CTimer *inactivityTimer, CTimer *lost_timer, bool NoChange, unsigned int dgid) {

    std::string file = m_conf->getBeaconPath();
    m_inactivityTimer = inactivityTimer;
    m_lostTimer = lost_timer;

	m_beacon_time = m_conf->getBeaconTime();
	if (!file.empty()) m_beacon_name = file;
	m_file_out=fopen(m_beacon_name.c_str(),"rb");
	if (!m_file_out || (m_beacon_time==0)) {
		LogMessage("Beacon off");
		m_beacon = false;
	} else {
		fclose(m_file_out);
		LogMessage("Beacon on. Timeout: %d minutes",m_beacon_time);
		m_beacon = true;
		m_beacon_Watch.start();
	}

    m_beacon_status = BE_OFF;
	m_NoChange = NoChange;
	m_DGID = dgid;

}

std::string CStreamer::getNetDst(void) {
    return m_netDst;
}

std::string CStreamer::get_ysfcallsign(void) {
    return m_ysf_callsign;
}		

void CStreamer::putNetDst(std::string newDst) {
    m_netDst = newDst;
}

unsigned int CStreamer::get_srcid(void) {
	unsigned int tmp_srcID;

	if (m_ysf_callsign.empty()) {
		unsigned int tmp = m_conf->getId();
		if (tmp > 99999999U)
			m_srcid = tmp / 100U;
		else if (tmp > 9999999U)
			m_srcid = tmp / 10U;
		else
			m_srcid = tmp;
	} else {
		tmp_srcID = findYSFID(m_ysf_callsign,false);
		if (tmp_srcID != m_srcid)
			m_srcid = findYSFID(m_ysf_callsign,true);

	}
	//LogMessage("Callsign: %s, Id: %d.",m_ysf_callsign.c_str(),m_srcid);
    return m_srcid;
}

unsigned int CStreamer::get_dstid(void) {
    return m_dstid;
}

void CStreamer::put_dstid(unsigned int new_dstid) {
    m_dstid = new_dstid;
}

void CStreamer::put_dmrflco(FLCO newflco) {
	m_dmrflco = newflco;
}

FLCO CStreamer::get_dmrflco(void){
	return m_dmrflco;
}  

void CStreamer::createGPS(std::string callsign)
{ 
	if (m_conf->getAPRSEnabled()) {
		std::string hostname 	= m_conf->getAPRSServer();
		unsigned int port    	= m_conf->getAPRSPort();
		std::string password 	= m_conf->getAPRSPassword();
		std::string tmp_callsign 	= m_conf->getAPRSCallsign();
		std::string desc     	= m_conf->getAPRSDescription();
		std::string icon        = m_conf->getAPRSIcon();
		std::string beacon_text = m_conf->getAPRSBeaconText();
		bool followMode			= m_conf->getAPRSFollowMe();
		int beacon_time			= m_conf->getAPRSBeaconTime();	
			
		if (tmp_callsign.empty())
			tmp_callsign = callsign;
		
		LogMessage("APRS Parameters");
		LogMessage("    Callsign: %s", tmp_callsign.c_str());
		LogMessage("    Node Callsign: %s", m_callsign.c_str());
		LogMessage("    Server: %s", hostname.c_str());
		LogMessage("    Port: %u", port);
		LogMessage("    Passworwd: %s", password.c_str());
		LogMessage("    Description: %s", desc.c_str());
		LogMessage("    Icon: %s", icon.c_str());
		LogMessage("    Beacon Text: %s", beacon_text.c_str());
		LogMessage("    Follow Mode: %s", followMode ? "yes" : "no");	
		LogMessage("    Beacon Time: %d", beacon_time);	
		
		m_writer = new CAPRSWriter(tmp_callsign, password, hostname, port, followMode);

		unsigned int txFrequency = m_conf->getTxFrequency();
		unsigned int rxFrequency = m_conf->getRxFrequency();


		m_writer->setInfo(m_callsign, txFrequency, rxFrequency, desc, icon, beacon_text, beacon_time, followMode);
		bool enabled = m_conf->getMobileGPSEnabled();
		if (enabled) {
			std::string address = m_conf->getMobileGPSAddress();
			unsigned int port   = m_conf->getMobileGPSPort();

			m_writer->setMobileLocation(address, port);
		} else {
			float latitude  = m_conf->getLatitude();
			float longitude = m_conf->getLongitude();
			int height      = m_conf->getHeight();

			m_writer->setStaticLocation(latitude, longitude, height);
		}

		m_gps = new CGPS(m_writer);
		bool ret = m_gps->open();
		if (!ret) {
		delete m_gps;
		LogMessage("Error starting GPS");
		m_gps = NULL;
		}   
	}

	if (m_conf->getAPRSAPIKey().empty()) {
		::memcpy(m_gps_buffer, dt1_temp, 10U);
		::memcpy(m_gps_buffer + 10U, dt2_temp, 10U);
		LogMessage("Geeting position from aprs.fi disabled. %s",m_conf->getAPRSAPIKey());		
	}
	else {
		m_APRS = new CAPRSReader(m_conf->getAPRSAPIKey(), m_conf->getAPRSRefresh());
		LogMessage("Geeting position information from aprs.fi with ApiKey.");
	}  

}

void CStreamer::BeaconLogic(void) {
	unsigned char buffer[50U];
	unsigned int n;	
	static bool first_time_b = true;
	
		// If Beacon time start voice beacon transmit
		if (first_time_b || (m_not_busy && m_inacBeaconTimer->hasExpired() && (m_beacon_Watch.elapsed()> (m_beacon_time*TIME_MIN)))) {
			//m_not_busy=false;
			m_beacon_status = BE_INIT;
			m_bea_voice_Watch.start();
			m_beacon_Watch.start();
			first_time_b = false;
			m_gid = 0;
		}
		
		// Beacon Logic
		if ((m_beacon_status != BE_OFF) && (m_bea_voice_Watch.elapsed() > BEACON_PER)) {

			switch (m_beacon_status) {
				case BE_INIT:
						m_rcv_callsign = "BEACON";
						m_rcv_callsign.resize(YSF_CALLSIGN_LENGTH, ' ');
						m_gid = 0;
						if (m_APRS != NULL)
							m_APRS->get_gps_buffer(m_gps_buffer,(int)(m_conf->getLatitude() * 1000),(int)(m_conf->getLongitude() * 1000));
						else {
							::memcpy(m_gps_buffer, dt1_temp, 10U);
							::memcpy(m_gps_buffer + 10U, dt2_temp, 10U);							
						}
						m_file_out=fopen(m_beacon_name.c_str(),"rb");
						if (!m_file_out) {
							LogMessage("Error opening file: %s.",m_beacon_name.c_str());
							m_beacon_status = BE_OFF;
						}
						else {
							LogMessage("Beacon Init: %s.",m_beacon_name.c_str());
							m_conv.putDMRHeader();							
							m_beacon_status = BE_DATA;
						}
						m_bea_voice_Watch.start();
						break;

				case BE_DATA:
						if (m_beacon_Watch.elapsed()>YSF_FRAME_PER) {
							n=fread(buffer,1U,40U,m_file_out);
							if (n>39U) {
								m_conv.AMB2YSF_Mode2(buffer);
								m_conv.AMB2YSF_Mode2(buffer+8U);
								m_conv.AMB2YSF_Mode2(buffer+16U);
								m_conv.AMB2YSF_Mode2(buffer+24U);
								m_conv.AMB2YSF_Mode2(buffer+32U);
							} else {
								m_beacon_status = BE_EOT;
								m_conv.putDMREOT(true);
								if (m_file_out) fclose(m_file_out);
								LogMessage("Beacon Out: %s.",m_beacon_name.c_str());
							}
							m_beacon_Watch.start();
							m_bea_voice_Watch.start();
						}
						break;

				case BE_EOT:
						//if (m_file_out) fclose(m_file_out);
						//LogMessage("Beacon Out: %s.",beacon_name);
						//m_conv.putDMREOT(true);
						//m_beacon_Watch.start();					
						break;

				case BE_OFF:
				        break;
				default:
					break;
			}

		}		
}		

bool CStreamer::not_busy(void) {
    return m_not_busy;
}

WX_STATUS CStreamer::change_TG(void){
	WX_STATUS tmp = m_wx_returned;

	m_wx_returned = WXS_NONE;
    return tmp;
}		

bool CStreamer::unkinkReceived(void) {
    return m_unlinkReceived;
}

unsigned char buffer[2000U];

void CStreamer::Init(CYSFNetwork *modemNetwork, CYSFNetwork* ysfNetwork, CFCSNetwork* fcsNetwork, CDMRNetwork *dmrNetwork,CReflectors *dmrReflectors) {
    m_ysfNetwork = ysfNetwork;
    m_fcsNetwork = fcsNetwork;    
    m_dmrNetwork = dmrNetwork;
    m_modemNetwork = modemNetwork;
	m_dmrReflectors = dmrReflectors;

	m_not_busy=true;	
	m_open_channel=false;
	m_data_ready=true;
	m_unlinkReceived = false;
	m_wx_returned = WXS_NONE;
	memset(buffer, 0U, 2000U);
    if (m_ysfNetworkEnabled) m_ysfWatch.start();	
	if (m_dmrNetworkEnabled) m_dmrWatch.start(); 
	//init radioid
	memcpy(ysf_radioid,std_ysf_radioid,5U);   
	m_inacBeaconTimer = new CTimer(1000U,30U);
}

CWiresX* CStreamer::createWiresX(CYSFNetwork* rptNetwork, bool makeUpper, std::string callsign, std::string location)
{
	assert(rptNetwork != NULL);

	m_storage = new CWiresXStorage(m_conf->getNewsPath());
	m_wiresX = new CWiresX(m_storage, callsign, location, rptNetwork, makeUpper, &m_conv);
	m_dtmf = new CDTMF();
	
	std::string name = m_conf->getName();

	unsigned int txFrequency = m_conf->getTxFrequency();
	unsigned int rxFrequency = m_conf->getRxFrequency();
	
	m_wiresX->setInfo(name, txFrequency, rxFrequency);
	m_wiresX->start();
	m_callsign = callsign;

    return m_wiresX;
}

void CStreamer::clock(TG_STATUS m_TG_connect_state, unsigned int ms) {
	CDMRData tx_dmrdata;
	unsigned char token;

		// Beacon processing
		if (m_TG_connect_state != TG_DISABLE) {
			if (m_beacon) BeaconLogic();		

			// Get DMR network data
			if (m_dmrNetworkEnabled && ((m_TG_connect_state==TG_NONE) || (m_TG_connect_state==WAITING_UNLINK)) )	
				while (m_dmrNetwork->read(tx_dmrdata) > 0U)
					if (m_tg_type == DMR) DMR_get_Network(tx_dmrdata,ms);

			// YSF Network receive and process		
			while ((m_ysfNetwork != NULL) && (m_ysfNetwork->read(buffer) > 0U)) {
				if (m_tg_type == YSF) GetFromNetwork(buffer,m_modemNetwork);
			}

			while ((m_fcsNetwork != NULL) && (m_fcsNetwork->read(buffer) > 0U)) {	
				if (m_tg_type == FCS) GetFromNetwork(buffer,m_modemNetwork);
			}

			// Send to Network delayed data packets from modem
			if ((m_tg_type==FCS) || (m_tg_type==YSF))
				while (m_rpt_buffer.hasData()  && m_data_ready) {
					m_rpt_buffer.getData(&token,1U);
					//token to stop flow of data
					if (token==0xFF) {
						m_data_ready = false;
						m_rpt_buffer.getData(buffer,155U);
						token=0;
						m_rpt_buffer.addData(&token,1U);
						m_rpt_buffer.addData(buffer,155U);
					} else {
						m_rpt_buffer.getData(buffer,155U);
						if ((m_ysfNetwork != NULL && (m_tg_type==YSF)) &&  ///!m_exclude) && 
							(::memcmp(buffer + 0U, "YSFD", 4U) == 0)) {
								m_ysfNetwork->write(buffer);
							}
						if ((m_fcsNetwork != NULL && (m_tg_type==FCS)) &&  ///!m_exclude) &&
							(::memcmp(buffer + 0U, "YSFD", 4U) == 0)) {
								m_fcsNetwork->write(buffer);
							}
					}			
				}		

			// Put DMR Network Data
			if ((m_dmrNetworkEnabled) && (m_tg_type==DMR)) DMR_send_Network();

			YSFPlayback(m_modemNetwork);    
		} else {
			// Get data
			while ((m_ysfNetwork != NULL) && (m_ysfNetwork->read(buffer) > 0U));
			while ((m_fcsNetwork != NULL) && (m_fcsNetwork->read(buffer) > 0U));
			// Get DMR network data
			if (m_dmrNetworkEnabled)	
				while (m_dmrNetwork->read(tx_dmrdata) > 0U);		
		}

		// Get from modem and proccess data info		
		GetFromModem(m_modemNetwork, m_TG_connect_state);

    	if (m_writer != NULL)
			m_writer->clock(ms);
		
		m_wiresX->clock(ms);   

		if (m_inacBeaconTimer!=NULL) 
			m_inacBeaconTimer->clock(ms); 
}

void CStreamer::AMBE_write(unsigned char* buffer, unsigned char fi, unsigned char dt, unsigned char fn, unsigned char ft, unsigned char bn, unsigned char bt) {
	static char tmp[40U];
	static int count_file_AMBE=0;
	static FILE *file;
	static int ysfFrames;
				
	if ((::memcmp(buffer, "YSFD", 4U) == 0U) && (dt == YSF_DT_VD_MODE2)) {
		CYSFPayload ysfPayload;

		if (fi == YSF_FI_HEADER) {
			if (ysfPayload.processHeaderData(buffer + 35U)) {
				sprintf(tmp, "/tmp/file%03d.amb",count_file_AMBE);
				count_file_AMBE++;
				file = fopen(tmp,"wb");
				if (!file) LogMessage("Error creating AMBE file: %s",tmp);
				else LogMessage("Recording AMBE file: %s",tmp);

				std::string ysfSrc = ysfPayload.getSource();
				std::string ysfDst = ysfPayload.getDest();
				LogMessage("Writing AMBE from YSF Header: Src: %s Dst: %s", ysfSrc.c_str(), ysfDst.c_str());
				m_conv.putYSFHeader();
				ysfFrames = 0U;				
			}
		} else if (fi == YSF_FI_TERMINATOR) {
			fclose(file);
			LogMessage("AMBE Closing %s file, %.1f seconds", tmp, float(ysfFrames) / 10.0F);
/*			int extraFrames = (m_hangTime / 100U) - m_ysfFrames - 2U;
			for (int i = 0U; i < extraFrames; i++)
				m_conv.putDummyYSF(); */	 			
			m_conv.putYSFEOT();
			ysfFrames = 0U;
		} else if (fi == YSF_FI_COMMUNICATIONS) {
			m_conv.putYSF_Mode2(buffer + 35U,file);
			ysfFrames++;
		}
	}	
}

unsigned char recv_buffer[2000U];

void CStreamer::GetFromModem(CYSFNetwork* rptNetwork, TG_STATUS m_TG_connect_state) {
// unsigned char d_buffer[200];
unsigned int len;
unsigned char token;
// unsigned char dch[20U];
// unsigned char csd1[20U],csd2[20U];
WX_STATUS ret;
	
	while ((len=rptNetwork->read(recv_buffer)) > 0U) {
				
		if (::memcmp(recv_buffer, "YSFD", 4U) != 0U) continue;
		CYSFFICH fich;
		bool valid = fich.decode(recv_buffer + 35U);
		if (valid) {
			unsigned char fi = fich.getFI();
			unsigned char dt = fich.getDT();
			unsigned char fn = fich.getFN();
			unsigned char ft = fich.getFT();
			unsigned char bn = fich.getBN();
			unsigned char bt = fich.getBT();

			if (fi == YSF_FI_HEADER) m_not_busy=false;
			else if (fi == YSF_FI_TERMINATOR) {
				if (m_inactivityTimer!=NULL) m_inactivityTimer->start();
				m_inacBeaconTimer->start();
			}

			if (m_TG_connect_state != TG_DISABLE) {

				if (dt == YSF_DT_VD_MODE1) {
					//LogMessage("Packet len= %d, fi=%d,dt=%d,fn=%d,ft=%d,bn=%d,bt=%d.",len,fi,dt,fn,ft,bn,bt);	
					//CUtils::dump(1U,"Bloque DV1",recv_buffer,155U);
					if (fi == YSF_FI_HEADER) 
						m_ysf_callsign = getSrcYSF_fromHeader(recv_buffer);

					//CYSFPayload payload;
					//payload.readVDMode1Data(recv_buffer + 35U,dch);
					//CUtils::dump(1U,"Bloque DCH",dch,20U);
					ret=m_wiresX->processVDMODE1(m_ysf_callsign,recv_buffer, fi, dt, fn, ft, bn, bt);
					if (ret == WXS_FAIL) {
						LogMessage("Audio Mode 1 going to the network.");
						// Not for me	
						// Flow non stop
						token = 0x00;
						m_data_ready = true;
						m_rpt_buffer.addData(&token,1U);
						m_rpt_buffer.addData(recv_buffer,len);
					}
					return;
				}

				// if (dt == YSF_DT_VD_MODE2) {
				// 	LogMessage("Packet len= %d, fi=%d,dt=%d,fn=%d,ft=%d,bn=%d,bt=%d.",len,fi,dt,fn,ft,bn,bt);	
				// 	//CUtils::dump(1U,"Bloque DV1",recv_buffer,155U);
				// 	if (fi == YSF_FI_HEADER || fi == YSF_FI_TERMINATOR) {
				// 	CYSFPayload payload;
				// 	payload.readDataFRModeData1(recv_buffer + 35U,csd1);
				// 	payload.readDataFRModeData2(recv_buffer + 35U,csd2);
				// 	CUtils::dump(1U,"CSD1",csd1,20U);
				// 	CUtils::dump(1U,"CSD2",csd2,20U);				
				// 	} else {
				// 	// 	m_ysf_callsign = getSrcYSF_fromHeader(recv_buffer);

				// 	CYSFPayload payload;
				// 	payload.readVDMode2Data(recv_buffer + 35U,dch);
				// 	CUtils::dump(1U,"Bloque DCH",dch,20U);
				// 	//ret = m_wiresX->processVDMODE1(m_ysf_callsign,recv_buffer, fi, dt, fn, ft, bn, bt);
				// 	//if (ret == WXS_PLAY) m_open_channel = true;
				// 	}
				// 	return;
				// }			
				
				if (m_NoChange) {
					fich.setDGId(m_DGID);
					fich.encode(recv_buffer + 35U);
				}

				if (m_saveAMBE) 
					AMBE_write(recv_buffer, fi, dt, fn, ft, bn, bt);

				//LogMessage("Packet len= %d, fi=%d,dt=%d,fn=%d,ft=%d,bn=%d,bt=%d.",len,fi,dt,fn,ft,bn,bt);	
				if (dt == YSF_DT_VOICE_FR_MODE) {
					// flow not stop
					token = 0x00;
					m_data_ready = true;
					m_rpt_buffer.addData(&token,1U);
					m_rpt_buffer.addData(recv_buffer,len);
				} else if (dt == YSF_DT_VD_MODE2) {
					m_data_ready = true;
					if (m_tg_type!=DMR) {
						// flow not stop
						token = 0x00;
						m_rpt_buffer.addData(&token,1U);
						m_rpt_buffer.addData(recv_buffer,len);
					}
					else {
						if (fi == YSF_FI_HEADER) {
							m_ysf_callsign = getSrcYSF_fromHeader(recv_buffer);
							m_dmrNetwork->reset(2U);	// OE1KBC fix
							m_srcid = findYSFID(m_ysf_callsign, true);						
							m_conv.putYSFHeader();
							m_ysfFrames = 0U;
						} else if (fi == YSF_FI_TERMINATOR) {
							//LogMessage("Received YSF Communication from modem, %.1f seconds", float(m_ysfFrames) / 10.0F);				
							m_conv.putYSFEOT();
							m_ysfFrames = 0U;					
						} else if (fi == YSF_FI_COMMUNICATIONS) {
							m_conv.putYSF(recv_buffer + 35U);
							m_ysfFrames++;							
						}
					}			
				} 
			}
			
			if (dt == YSF_DT_DATA_FR_MODE) {
					if (m_tg_type!=DMR) {

						if (fi==YSF_FI_HEADER) {
							// stop flow of data until we know if go to network or no
							token = 0xFF;
						} else token = 0x00;
						m_rpt_buffer.addData(&token,1U);
						m_rpt_buffer.addData(recv_buffer,len);
					}		
					//processDTMF(buffer, dt);
					processWiresX(recv_buffer, fi, dt, fn, ft, bn, bt);
					if (fi == YSF_FI_TERMINATOR) {
						if (m_wiresX->sendNetwork()) {
							LogMessage("Data allowed to go");
						} else {
							LogMessage("Data not allowed to go to Network");
							m_rpt_buffer.clear();
						}
						m_data_ready = true;
					}
			}
		if (m_gps != NULL)
			m_gps->data(recv_buffer + 14U, recv_buffer + 35U, fi, dt, fn, ft, m_tg_type, m_dstid);			

		if ((recv_buffer[34U] & 0x01U) == 0x01U) {
			if (m_gps != NULL)
				m_gps->reset();
			m_dtmf->reset();
			}
		} 
	}
}


void CStreamer::put_tgType(enum TG_TYPE tg_type) {
	m_tg_type = tg_type;
}

char *CStreamer::get_radio(char c) {
	static char radio[10U];

	switch (c) {
	case 0x10U:
		::strcpy(radio, "BlueDV");
		break;
	case 0x11U:
		::strcpy(radio, "Peanut");
		break;
	case 0x15U:
		::strcpy(radio, "YSF2DMR");
		break;
	case 0x16U:
		::strcpy(radio, "IPCS2");
		break;		
	case 0x20U:
		::strcpy(radio, "DX-R2");
		break;							
	case 0x24U:
		::strcpy(radio, "FT-1D");
		break;
	case 0x25U:
		::strcpy(radio, "FTM-400D");
		break;
	case 0x26U:
		::strcpy(radio, "DR-1X");
		break;
	case 0x27U:
		::strcpy(radio, "FT-991");
		break;		
	case 0x28U:
		::strcpy(radio, "FT-2D");
		break;
	case 0x29U:
		::strcpy(radio, "FTM-100D");
		break;
	case 0x2AU:
		::strcpy(radio, "FTM-3200");
		break;		
	case 0x2BU: 
		::strcpy(radio, "FT-70");
		break;
	case 0x2DU: 
		::strcpy(radio, "FTM-3207");
		break;
	case 0x2EU:  
		::strcpy(radio, "FTM-7250");
		break;			
	case 0x30U:
		::strcpy(radio, "FT-3D");
		break;
	case 0x31U:
		::strcpy(radio, "FTM-300D");
		break;			
	default:
		::sprintf(radio, "0x%02X", c);
		break;
	}
	return radio;	
}

bool CStreamer::containsOnlyASCII(const std::string& filePath) {
  for (auto c: filePath) {
    if (static_cast<unsigned char>(c) > 127) {
      return false;
    }
  }
  return true;
}

int silence_number;
char alien_user[YSF_CALLSIGN_LENGTH+1];

void CStreamer::GetFromNetwork(unsigned char *buffer, CYSFNetwork* rtpNetwork) {
static unsigned char tmp[20];
static bool first_time,beacon_running;
std::string tmp_str;
//static unsigned int last_num,act_num;
CYSFPayload ysfPayload;
	
	// Only pass through YSF data packets
	if ((::memcmp(buffer + 0U, "YSFD", 4U) == 0) && !m_wiresX->isBusy()) {
		if (m_beacon_status==BE_DATA) {
			if (m_file_out) fclose(m_file_out);
			LogMessage("Beacon Break.");
			m_conv.putDMREOT(true);
			m_beacon_Watch.start();
			m_beacon_status = BE_OFF;
			m_open_channel=true;
			beacon_running = true;
		} else beacon_running = false;
	
		CYSFFICH fich;
		bool valid = fich.decode(buffer + 35U);

		if (valid) {
			unsigned char fi = fich.getFI();
			unsigned char dt = fich.getDT();
			unsigned char fn = fich.getFN();
			unsigned char ft = fich.getFT();  // ft=6 no gps  ft=7 gps

			//  if (fi==YSF_FI_HEADER) {
			//  	last_num=buffer[34U];
			//  } else 
			//  if (fi==YSF_FI_COMMUNICATIONS) {
			// 	if (::memcmp(buffer +14U, m_rcv_callsign.c_str(), YSF_CALLSIGN_LENGTH) != 0) {
			// 		strncpy(alien_user,buffer+14,YSF_CALLSIGN_LENGTH);
			// 		LogMessage("packet from alien user %s.Rejecting",m_rcv_callsign.c_str());
			// 	}
			//  }
			// 	act_num=buffer[34U];
			// 	if ((act_num != ((last_num+2) & 0xFFU)) && (act_num != ((last_num+4) & 0xFFU)) && (act_num != ((last_num+6) & 0xFFU))
			// 	&& (act_num != ((last_num+8) & 0xFFU)) && (act_num != ((last_num+10) & 0xFFU))) {
			// 		LogMessage("packet out of order actual=%d, last=%d, rejecting..",act_num,last_num);
			// 		//LogMessage("Packet out of order rejected");
			// 		last_num=act_num;
			// 		return;
			// 	} 
			// 	// 	LogMessage("packet out of order actual=%d, last=%d",act_num,last_num);
			// 	last_num=act_num;
			// }		

			//LogMessage("RX Packet gid=%d, fi=%d,dt=%d,fn=%d,ft=%d.",m_gid,fi,dt,fn,ft);										
			if ((dt==YSF_DT_DATA_FR_MODE) || (dt==YSF_DT_VOICE_FR_MODE)) {
				// Data packets go direct to modem
				rtpNetwork->write(buffer);	
			} else if (dt==YSF_DT_VD_MODE2) {

				if (fi==YSF_FI_HEADER) {
					CYSFPayload payload;
					
					if (m_open_channel && (!beacon_running)) {
						tmp_str = getSrcYSF_fromHeader(buffer);
						if (strcmp(tmp_str.c_str(),m_rcv_callsign.c_str())!=0) {
							strcpy(alien_user,tmp_str.c_str());					
							LogMessage("Received duplicate start from alien user %s.",alien_user);
							return;
						} else {
							// double starting
							strcpy(alien_user,"");
							return;
						}
					} else strcpy(alien_user,"");

					
					unsigned char dch[20U];
					if (payload.readVDMode1Data(buffer+35U,dch)) memcpy(ysf_radioid,dch+5U,5U);
					else memcpy(ysf_radioid,std_ysf_radioid,5U);
					memcpy(tmp,ysf_radioid,5U);
					tmp[5U]=0;
					LogMessage("Radio ID: %s",tmp);
					m_not_busy=false;
					m_rcv_callsign = getSrcYSF_fromHeader(buffer);
					m_gid = fich.getDGId();					
					LogMessage("Received voice data *%s* from *%s*, gid=%d.",m_rcv_callsign.c_str(),m_netDst.c_str(),m_gid);
					if (m_APRS != NULL) m_APRS->get_gps_buffer(m_gps_buffer,m_rcv_callsign);
					else {
						::memcpy(m_gps_buffer, dt1_temp, 10U);
						::memcpy(m_gps_buffer + 10U, dt2_temp, 10U);
					}
					m_gid = fich.getDGId();
					m_conv.putDMRHeader();
					first_time = true;
					m_open_channel=true;					
				} else if (fi==YSF_FI_COMMUNICATIONS) {
					// Test if late entry
					if (m_open_channel==false) {
						if (m_tg_type == FCS) { if (fn==1) m_rcv_callsign=getSrcYSF_fromFN1(buffer);}
						else m_rcv_callsign = getSrcYSF_fromData(buffer);
						if (m_APRS != NULL) m_APRS->get_gps_buffer(m_gps_buffer,m_rcv_callsign);
						else {
							::memcpy(m_gps_buffer, dt1_temp, 10U);
							::memcpy(m_gps_buffer + 10U, dt2_temp, 10U);
						}
						m_gid = fich.getDGId();
						LogMessage("Late Entry from %s, gid=%d.",m_rcv_callsign.c_str(),m_gid);
						memcpy(ysf_radioid,std_ysf_radioid,5U);
						strcpy(alien_user,"");
						m_not_busy=false;
						m_conv.putDMRHeader();
						first_time = true;
						m_open_channel=true;						
					}

					if (m_tg_type == YSF) {
						tmp_str = getSrcYSF_fromData(buffer);
						if (strcmp(alien_user,tmp_str.c_str()) == 0) {
							LogMessage("Voice Packet from alien user %s. Rejecting..",alien_user);
							return;
						}
					}
					
					if (m_tg_type == FCS) { if (fn==1) m_rcv_callsign=getSrcYSF_fromFN1(buffer);}
					else m_rcv_callsign = getSrcYSF_fromData(buffer); 			
					// Update gps info for ft=6
					silence_number = 0;
					if ((ft==6) && (fn==6)) {
						//show info once
						if (first_time) {
							ysfPayload.readVDMode2Data(buffer + 35U, tmp);
							CUtils::dump("GPS Info not provided",tmp,10U);
							LogMessage("Radio: %s.",get_radio(*(tmp+4)));						
							first_time = false;
						}
						//update gps
						if (m_APRS != NULL) m_APRS->get_gps_buffer(m_gps_buffer,m_rcv_callsign);
						else {
							::memcpy(m_gps_buffer, dt1_temp, 10U);
							::memcpy(m_gps_buffer + 10U, dt2_temp, 10U);
						}						
					}
					// Update gps info for ft=7
					if ((ft==7) && ((fn==6) || (fn==7))) {
						if (fn==6) {
							ysfPayload.readVDMode2Data(buffer + 35U, tmp);
							if ((*(tmp + 5U) == 0x00) && (*(tmp + 2U) == 0x62)) {
								if (first_time) {
									LogMessage("GPS Info Empty. DMR Transcoding?");
									LogMessage("Radio: %s.",get_radio(*(tmp+4)));
									first_time = false;	
								}								
								if (m_APRS != NULL) m_APRS->get_gps_buffer(m_gps_buffer,m_rcv_callsign);
								else {
									::memcpy(m_gps_buffer, dt1_temp, 10U);
									::memcpy(m_gps_buffer + 10U, dt2_temp, 10U);									
								}							
							} else {
									if (first_time) {
									LogMessage("Radio: %s.",get_radio(*(tmp+4)));
									//first_time = false;	
								}								

							}
													
						} else {
							if ((*(tmp + 5U) != 0x00) || (*(tmp + 2U) != 0x62)) {
								memcpy(m_gps_buffer,tmp,10U);
								ysfPayload.readVDMode2Data(buffer + 35U, m_gps_buffer + 10U);
								if (first_time) {
									CUtils::dump("GPS Real info found",m_gps_buffer,20U);
									//LogMessage("Radio: %s.",get_radio(*(tmp+4)));
									first_time = false;
								}
							} //else CUtils::dump("Fake info found",m_gps_buffer,20U);
						}
					}
					m_conv.putVCH(buffer + 35U);
					// m_open_channel=true;

				} else if (fi==YSF_FI_TERMINATOR) {
					tmp_str = getSrcYSF_fromHeader(buffer);
					if (strcmp(alien_user,tmp_str.c_str()) == 0) {
						LogMessage("EOT Packet from alien user %s. Rejecting..",alien_user);
						return;
					}
					LogMessage("YSF EOT received");
					m_conv.putDMREOT(true);  // changed from false
					m_open_channel = true;
				}
			}
		} 
		// else {
		// 	CUtils::dump("Packet received from network not valid",buffer,155U);
		// }
	}
	m_lostTimer->start();
}	

void CStreamer::YSFPlayback(CYSFNetwork *rptNetwork) {
	static bool start_silence = false;
	unsigned int fn;
	unsigned int ysfFrameType;
	std::string tmp_callsign = m_callsign;
	tmp_callsign.resize(YSF_CALLSIGN_LENGTH, ' ');

	// YSF Playback 
	if ((m_ysfWatch.elapsed() > YSF_FRAME_PER) && (m_open_channel || (m_beacon_status!=BE_OFF))) {
		// Playback YSF
		::memset(m_ysfFrame,0U,200U);
		ysfFrameType = m_conv.getYSF(m_ysfFrame + 35U);
		
		if((ysfFrameType == TAG_HEADER) || (ysfFrameType == TAG_HEADERV1)) {
			start_silence = true;
			m_ysf_cnt = 0U;
			silence_number=0;			

			::memcpy(m_ysfFrame + 0U, "YSFD", 4U);
			::memcpy(m_ysfFrame + 4U, m_netDst.c_str(), YSF_CALLSIGN_LENGTH);
			::memcpy(m_ysfFrame + 14U, m_rcv_callsign.c_str(), YSF_CALLSIGN_LENGTH);
			::memcpy(m_ysfFrame + 24U, "ALL       ", YSF_CALLSIGN_LENGTH);
			m_ysfFrame[34U] = 0U; // Net frame counter

			CSync::addYSFSync(m_ysfFrame + 35U);

			// Set the FICH
			CYSFFICH fich;			
			fich.setFI(YSF_FI_HEADER);
			fich.setCS(2U);
			if (ysf_radioid[0] != '*') fich.setCM(1U);
			else fich.setCM(0U);
			fich.setBN(0U);
			fich.setBT(0U);		
			fich.setFN(0U);
			fich.setFT(7U);
			// fich.setDev(0U);
			// fich.setMR(0U);
			// fich.setVoIP(false);			
			if (ysfFrameType == TAG_HEADER) fich.setDT(YSF_DT_VD_MODE1);	
			else fich.setDT(YSF_DT_VD_MODE2);	
			fich.setDGId(m_gid);
			//fich.setMR(2U);
			fich.encode(m_ysfFrame + 35U);

			unsigned char dch[20U];
			unsigned char csd1[20U], csd2[20U];
			CYSFPayload payload;
			if (ysfFrameType == TAG_HEADER) {
				memset(csd1, '*', YSF_CALLSIGN_LENGTH/2);
				memcpy(csd1 + YSF_CALLSIGN_LENGTH/2, ysf_radioid, YSF_CALLSIGN_LENGTH/2);			
				memcpy(csd1 + YSF_CALLSIGN_LENGTH, m_rcv_callsign.c_str(), YSF_CALLSIGN_LENGTH);
				memset(csd2 , ' ', YSF_CALLSIGN_LENGTH + YSF_CALLSIGN_LENGTH);
				payload.writeHeader(m_ysfFrame + 35U, csd1, csd2);
			} else {
				memset(dch,'*',YSF_CALLSIGN_LENGTH);
				memcpy(dch+YSF_CALLSIGN_LENGTH,tmp_callsign.c_str(),YSF_CALLSIGN_LENGTH);
				payload.writeVDMode1Data(m_ysfFrame + 35U, dch);
			}

		//	LogMessage("Header Playback");
			rptNetwork->write(m_ysfFrame);

			m_ysf_cnt++;
			m_ysfWatch.start();
		} else if (ysfFrameType == TAG_EOT || ysfFrameType == TAG_EOTV1) {
			silence_number=0;

			::memcpy(m_ysfFrame + 0U, "YSFD", 4U);
			::memcpy(m_ysfFrame + 4U, m_netDst.c_str(), YSF_CALLSIGN_LENGTH);
			::memcpy(m_ysfFrame + 14U, m_rcv_callsign.c_str(), YSF_CALLSIGN_LENGTH);
			::memcpy(m_ysfFrame + 24U, "ALL       ", YSF_CALLSIGN_LENGTH);
			m_ysfFrame[34U] = ((m_ysf_cnt & 0x7FU) <<1) | 0x01U; // Net frame counter

			CSync::addYSFSync(m_ysfFrame + 35U);

			// Set the FICH
			CYSFFICH fich;
			fich.setFI(YSF_FI_TERMINATOR);
			fich.setCS(2U);
			if (ysf_radioid[0] != '*') fich.setCM(1U);
			else fich.setCM(0U);
			fich.setBN(0U);
			fich.setBT(0U);						
			fich.setFN(0U);
			fich.setFT(7U);
			// fich.setDev(0U);
			// fich.setMR(0U);
			// fich.setVoIP(false);			
			if (ysfFrameType == TAG_HEADER) fich.setDT(YSF_DT_VD_MODE1);	
			else fich.setDT(YSF_DT_VD_MODE2);				
			fich.setDGId(m_gid);
			fich.encode(m_ysfFrame + 35U);

			unsigned char dch[20U];
			unsigned char csd1[20U], csd2[20U];
			CYSFPayload payload;			
			if (ysfFrameType == TAG_HEADER) {
				memset(csd1, '*', YSF_CALLSIGN_LENGTH);
				memcpy(csd1 + YSF_CALLSIGN_LENGTH/2, ysf_radioid, YSF_CALLSIGN_LENGTH/2);			
				memcpy(csd1 + YSF_CALLSIGN_LENGTH, m_rcv_callsign.c_str(), YSF_CALLSIGN_LENGTH);
				memset(csd2 , ' ', YSF_CALLSIGN_LENGTH + YSF_CALLSIGN_LENGTH);
				payload.writeHeader(m_ysfFrame + 35U, csd1, csd2);
			} else {
				memset(dch,'*',YSF_CALLSIGN_LENGTH);
				memcpy(dch+YSF_CALLSIGN_LENGTH,tmp_callsign.c_str(),YSF_CALLSIGN_LENGTH);
				payload.writeVDMode1Data(m_ysfFrame + 35U, dch);
			}

		//	LogMessage("EOT Playback");
			rptNetwork->write(m_ysfFrame);

			m_open_channel=false;
			start_silence = false;
			strcpy(alien_user,"");
			m_rcv_callsign = std::string("");			
			if (m_beacon_status != BE_OFF) m_beacon_status = BE_OFF;
			m_not_busy = true;
			m_inacBeaconTimer->start();
			memcpy(ysf_radioid,std_ysf_radioid,5U);
			m_conv.reset();	

		} else if ((ysfFrameType == TAG_DATA) || (ysfFrameType == TAG_DATAV1)) {
			fn = (m_ysf_cnt - 1U) % 8U;
			
			::memcpy(m_ysfFrame + 0U, "YSFD", 4U);
			::memcpy(m_ysfFrame + 4U, m_netDst.c_str(), YSF_CALLSIGN_LENGTH);
			::memcpy(m_ysfFrame + 14U, m_rcv_callsign.c_str(), YSF_CALLSIGN_LENGTH);
			::memcpy(m_ysfFrame + 24U, "ALL       ", YSF_CALLSIGN_LENGTH);

			// Add the YSF Sync
			CSync::addYSFSync(m_ysfFrame + 35U);
			CYSFPayload payload;
			if (ysfFrameType == TAG_DATA) {
					switch (fn) {
						case 0:
							// ***key
							unsigned char dch[20U];
							memset(dch, '*', YSF_CALLSIGN_LENGTH);
							memcpy(dch + YSF_CALLSIGN_LENGTH/2, ysf_radioid, YSF_CALLSIGN_LENGTH/2);				
							payload.writeVDMode2Data(m_ysfFrame + 35U, dch);
							break;
						case 1:
							//Callsign
							payload.writeVDMode2Data(m_ysfFrame + 35U, (const unsigned char*)m_rcv_callsign.c_str());
							break;
						case 5:					
							if (ysf_radioid[0] != '*') {
								memset(dch, ' ', YSF_CALLSIGN_LENGTH/2);
								memcpy(dch + YSF_CALLSIGN_LENGTH/2, ysf_radioid, YSF_CALLSIGN_LENGTH/2);
								payload.writeVDMode2Data(m_ysfFrame + 35U, dch);							
							} else payload.writeVDMode2Data(m_ysfFrame + 35U, (const unsigned char*)"          ");
							break;									
						case 6:
							if ((m_tg_type == DMR) && (m_beacon_status == BE_OFF)) {
								if (m_APRS != NULL) m_APRS->get_gps_buffer(m_gps_buffer,m_rcv_callsign);
								else {
									::memcpy(m_gps_buffer, dt1_temp, 10U);
									::memcpy(m_gps_buffer + 10U, dt2_temp, 10U);
								}
							}
							payload.writeVDMode2Data(m_ysfFrame + 35U, (const unsigned char*) m_gps_buffer); 
							break;
						case 7:
							payload.writeVDMode2Data(m_ysfFrame + 35U, (const unsigned char*) m_gps_buffer + 10U);
							break;
						default:
							payload.writeVDMode2Data(m_ysfFrame + 35U, (const unsigned char*)"          ");
					}
			} else {
					unsigned char dch[20U];
					m_wiresX->getMode1DCH(dch,fn);
					payload.writeVDMode1Data(m_ysfFrame + 35U, dch);
			}

			// Set the FICH
			CYSFFICH fich;			
			fich.setFI(YSF_FI_COMMUNICATIONS);
			fich.setCS(2U);
			if (ysf_radioid[0] != '*') fich.setCM(1U);
			else fich.setCM(0U);
			fich.setBN(0U);
			fich.setBT(0U);								
			fich.setFN(fn);
			fich.setFT(7U);
			// fich.setDev(0U);
			// fich.setMR(0U);
			// fich.setVoIP(false);			
			if (ysfFrameType == TAG_HEADER) fich.setDT(YSF_DT_VD_MODE1);	
			else fich.setDT(YSF_DT_VD_MODE2);		
			//fich.setMR(YSF_MR_BUSY);
			fich.setDGId(m_gid);

			//CUtils::dump("Data b",m_ysfFrame,155U);			
			fich.encode(m_ysfFrame + 35U);

			// Net frame counter
			m_ysfFrame[34U] = (m_ysf_cnt & 0x7FU) << 1;
			//LogMessage("Data Playback %d",m_ysf_cnt);
			// Send data to MMDVMHost
			rptNetwork->write(m_ysfFrame);

			m_ysf_cnt++;
			m_ysfWatch.start();
		} else {
			if ((m_ysfWatch.elapsed() > 130U) && start_silence) {  // 180U
					LogMessage("pipe_stalls, Inserting silence.");
					m_conv.putDMRSilence();
					silence_number++;
					if (silence_number>5) {
						LogMessage("Too many silence, Signal lost.");
						m_conv.putDMREOT(true);
					}
				}
		}
	}	
	
}

void CStreamer::DMR_get_Network(CDMRData tx_dmrdata, unsigned int ms) {	

	if ((m_tg_type==DMR) && !m_wiresX->isBusy()) {
		unsigned int SrcId = tx_dmrdata.getSrcId();
		unsigned int DstId = tx_dmrdata.getDstId();
		
		if (m_beacon_status==BE_DATA) {
			if (m_file_out) fclose(m_file_out);
			LogMessage("Beacon Break.");
			m_conv.putDMREOT(true);
			m_beacon_Watch.start();
			m_beacon_status = BE_OFF;
			m_open_channel=true;
		}
		FLCO netflco = tx_dmrdata.getFLCO();
		unsigned char DataType = tx_dmrdata.getDataType();

		if (!tx_dmrdata.isMissing()) {
			m_networkWatchdog.start();

			if(DataType == DT_TERMINATOR_WITH_LC) {
				if (m_dmrFrames == 0U) {
					m_dmrNetwork->reset(2U);
					m_networkWatchdog.stop();
					m_dmrinfo = false;
					m_firstSync = false;
					return;
				}
				
				if (!m_unlinkReceived && ((SrcId == 4000U) || (SrcId==m_dstid)))
					m_unlinkReceived = true;

				LogMessage("DMR received end of voice transmission, %.1f seconds", float(m_dmrFrames) / 16.667F);
				m_conv.putDMREOT(true);
				m_open_channel=true;
				m_dmrNetwork->reset(2U);
				m_networkWatchdog.stop();
				m_dmrFrames = 0U;
				m_dmrinfo = false;
				m_firstSync = false;
				//m_not_busy = true;
			}

			if((DataType == DT_VOICE_LC_HEADER) && (DataType != m_dmrLastDT)) {
				m_gid = 0;
				m_netDst = (netflco == FLCO_GROUP ? "TG " : "") + m_lookup->findCS(DstId);
				if (SrcId == 9990U)
					m_rcv_callsign = "PARROT";
				else if (SrcId == 9U)
					m_rcv_callsign = "LOCAL";
				else if (SrcId == 4000U)
					m_rcv_callsign = "UNLINK";
				else {
					m_rcv_callsign = m_lookup->findCS(SrcId);							
					CReflector* tmp_ref = m_dmrReflectors->findById(std::to_string(DstId));
					if (tmp_ref) m_netDst = tmp_ref->m_name;
					//else m_netDst = std::string("TG") + std::to_string(DstId);
				}
				m_conv.putDMRHeader();
				m_open_channel=true;
				LogMessage("DMR audio received from %s to %s", m_rcv_callsign.c_str(), m_netDst.c_str());
				m_dmrinfo = true;
				m_rcv_callsign.resize(YSF_CALLSIGN_LENGTH, ' ');
				m_netDst.resize(YSF_CALLSIGN_LENGTH, ' ');
				m_dmrFrames = 0U;
				m_firstSync = false;
				m_not_busy = false;
			}

			if(DataType == DT_VOICE_SYNC)
				m_firstSync = true;

			if((DataType == DT_VOICE_SYNC || DataType == DT_VOICE) && m_firstSync) {
				unsigned char dmr_frame[50];

				silence_number = 0;
				tx_dmrdata.getData(dmr_frame);
				if (!m_dmrinfo) {
					m_netDst = (netflco == FLCO_GROUP ? "TG " : "") + m_lookup->findCS(DstId);
					if (SrcId == 9990U)
						m_rcv_callsign = "PARROT";
					else if (SrcId == 9U)
						m_rcv_callsign = "LOCAL";
					else if (SrcId == 4000U)
						m_rcv_callsign = "UNLINK";
					else{
						m_rcv_callsign = m_lookup->findCS(SrcId);															
						CReflector* tmp_ref = m_dmrReflectors->findById(std::to_string(DstId));
						if (tmp_ref) m_netDst = tmp_ref->m_name;
						//else m_netDst = "UNKNOW";
					}
					LogMessage("DMR audio late entry received from %s to %s", m_rcv_callsign.c_str(), m_netDst.c_str());
					m_rcv_callsign.resize(YSF_CALLSIGN_LENGTH, ' ');
					m_netDst.resize(YSF_CALLSIGN_LENGTH, ' ');
					m_dmrinfo = true;
				}

				m_conv.putDMR(dmr_frame); // Add DMR frame for YSF conversion
				m_open_channel=true;
				m_not_busy = false;
				m_dmrFrames++;
			}
		}
		else {
			if(DataType == DT_VOICE_SYNC || DataType == DT_VOICE) {
				unsigned char dmr_frame[50];

				silence_number = 0;
				tx_dmrdata.getData(dmr_frame);
				m_conv.putDMR(dmr_frame); // Add DMR frame for YSF conversion
				m_dmrFrames++;
				m_open_channel=true;
				m_not_busy = false;
			}
			
			m_networkWatchdog.clock(ms);
			if (m_networkWatchdog.hasExpired()) {
				LogDebug("Network watchdog has expired, %.1f seconds", float(m_dmrFrames) / 16.667F);
				m_dmrNetwork->reset(2U);
				m_conv.reset();
				m_networkWatchdog.stop();
				m_dmrFrames = 0U;
				m_dmrinfo = false;
				m_not_busy = true;
			}
		}
	m_dmrLastDT = DataType;
	m_lostTimer->start();
	}		
}

void CStreamer::DMR_send_Network(void) {
static unsigned int m_fill=0;
static unsigned int m_actual_step = 0;
static bool sending_silence=false;
	
	if ((m_dmrNetwork!=NULL) && (m_dmrWatch.elapsed() > DMR_FRAME_PER)) {			
		unsigned int dmrFrameType = m_conv.getDMR(m_dmrFrame);
		if (sending_silence) {
			CDMRData rx_dmrdata;
			unsigned int n_dmr = (m_dmr_cnt - 3U) % 6U;
			//LogMessage("Adding time: %d-%d",m_actual_step,m_fill);
			
			if (m_actual_step < m_fill) {
				CDMREMB emb;

				rx_dmrdata.setSlotNo(2U);
				rx_dmrdata.setSrcId(m_srcid);
				rx_dmrdata.setDstId(m_dstid);
				rx_dmrdata.setFLCO(m_dmrflco);
				rx_dmrdata.setN(n_dmr);
				rx_dmrdata.setSeqNo(m_dmr_cnt);
				rx_dmrdata.setBER(0U);
				rx_dmrdata.setRSSI(0U);
				rx_dmrdata.setDataType(DT_VOICE);

				if (!n_dmr) {
					rx_dmrdata.setDataType(DT_VOICE_SYNC);
					// Add sync
					CSync::addDMRAudioSync(m_dmrFrame, 0U);
					// Prepare Full LC data
					CDMRLC dmrLC = CDMRLC(m_dmrflco, m_srcid, m_dstid);
					// Configure the Embedded LC
					m_EmbeddedLC.setLC(dmrLC);
				}
				else {
					rx_dmrdata.setDataType(DT_VOICE);
					::memcpy(m_dmrFrame, DMR_SILENCE_DATA, DMR_FRAME_LENGTH_BYTES);					
					// Generate the Embedded LC
					unsigned char lcss = m_EmbeddedLC.getData(m_dmrFrame, n_dmr);
					// Generate the EMB
					emb.setColorCode(m_colorcode);
					emb.setLCSS(lcss);
					emb.getData(m_dmrFrame);
				}
				rx_dmrdata.setData(m_dmrFrame);
				m_dmrNetwork->write(rx_dmrdata);
			
				m_dmr_cnt++;
				m_actual_step++;
				m_dmrWatch.start();
			} else {
				sending_silence = false;
				unsigned int fill = (6U - n_dmr);
				
				if (n_dmr) {
					for (unsigned int i = 0U; i < fill; i++) {

						CDMREMB emb;
						CDMRData rx_dmrdata;

						rx_dmrdata.setSlotNo(2U);
						rx_dmrdata.setSrcId(m_srcid);
						rx_dmrdata.setDstId(m_dstid);
						rx_dmrdata.setFLCO(m_dmrflco);
						rx_dmrdata.setN(n_dmr);
						rx_dmrdata.setSeqNo(m_dmr_cnt);
						rx_dmrdata.setBER(0U);
						rx_dmrdata.setRSSI(0U);
						rx_dmrdata.setDataType(DT_VOICE);

						::memcpy(m_dmrFrame, DMR_SILENCE_DATA, DMR_FRAME_LENGTH_BYTES);

						// Generate the Embedded LC
						unsigned char lcss = m_EmbeddedLC.getData(m_dmrFrame, n_dmr);

						// Generate the EMB
						emb.setColorCode(m_colorcode);
						emb.setLCSS(lcss);
						emb.getData(m_dmrFrame);

						rx_dmrdata.setData(m_dmrFrame);
						m_dmrNetwork->write(rx_dmrdata);
						n_dmr++;
						m_dmr_cnt++;
					}
				}										
				
				LogMessage("End DMR received end of voice transmission, %.1f seconds", float(m_dmr_cnt) / 16.667F);
				rx_dmrdata.setSlotNo(2U);
				rx_dmrdata.setSrcId(m_srcid);
				rx_dmrdata.setDstId(m_dstid);
				rx_dmrdata.setFLCO(m_dmrflco);
				rx_dmrdata.setN(n_dmr);
				rx_dmrdata.setSeqNo(m_dmr_cnt);
				rx_dmrdata.setBER(0U);
				rx_dmrdata.setRSSI(0U);
				rx_dmrdata.setDataType(DT_TERMINATOR_WITH_LC);

				// Add sync
				CSync::addDMRDataSync(m_dmrFrame, 0);

				// Add SlotType
				CDMRSlotType slotType;
				slotType.setColorCode(m_colorcode);
				slotType.setDataType(DT_TERMINATOR_WITH_LC);
				slotType.getData(m_dmrFrame);
	
				// Full LC
				CDMRLC dmrLC = CDMRLC(m_dmrflco, m_srcid, m_dstid);
				CDMRFullLC fullLC;
				fullLC.encode(dmrLC, m_dmrFrame, DT_TERMINATOR_WITH_LC);
				
				rx_dmrdata.setData(m_dmrFrame);
				m_dmrNetwork->write(rx_dmrdata);
				}					
		}

		if(dmrFrameType == TAG_HEADER) {
			if (sending_silence) {
				sending_silence = false;
				m_dmrWatch.start();					
			}
			else {
				m_dmr_cnt = 0U;
				CDMRData rx_dmrdata;

				rx_dmrdata.setSlotNo(2U);
				rx_dmrdata.setSrcId(m_srcid);
				rx_dmrdata.setDstId(m_dstid);
				rx_dmrdata.setFLCO(m_dmrflco);
				rx_dmrdata.setN(0U);
				rx_dmrdata.setSeqNo(0U);
				rx_dmrdata.setBER(0U);
				rx_dmrdata.setRSSI(0U);
				rx_dmrdata.setDataType(DT_VOICE_LC_HEADER);
//				memcpy(ysf_radioid,std_ysf_radioid,5U);

				// Add sync
				CSync::addDMRDataSync(m_dmrFrame, 0);

				// Add SlotType
				CDMRSlotType slotType;
				slotType.setColorCode(m_colorcode);
				slotType.setDataType(DT_VOICE_LC_HEADER);
				slotType.getData(m_dmrFrame);
	
				// Full LC
				CDMRLC dmrLC = CDMRLC(m_dmrflco, m_srcid, m_dstid);
				CDMRFullLC fullLC;
				fullLC.encode(dmrLC, m_dmrFrame, DT_VOICE_LC_HEADER);
				m_EmbeddedLC.setLC(dmrLC);			
				rx_dmrdata.setData(m_dmrFrame);

				for (unsigned int i = 0U; i < 3U; i++) {
					rx_dmrdata.setSeqNo(m_dmr_cnt);
					m_dmrNetwork->write(rx_dmrdata);
					m_dmr_cnt++;
				}
				m_dmrWatch.start();
			}
		} else if(dmrFrameType == TAG_EOT) {
			CDMRData rx_dmrdata;
			unsigned int n_dmr = (m_dmr_cnt - 3U) % 6U;
			unsigned int fill = (6U - n_dmr);

			//LogMessage("DMR received end of voice transmission, %.1f seconds", float(m_dmr_cnt) / 16.667F);
			unsigned int time_blk_10 = int(float(m_dmr_cnt) / 1.6667F);
			if (time_blk_10<21) {
				sending_silence=true;
				m_fill=(((30-time_blk_10)/6)+1)*6; // 100ms por packet 
				m_actual_step = 0;
				m_dmrWatch.start();
			} else {
				if (n_dmr) {
					for (unsigned int i = 0U; i < fill; i++) {
						CDMREMB emb;
						CDMRData rx_dmrdata;

						rx_dmrdata.setSlotNo(2U);
						rx_dmrdata.setSrcId(m_srcid);
						rx_dmrdata.setDstId(m_dstid);
						rx_dmrdata.setFLCO(m_dmrflco);
						rx_dmrdata.setN(n_dmr);
						rx_dmrdata.setSeqNo(m_dmr_cnt);
						rx_dmrdata.setBER(0U);
						rx_dmrdata.setRSSI(0U);
						rx_dmrdata.setDataType(DT_VOICE);

						::memcpy(m_dmrFrame, DMR_SILENCE_DATA, DMR_FRAME_LENGTH_BYTES);

						// Generate the Embedded LC
						unsigned char lcss = m_EmbeddedLC.getData(m_dmrFrame, n_dmr);

						// Generate the EMB
						emb.setColorCode(m_colorcode);
						emb.setLCSS(lcss);
						emb.getData(m_dmrFrame);
						rx_dmrdata.setData(m_dmrFrame);
				
						//CUtils::dump(1U, "EOT DMR data:", m_dmrFrame, 33U);
						m_dmrNetwork->write(rx_dmrdata);
						n_dmr++;
						m_dmr_cnt++;
					}
				}					
				
				LogMessage("End DMR received end of voice transmission, %.1f seconds", float(m_dmr_cnt) / 16.667F);
				rx_dmrdata.setSlotNo(2U);
				rx_dmrdata.setSrcId(m_srcid);
				rx_dmrdata.setDstId(m_dstid);
				rx_dmrdata.setFLCO(m_dmrflco);
				rx_dmrdata.setN(n_dmr);
				rx_dmrdata.setSeqNo(m_dmr_cnt);
				rx_dmrdata.setBER(0U);
				rx_dmrdata.setRSSI(0U);
				rx_dmrdata.setDataType(DT_TERMINATOR_WITH_LC);

				// Add sync
				CSync::addDMRDataSync(m_dmrFrame, 0);

				// Add SlotType
				CDMRSlotType slotType;
				slotType.setColorCode(m_colorcode);
				slotType.setDataType(DT_TERMINATOR_WITH_LC);
				slotType.getData(m_dmrFrame);
	
				// Full LC
				CDMRLC dmrLC = CDMRLC(m_dmrflco, m_srcid, m_dstid);
				CDMRFullLC fullLC;
				fullLC.encode(dmrLC, m_dmrFrame, DT_TERMINATOR_WITH_LC);
				
				rx_dmrdata.setData(m_dmrFrame);
				//CUtils::dump(1U, "VOICE DMR data:", m_dmrFrame, 33U);
				m_dmrNetwork->write(rx_dmrdata);
				}
		} else if(dmrFrameType == TAG_DATA) {
			CDMREMB emb;
			CDMRData rx_dmrdata;
			unsigned int n_dmr = (m_dmr_cnt - 3U) % 6U;
			//sending_silence = false;

			rx_dmrdata.setSlotNo(2U);
			rx_dmrdata.setSrcId(m_srcid);
			rx_dmrdata.setDstId(m_dstid);
			rx_dmrdata.setFLCO(m_dmrflco);
			rx_dmrdata.setN(n_dmr);
			rx_dmrdata.setSeqNo(m_dmr_cnt);
			rx_dmrdata.setBER(0U);
			rx_dmrdata.setRSSI(0U);
		
			if (!n_dmr) {
				rx_dmrdata.setDataType(DT_VOICE_SYNC);
				// Add sync
				CSync::addDMRAudioSync(m_dmrFrame, 0U);
				// Prepare Full LC data
				CDMRLC dmrLC = CDMRLC(m_dmrflco, m_srcid, m_dstid);
				// Configure the Embedded LC
				m_EmbeddedLC.setLC(dmrLC);
			}
			else {
				rx_dmrdata.setDataType(DT_VOICE);
				// Generate the Embedded LC
				unsigned char lcss = m_EmbeddedLC.getData(m_dmrFrame, n_dmr);
				// Generate the EMB
				emb.setColorCode(m_colorcode);
				emb.setLCSS(lcss);
				emb.getData(m_dmrFrame);
			}
			rx_dmrdata.setData(m_dmrFrame);
			//CUtils::dump(1U, "VOICE DMR data:", m_dmrFrame, 33U);
			m_dmrNetwork->write(rx_dmrdata);
			m_dmr_cnt++;
			m_dmrWatch.start();
		}
	}	
}

unsigned int CStreamer::findYSFID(std::string cs, bool showdst)
{
	std::string cstrim;
	//bool dmrpc = false;
	unsigned int id;
	
	//LogMessage("cs=%s",cs.c_str());
	
	int first = cs.find_first_not_of(' ');
	int mid1 = cs.find_last_of('-');
	int mid2 = cs.find_last_of('/');
	int last = cs.find_last_not_of(' ');
	
	//LogMessage("trim=%s",cs.c_str());
	
	if (mid1 == -1 && mid2 == -1 && first == -1 && last == -1)
		cstrim = "N0CALL";
	else if (mid1 == -1 && mid2 == -1)
		cstrim = cs.substr(first, (last - first + 1));
	else if (mid1 > first)
		cstrim = cs.substr(first, (mid1 - first));
	else if (mid2 > first)
		cstrim = cs.substr(first, (mid2 - first));
	else
		cstrim = "N0CALL";

	//LogMessage("cstrim=%s",cstrim.c_str());

	if (m_lookup != NULL) {
		id = m_lookup->findID(cstrim);

		// if (m_dmrflco == FLCO_USER_USER)
		// 	dmrpc = true;
		// else if (m_dmrflco == FLCO_GROUP)
		// 	dmrpc = false;

		// if (id == 0) LogMessage("Not DMR ID %s->%s found, drooping voice data.",cs.c_str(),cstrim.c_str());
		// else {
		// 	if (showdst)
		// 		LogMessage("DMR ID of %s: %u, DstID: %s%u", cstrim.c_str(), id, dmrpc ? "" : "TG ", m_dstid);
		// 	else
		// 		LogMessage("DMR ID of %s: %u", cstrim.c_str(), id);
		// }
	} else id=0;

	//LogMessage("id=%d",id);

	return id;
}

std::string CStreamer::getSrcYSF_fromHeader(const unsigned char* buffer) {
	std::string rcv_callsign("");
	unsigned char dch[20U];	
	char tmp[11U];
	unsigned int ret;
	CYSFPayload ysfPayload;

	ret = ysfPayload.readDataFRModeData2(buffer + 35U,dch);
	if (ret) {
		memcpy(tmp,dch,10U);
		tmp[10U] = 0;
		rcv_callsign = std::string(tmp);
	}
	if ((dch[0] == 0x20) && (m_tg_type == YSF)) {
		memcpy(tmp,buffer+14U,10U);
		tmp[10U]=0;
		rcv_callsign = std::string(tmp);
	}
	
	strcpy(tmp,rcv_callsign.c_str());
	// LogMessage("Antes: %s",tmp);
	unsigned int i=0;
	while (i<strlen(tmp) && isalnum(tmp[i])) {
		i++;
	}
	i--;
	while ((i>0) && isdigit(tmp[i])) {
		i--;
	}
	tmp[i+1]=0U;
	rcv_callsign = std::string(tmp);
	if (rcv_callsign.empty()) rcv_callsign = std::string("UNKNOW");	
	rcv_callsign.resize(YSF_CALLSIGN_LENGTH, ' ');
	// LogMessage("Despues: %s",tmp);	
	// LogMessage("Get from header: %s",rcv_callsign.c_str());	
	return rcv_callsign;
}

std::string CStreamer::getSrcYSF_fromModem(const unsigned char* buffer) {
	std::string rcv_callsign("");
	char tmp[11U];

	::memcpy(tmp,buffer+14U,10U);
	tmp[10U]=0;
	rcv_callsign = std::string(tmp);
	strcpy(tmp,rcv_callsign.c_str());
//		LogMessage("Get from Data: %s",tmp);		
//	LogMessage("Antes: %s",tmp);
	unsigned int i=0;
	while (i<strlen(tmp) && isalnum(tmp[i])) {
		i++;
	}
	i--;
	while ((i>0) && isdigit(tmp[i])) {
		i--;
	}
	tmp[i+1]=0U;		
	rcv_callsign = std::string(tmp);
	if (rcv_callsign.empty()) rcv_callsign = std::string("UNKNOW");
	rcv_callsign.resize(YSF_CALLSIGN_LENGTH, ' ');
//	LogMessage("Despues: %s",tmp);	
	return rcv_callsign;
}

std::string CStreamer::getSrcYSF_fromData(const unsigned char* buffer) {
	std::string rcv_callsign("");
	char tmp[11U];

	if (m_tg_type == YSF) {
		::memcpy(tmp,buffer+14U,10U);
		tmp[10U]=0;
		rcv_callsign = std::string(tmp);
		strcpy(tmp,rcv_callsign.c_str());
//		LogMessage("Get from Data: %s",tmp);		
	//	LogMessage("Antes: %s",tmp);
		unsigned int i=0;
		while (i<strlen(tmp) && isalnum(tmp[i])) {
			i++;
		}
		i--;
		while ((i>0) && isdigit(tmp[i])) {
			i--;
		}
		tmp[i+1]=0U;		
		rcv_callsign = std::string(tmp);
	}
	if (rcv_callsign.empty()) rcv_callsign = m_rcv_callsign;
	if (rcv_callsign.empty()) rcv_callsign = std::string("UNKNOW");
	rcv_callsign.resize(YSF_CALLSIGN_LENGTH, ' ');
//	LogMessage("Despues: %s",tmp);	
	return rcv_callsign;
}

std::string CStreamer::getSrcYSF_fromFN1(const unsigned char* buffer) {	
	std::string rcv_callsign("");
	char tmp[11U];
	unsigned int ret;
	CYSFPayload ysfPayload;
	unsigned char dch[20U];

	ret = ysfPayload.readVDMode2Data(buffer + 35U,dch);
	if (ret) {
		memcpy(tmp,dch,10U);
		tmp[10U]=0;
		rcv_callsign = std::string(tmp);
		strcpy(tmp,rcv_callsign.c_str());
//		LogMessage("Get from FN1: %s",tmp);
		unsigned int i=0;
		while (i<strlen(tmp) && isalnum(tmp[i])) {
			i++;
		}
		i--;
		while ((i>0) && isdigit(tmp[i])) {
			i--;
		}
		tmp[i+1]=0U;
		rcv_callsign = std::string(tmp);
	}
	if (rcv_callsign.empty()) rcv_callsign = m_rcv_callsign;
	if (rcv_callsign.empty()) rcv_callsign = std::string("UNKNOW");
	rcv_callsign.resize(YSF_CALLSIGN_LENGTH, ' ');
//	LogMessage("Despues: %s",tmp);	
	return rcv_callsign;
}

void CStreamer::processWiresX(const unsigned char* buffer, unsigned char fi, unsigned char dt, unsigned char fn, unsigned char ft, unsigned char bn, unsigned char bt)
{
	assert(buffer != NULL);
    //LogMessage("Calling process with fi=%d,dt=%d,fn=%d,ft=%d,bn=%d,bt=%d",fi,dt,fn,ft,bn,bt);
	WX_STATUS status = m_wiresX->process(buffer + 35U, buffer + 14U, fi, dt, fn, ft, bn, bt);
	switch (status) {		
		case WXS_CONNECT: {			
			m_ysf_callsign = getSrcYSF_fromModem(buffer);
			LogMessage("Callsign connect: %s",m_ysf_callsign.c_str());
			m_srcid = findYSFID(m_ysf_callsign, true);
			m_dstid = m_wiresX->getDstID(); 
			m_wx_returned = WXS_CONNECT;
		}
		break;
		case WXS_PLAY: {
			LogMessage("Opening channel..");
			m_open_channel = true;
		}
		break;
		case WXS_DISCONNECT: {
			m_wx_returned = WXS_DISCONNECT;
		}
		break;
		default:
		break;
		}
}

void CStreamer::processDTMF(unsigned char* buffer, unsigned char dt)
{
	assert(buffer != NULL); 

	WX_STATUS status = WXS_NONE;
	switch (dt) {
	case YSF_DT_VD_MODE2:
		status = m_dtmf->decodeVDMode2(buffer + 35U, (buffer[34U] & 0x01U) == 0x01U);
		break;
	default:
		break;
	}

	switch (status) {
	case WXS_CONNECT: {
			m_ysf_callsign = getSrcYSF_fromModem(buffer);
			m_srcid = findYSFID(m_ysf_callsign, true);			
			std::string id = m_dtmf->getReflector();
			m_dstid = atoi(id.c_str());
			//m_change_TG = true;			
		}
		break;
		default:
		break;
	}			
}

void CStreamer::SendFinalPTT() {
	m_conv.putYSFHeader();
	m_conv.putDummyYSF();
	m_conv.putYSFEOT();
}


void CStreamer::SendDummyDMR(unsigned int srcid,unsigned int dstid, FLCO dmr_flco)
{
	CDMRData dmrdata;
	CDMRSlotType slotType;
	CDMRFullLC fullLC;

	int dmr_cnt = 0U;

	// Generate DMR LC for header and TermLC frames
	CDMRLC dmrLC = CDMRLC(dmr_flco, srcid, dstid);

	// Build DMR header
	dmrdata.setSlotNo(2U);
	dmrdata.setSrcId(srcid);
	dmrdata.setDstId(dstid);
	dmrdata.setFLCO(dmr_flco);
	dmrdata.setN(0U);
	dmrdata.setSeqNo(0U);
	dmrdata.setBER(0U);
	dmrdata.setRSSI(0U);
	dmrdata.setDataType(DT_VOICE_LC_HEADER);

	// Add sync
	CSync::addDMRDataSync(m_dmrFrame, 0);

	// Add SlotType
	slotType.setColorCode(m_colorcode);
	slotType.setDataType(DT_VOICE_LC_HEADER);
	slotType.getData(m_dmrFrame);

	// Full LC
	fullLC.encode(dmrLC, m_dmrFrame, DT_VOICE_LC_HEADER);

	dmrdata.setData(m_dmrFrame);

	// Send DMR header
	for (unsigned int i = 0U; i < 3U; i++) {
		dmrdata.setSeqNo(dmr_cnt);
		m_dmrNetwork->write(dmrdata);
		dmr_cnt++;
	}

	// // Build DMR TermLC
	// dmrdata.setSeqNo(dmr_cnt);
	// dmrdata.setDataType(DT_VOICE_SYNC);

	// // Add sync
	// CSync::addDMRDataSync(m_dmrFrame, 0);

	// // Add SlotType
	// slotType.setColorCode(m_colorcode);
	// slotType.setDataType(DT_VOICE_SYNC);
	// slotType.getData(m_dmrFrame);

	// // Full LC for TermLC frame
	// fullLC.encode(dmrLC, m_dmrFrame, DT_VOICE_SYNC);

	// dmrdata.setData(m_dmrFrame);

	// // Send DMR TermLC
	// m_dmrNetwork->write(dmrdata);



	// Build DMR TermLC
	dmrdata.setSeqNo(dmr_cnt);
	dmrdata.setDataType(DT_TERMINATOR_WITH_LC);

	// Add sync
	CSync::addDMRDataSync(m_dmrFrame, 0);

	// Add SlotType
	slotType.setColorCode(m_colorcode);
	slotType.setDataType(DT_TERMINATOR_WITH_LC);
	slotType.getData(m_dmrFrame);

	// Full LC for TermLC frame
	fullLC.encode(dmrLC, m_dmrFrame, DT_TERMINATOR_WITH_LC);

	dmrdata.setData(m_dmrFrame);

	// Send DMR TermLC
	m_dmrNetwork->write(dmrdata);
	m_unlinkReceived = false;
	m_dmrinfo = false;
}

unsigned char ysfFrame[155U];

void CStreamer::SendDummyYSF(CYSFNetwork *ysf_network, unsigned int dg_id)
{
	unsigned char m_ysf_cnt;
	unsigned char csd1[20U], csd2[20U];
	CYSFPayload payload;
	CYSFFICH fich;	
	unsigned int i;	

	if (dg_id == 0) return;	

	if (m_ysf_callsign.empty()) {
		m_ysf_callsign = m_callsign;
		m_ysf_callsign.resize(YSF_CALLSIGN_LENGTH, ' ');
	}

	unsigned int id=ysf_network->getRoomID();
	LogMessage("Room ID: %d",id);
	if (id == dg_id) return;
	LogMessage("Sending Dummy YSF, source: %s to DG-ID: %d",m_ysf_callsign.c_str(),dg_id);

	::memset(ysfFrame,0,155U);
	::memcpy(ysfFrame + 0U, "YSFD", 4U);
	::memcpy(ysfFrame + 4U, m_netDst.c_str(), YSF_CALLSIGN_LENGTH);
	::memcpy(ysfFrame + 14U, m_ysf_callsign.c_str(), YSF_CALLSIGN_LENGTH);
	::memcpy(ysfFrame + 24U, "ALL       ", YSF_CALLSIGN_LENGTH);
	ysfFrame[34U] = 0U; // Net frame counter

	CSync::addYSFSync(ysfFrame + 35U);

	// Set the FICH
	fich.setFI(YSF_FI_HEADER);
	fich.setCS(2U);
	fich.setCM(0U);			
	fich.setFN(0U);
	fich.setFT(7U);
	fich.setDev(0U);
	fich.setDT(YSF_DT_DATA_FR_MODE);			
	fich.setDGId(dg_id);
	fich.setMR(2U);
	fich.encode(ysfFrame + 35U);

	memset(csd1, '*', YSF_CALLSIGN_LENGTH);
	memcpy(csd1 + YSF_CALLSIGN_LENGTH/2, ysf_radioid, YSF_CALLSIGN_LENGTH/2);			
	memcpy(csd1 + YSF_CALLSIGN_LENGTH, m_ysf_callsign.c_str(), YSF_CALLSIGN_LENGTH);
	memset(csd2 , ' ', YSF_CALLSIGN_LENGTH + YSF_CALLSIGN_LENGTH);

	payload.writeHeader(ysfFrame + 35U, csd1, csd2);
	ysf_network->write(ysfFrame);
	m_ysf_cnt=1;

	::memset(ysfFrame,0,155U);
	::memcpy(ysfFrame + 0U, "YSFD", 4U);
	::memcpy(ysfFrame + 4U, m_netDst.c_str(), YSF_CALLSIGN_LENGTH);
	::memcpy(ysfFrame + 14U, m_ysf_callsign.c_str(), YSF_CALLSIGN_LENGTH);
	::memcpy(ysfFrame + 24U, "ALL       ", YSF_CALLSIGN_LENGTH);
	CSync::addYSFSync(ysfFrame + 35U);
	//payload.writeVDMode2Data(ysfFrame + 35U, (const unsigned char*)"          ");
	fich.setFI(YSF_FI_COMMUNICATIONS);
	fich.setCS(2U);
	fich.setCM(0U);			
	fich.setFN(0U);
	fich.setFT(7U);
	fich.setDev(0U);
	fich.setDT(YSF_DT_DATA_FR_MODE);			
	fich.setDGId(dg_id);
	fich.setMR(2U);	
	fich.setFT(2U);		



	for (i=0;i<2;i++) {
		fich.setFN(i+1);
		fich.encode(ysfFrame + 35U);		
		m_ysfFrame[34U] = (m_ysf_cnt & 0x7FU) <<1; // Net frame counter
		ysf_network->write(ysfFrame);
		m_ysf_cnt++;
	}

	::memset(ysfFrame,0,155U);
	::memcpy(ysfFrame + 0U, "YSFD", 4U);
	::memcpy(ysfFrame + 4U, m_netDst.c_str(), YSF_CALLSIGN_LENGTH);
	::memcpy(ysfFrame + 14U, m_ysf_callsign.c_str(), YSF_CALLSIGN_LENGTH);
	::memcpy(ysfFrame + 24U, "ALL       ", YSF_CALLSIGN_LENGTH);
	ysfFrame[34U] = (m_ysf_cnt & 0x7FU) <<1; // Net frame counter

	CSync::addYSFSync(ysfFrame + 35U);

	fich.setFI(YSF_FI_TERMINATOR);
	fich.setCS(2U);
	fich.setCM(0U);			
	fich.setFN(0U);
	fich.setFT(7U);
	fich.setDev(0U);
	fich.setDT(YSF_DT_DATA_FR_MODE);			
	fich.setDGId(dg_id);
	fich.setMR(2U);	
	fich.setFT(2U);		
	fich.encode(ysfFrame + 35U);

	payload.writeHeader(ysfFrame + 35U, csd1, csd2);
	ysf_network->write(ysfFrame);	
}
