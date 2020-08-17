/*
*   Copyright (C) 2016-2019 by Jonathan Naylor G4KLX
*   Copyright (C) 2018,2019 by Andy Uribe CA6JAU
*   Copyright (C) 2019 by Manuel Sanchez EA7EE
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

#include "YSFGateway.h"
#include "UDPSocket.h"
#include "StopWatch.h"
#include "Version.h"
#include "YSFFICH.h"
#include "Thread.h"
#include "Timer.h"
#include "Log.h"
#include "Utils.h"

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pwd.h>
#endif

#if defined(_WIN32) || defined(_WIN64)
const char* DEFAULT_INI_FILE = "YSFGateway.ini";
#else
const char* DEFAULT_INI_FILE = "/etc/YSFGateway.ini";
#endif

#include <functional>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <clocale>
#include <cmath>
#include <cctype>

int end = 0;

#if !defined(_WIN32) && !defined(_WIN64)
void sig_handler(int signo)
{
	if (signo == SIGTERM) {
		end = 1;
		::fprintf(stdout, "Received SIGTERM\n");
	}
}
#endif

#define DMR_FRAME_PER       55U
#define YSF_FRAME_PER       90U
#define BEACON_PER			55U

#define XLX_SLOT            2U
#define XLX_COLOR_CODE      3U

#define TIME_MIN			60000U
#define TIME_SEC			1000U


const char* HEADER1 = "This software is for use on amateur radio networks only,";
const char* HEADER2 = "it is to be used for educational purposes only. Its use on";
const char* HEADER3 = "commercial networks is strictly prohibited.";
const char* HEADER4 = "Copyright(C) 2018,2019 by CA6JAU, EA7EE, G4KLX and others";
char const *text_type[6] = {"NONE","YSF ","FCS ","DMR ","NXDN","P25 "};

// DT1 and DT2, suggested by Manuel EA7EE
const unsigned char dt1_temp[] = {0x31, 0x22, 0x62, 0x5F, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char dt2_temp[] = {0x00, 0x00, 0x00, 0x00, 0x6C, 0x20, 0x1C, 0x20, 0x03, 0x08};

unsigned char m_gps_buffer[20U];	
char beacon_name[]="/usr/local/sbin/beacon.amb";
bool ysfNetworkEnabled;
bool m_nxdnNetworkEnabled;
bool m_p25NetworkEnabled;

int main(int argc, char** argv)
{
	const char* iniFile = DEFAULT_INI_FILE;
	if (argc > 1) {
		for (int currentArg = 1; currentArg < argc; ++currentArg) {
			std::string arg = argv[currentArg];
			if ((arg == "-v") || (arg == "--version")) {
				::fprintf(stdout, "YSFGateway version %s\n", VERSION);
				return 0;
			} else if (arg.substr(0, 1) == "-") {
				::fprintf(stderr, "Usage: YSFGateway [-v|--version] [filename]\n");
				return 1;
			} else {
				iniFile = argv[currentArg];
			}
		}
	}

#if !defined(_WIN32) && !defined(_WIN64)
	// Capture SIGTERM to finish gracelessly
	if (signal(SIGTERM, sig_handler) == SIG_ERR)
		::fprintf(stdout, "Can't catch SIGTERM\n");
#endif

	CYSFGateway* gateway = new CYSFGateway(std::string(iniFile));

	int ret = gateway->run();

	delete gateway;

	return ret;
}


CYSFGateway::CYSFGateway(const std::string& configFile) :
m_storage(NULL),
m_callsign(),
m_conf(configFile),
m_writer(NULL),
m_gps(NULL),
m_ysfReflectors(NULL),
m_fcsReflectors(NULL),
m_dmrReflectors(NULL),
m_nxdnReflectors(NULL),
m_p25Reflectors(NULL),
m_lookup(NULL),
m_wiresX(NULL),
m_dtmf(NULL),
m_APRS(NULL),
m_conv(),
m_colorcode(1U),
m_srcHS(1U),
m_srcid(1U),
m_defsrcid(1U),
m_dstid(1U),
m_ptt_dstid(1U),
m_ptt_pc(false),
m_dmrpc(false),
m_ysfNetwork(NULL),
m_fcsNetwork(NULL),
m_dmrNetwork(NULL),
m_current(),
m_startup(),
m_exclude(false),
m_inactivityTimer(NULL),
m_lostTimer(1000U, 120U),
m_networkWatchdog(1000U, 0U, 500U),
m_dmrFrames(0U),
m_ysfFrames(0U),
m_EmbeddedLC(),
m_dmrflco(FLCO_GROUP),
m_dmrinfo(false),
m_idUnlink(4000U),
m_flcoUnlink(FLCO_GROUP),
m_saveAMBE(false),
m_rpt_buffer(5000U, "RPTGATEWAY"),
m_xlxmodule(),
m_xlxConnected(false),
m_xlxReflectors(NULL),
m_xlxrefl(0U),
m_beacon(false)
{
	m_ysfFrame = new unsigned char[200U];
	m_dmrFrame = new unsigned char[50U];
	
	::memset(m_ysfFrame, 0U, 200U);
	::memset(m_dmrFrame, 0U, 50U);
	m_conv.reset();
}

CYSFGateway::~CYSFGateway()
{
	delete[] m_ysfFrame;
	delete[] m_dmrFrame;
}

int CYSFGateway::run()
{
	bool ret = m_conf.read();
	if (!ret) {
		::fprintf(stderr, "YSFGateway: cannot read the .ini file\n");
		return 1;
	}
	setlocale(LC_ALL, "C");
	
//	unsigned int logDisplayLevel = m_conf.getLogDisplayLevel();	
#if !defined(_WIN32) && !defined(_WIN64)
	bool m_daemon = m_conf.getDaemon();	
	if (m_daemon) {
		// Create new process
		pid_t pid = ::fork();
		if (pid == -1) {
			::fprintf(stderr, "Couldn't fork() , exiting\n");
			return -1;
		}
		else if (pid != 0) {
			exit(EXIT_SUCCESS);
		}
		// Create new session and process group
		if (::setsid() == -1) {
			::fprintf(stderr, "Couldn't setsid(), exiting\n");
			return -1;
		}
		// Set the working directory to the root directory
		if (::chdir("/") == -1) {
			::fprintf(stderr, "Couldn't cd /, exiting\n");
			return -1;
		}
		// If we are currently root...
		if (getuid() == 0) {
			struct passwd* user = ::getpwnam("mmdvm");
			if (user == NULL) {
				::fprintf(stderr, "Could not get the mmdvm user, exiting\n");
				return -1;
			}
			uid_t mmdvm_uid = user->pw_uid;
			gid_t mmdvm_gid = user->pw_gid;
			// Set user and group ID's to mmdvm:mmdvm
			if (setgid(mmdvm_gid) != 0) {
				::fprintf(stderr, "Could not set mmdvm GID, exiting\n");
				return -1;
			}
			if (setuid(mmdvm_uid) != 0) {
				::fprintf(stderr, "Could not set mmdvm UID, exiting\n");
				return -1;
			}
			// Double check it worked (AKA Paranoia) 
			if (setuid(0) != -1) {
				::fprintf(stderr, "It's possible to regain root - something is wrong!, exiting\n");
				return -1;
			}
		}
	}
#endif

	ret = ::LogInitialise(m_conf.getLogFilePath(), m_conf.getLogFileRoot(), m_conf.getLogFileLevel(), m_conf.getLogDisplayLevel());
	if (!ret) {
		::fprintf(stderr, "YSFGateway: unable to open the log file\n");
		return 1;
	}

#if !defined(_WIN32) && !defined(_WIN64)
	if (m_daemon) {
		::close(STDIN_FILENO);
		::close(STDOUT_FILENO);
		::close(STDERR_FILENO);
	}
#endif

	LogInfo(HEADER1);
	LogInfo(HEADER2);
	LogInfo(HEADER3);
	LogInfo(HEADER4);
	
	m_callsign = m_conf.getCallsign();
	
	bool debug            = m_conf.getNetworkDebug();
	in_addr rptAddress    = CUDPSocket::lookup(m_conf.getRptAddress());
	unsigned int rptPort  = m_conf.getRptPort();
	std::string myAddress = m_conf.getMyAddress();
	unsigned int myPort   = m_conf.getMyPort();
	m_saveAMBE		 	  = m_conf.getSaveAMBE();
	m_NoChange			  = m_conf.getNetworkNoChange();
	

	// Get timeout and beacon times from Conf.cpp
	unsigned int m_timeout_time = m_conf.getNetworkInactivityTimeout();
	m_beacon_time = m_conf.getBeaconTime();
	unsigned int reloadTime = m_conf.getNetworkReloadTime();
	bool wiresXMakeUpper = m_conf.getWiresXMakeUpper();

	ysfNetworkEnabled = m_conf.getYSFNetworkEnabled();
	m_fcsNetworkEnabled = m_conf.getFCSNetworkEnabled();
	m_dmrNetworkEnabled = m_conf.getDMRNetworkEnabled();
	m_nxdnNetworkEnabled = m_conf.getNXDNNetworkEnabled();
	m_p25NetworkEnabled = m_conf.getP25NetworkEnabled();
	
	unsigned int lev_a = m_conf.getAMBECompA();
	unsigned int lev_b = m_conf.getAMBECompB();
	m_conv.LoadTable(lev_a,lev_b);	
	
	m_startup = m_conf.getNetworkStartup();
	m_last_DMR_TG = m_conf.getDMRStartup();
	m_last_YSF_TG = m_conf.getYSFStartup();
	m_last_FCS_TG = m_conf.getFCSStartup();
	m_last_NXDN_TG = m_conf.getNXDNStartup();
	m_last_P25_TG = m_conf.getP25Startup();
	
	m_tg_type = (enum TG_TYPE) m_conf.getNetworkTypeStartup();
	
	// Testing only
	unsigned int m_original = atoi(m_startup.c_str());
	m_dstid = m_original;
	
	LogInfo("General Parameters");
	if (m_dstid == 0) LogInfo("    Startup TG: %s", m_startup.c_str());
	else LogInfo("    Startup TG: %d", m_dstid);
    LogInfo("    Startup Network Type: %s",	text_type[m_conf.getNetworkTypeStartup()]);
	LogInfo("    Timeout TG Time: %d min", m_timeout_time);
	LogInfo("    AMBE Recording: %s", m_saveAMBE ? "yes" : "no");
    LogInfo("    TG List Reload Time: %d min", reloadTime);
	LogInfo("    Make Upper: %s", wiresXMakeUpper ? "yes" : "no");
	LogInfo("    No Change option: %s", m_NoChange ? "yes" : "no");
	LogInfo("    YSF Enabled: %s", ysfNetworkEnabled ? "yes" : "no");
	LogInfo("    FCS Enabled: %s", m_fcsNetworkEnabled ? "yes" : "no");
	LogInfo("    DMR Enabled: %s", m_dmrNetworkEnabled ? "yes" : "no");
	LogInfo("    NXDN Enabled: %s", m_nxdnNetworkEnabled ? "yes" : "no");
	LogInfo("    P25 Enabled: %s", m_p25NetworkEnabled ? "yes" : "no");			
	LogInfo("    YSF Startup: %d", m_last_YSF_TG);
	LogInfo("    FCS Startup: %d", m_last_FCS_TG);
	LogInfo("    DMR Startup: %d", m_last_DMR_TG);
	LogInfo("    NXDN Startup: %d", m_last_NXDN_TG);			
	LogInfo("    P25 Startup: %d", m_last_P25_TG);		
	
	CYSFNetwork rptNetwork(myAddress, myPort, m_callsign, debug);
	rptNetwork.setDestination("MMDVM", rptAddress, rptPort);

	ret = rptNetwork.open();
	if (!ret) {
		::LogError("Cannot open the repeater network port");
		::LogFinalise();
		return 1;
	}

	if (ysfNetworkEnabled) {
		unsigned int ysfPort = m_conf.getYSFNetworkPort();

		m_ysfNetwork = new CYSFNetwork(ysfPort, m_callsign, debug);
		ret = m_ysfNetwork->open();
		if (!ret) {
			::LogError("Cannot open the YSF reflector network port");
			::LogFinalise();
			return 1;
		}
	}

	if (m_fcsNetworkEnabled) {
		unsigned int txFrequency = m_conf.getTxFrequency();
		unsigned int rxFrequency = m_conf.getRxFrequency();
		std::string locator = calculateLocator();
		unsigned int id = m_conf.getId();

		unsigned int fcsPort = m_conf.getFCSNetworkPort();

		m_fcsNetwork = new CFCSNetwork(fcsPort, m_callsign, rxFrequency, txFrequency, locator, id, debug);
		ret = m_fcsNetwork->open();
		if (!ret) {
			::LogError("Cannot open the FCS reflector network port");
			::LogFinalise();
			return 1;
		}
	}
	
	if (m_dmrNetworkEnabled) {
		LogMessage("Opening DMR network connection");
		ret = createDMRNetwork();
		if (!ret) {
			::LogError("Cannot open DMR Network");
			::LogFinalise();
			return 1;
		}

		std::string lookupFile = m_conf.getDMRIdLookupFile();
		if (lookupFile.empty()) lookupFile  = "/usr/local/etc/DMRIds.dat";
		m_lookup = new CDMRLookup(lookupFile, reloadTime);
		m_lookup->read();
		if (m_dmrpc)
			m_dmrflco = FLCO_USER_USER;
		else
			m_dmrflco = FLCO_GROUP;		
	}
	
	if (m_timeout_time>0) {
		m_inactivityTimer = new CTimer(m_timeout_time * TIME_MIN);
	}
	else m_inactivityTimer = NULL;
	
	std::string file_ysf = m_conf.getYSFNetworkHosts();
	if (file_ysf.empty()) file_ysf = "/usr/local/etc/YSFHosts.txt";
	std::string file_fcs = m_conf.getFCSNetworkFile();
	if (file_fcs.empty()) file_fcs = "/usr/local/etc/FCSHosts.txt";	
	std::string file_dmr = m_conf.getDMRNetworkFile();
	if (file_dmr.empty()) file_dmr = "/usr/local/etc/DMRHosts.txt";	
	std::string file_nxdn = m_conf.getNXDNNetworkFile();
	if (file_nxdn.empty()) file_nxdn = "/usr/local/etc/TGList_NXDN.txt";	
	std::string file_p25 = m_conf.getP25NetworkFile();
	if (file_p25.empty()) file_p25 = "/usr/local/etc/TGList_P25.txt";	

	LogInfo("Reflector List Parameters");
	LogInfo("    YSF List: %s", file_ysf.c_str());
	LogInfo("    FCS List: %s", file_fcs.c_str());
	LogInfo("    DMR List: %s", file_dmr.c_str());
	LogInfo("    NXDN List: %s", file_nxdn.c_str());
	LogInfo("    P25 List: %s", file_p25.c_str());
	
	m_ysfReflectors = new CReflectors(file_ysf, YSF, reloadTime, wiresXMakeUpper);
	m_fcsReflectors = new CReflectors(file_fcs, FCS, reloadTime, wiresXMakeUpper);
	m_dmrReflectors = new CReflectors(file_dmr, DMR, reloadTime, wiresXMakeUpper);
	m_nxdnReflectors = new CReflectors(file_nxdn, NXDN, reloadTime, wiresXMakeUpper);
	m_p25Reflectors = new CReflectors(file_p25, P25, reloadTime, wiresXMakeUpper);
	
	std::string fileName    = m_conf.getDMRXLXFile();
	if (!fileName.empty()) {
		m_xlxReflectors = new CReflectors(fileName, DMR, reloadTime, wiresXMakeUpper);
		m_xlxReflectors->load();
	}

	createWiresX(&rptNetwork, wiresXMakeUpper);
	
	m_ysfReflectors->reload();
	m_fcsReflectors->reload();
	m_dmrReflectors->reload();
	m_nxdnReflectors->reload();
	m_p25Reflectors->reload();
		
	createGPS();
	if (m_conf.getAPRSAPIKey().empty()) {
		::memcpy(m_gps_buffer, dt1_temp, 10U);
		::memcpy(m_gps_buffer + 10U, dt2_temp, 10U);
		LogMessage("Geeting position from aprs.fi disabled. %s",m_conf.getAPRSAPIKey());		
	}
	else {
		m_APRS = new CAPRSReader(m_conf.getAPRSAPIKey(), m_conf.getAPRSRefresh());
		LogMessage("Geeting position information from aprs.fi with ApiKey.");
	}
	if (startupLinking()==false) {
		LogMessage("Cannot conect to startup reflector. Exiting...");
		rptNetwork.close();

		if (m_APRS != NULL) {
			m_APRS->stop();
			delete m_APRS;
		}

		if (m_gps != NULL) {
			m_writer->close();
			delete m_writer;
			delete m_gps;
		}

		if (m_ysfNetwork != NULL) {
			m_ysfNetwork->close();
			delete m_ysfNetwork;
		}

		if (m_fcsNetwork != NULL) {
			m_fcsNetwork->close();
			delete m_fcsNetwork;
		}

		delete m_wiresX;
		delete m_storage;
		
		if (m_xlxReflectors != NULL)
			delete m_xlxReflectors;	
		
		::LogFinalise();

		return 0;
	};
	
	m_stopWatch.start();
	m_ysfWatch.start();	

	if (m_dmrNetworkEnabled) m_dmrWatch.start();

	m_file_out=fopen(beacon_name,"rb");
	if (!m_file_out || (m_beacon_time==0)) {
		LogMessage("Beacon off");
		m_beacon = false;
	} else {
		fclose(m_file_out);
		LogMessage("Beacon on. Timeout: %d minutes",m_beacon_time);
		m_beacon = true;
		m_beacon_Watch.start();
	}
	
	LogMessage("Starting YSFGateway-%s", VERSION);

	m_not_busy=true;	
	m_beacon_status = BE_OFF;
	m_open_channel=false;
	m_data_ready=false;
	
	m_unlinkReceived = false;
	m_TG_connect_state = TG_NONE;
	//ysfWatchdog.stop();
	unsigned int ms=0;
	unsigned char buffer[2000U];
	memset(buffer, 0U, 2000U);
			
	for (;end == 0;) {
		// DMR connect logic
		if (m_dmrNetworkEnabled) DMR_reconect_logic();
		
		// Beacon processing
		if (m_beacon) BeaconLogic();		

		// Put DMR Network Data
		if (m_dmrNetworkEnabled) DMR_send_Network();
		
		// Get DMR network data
		if (m_dmrNetworkEnabled) DMR_get_Modem(ms);

		// Get from modem and proccess data info		
		GetFromModem(&rptNetwork);
		
		// Send to Network delayed data packets from modem
		while (m_rpt_buffer.hasData() && m_data_ready) {
			m_rpt_buffer.getData(buffer,155U);
			if ((m_ysfNetwork != NULL && (m_tg_type==YSF) && !m_exclude) && 
				 (::memcmp(buffer + 0U, "YSFD", 4U) == 0)) {
				 	m_ysfNetwork->write(buffer);
				}
			if ((m_fcsNetwork != NULL && (m_tg_type==FCS) && !m_exclude) &&
				 (::memcmp(buffer + 0U, "YSFD", 4U) == 0)) {
					m_fcsNetwork->write(buffer);
				}			
		}		

		// YSF Network receive and process		
		while ((m_ysfNetwork != NULL) && (m_ysfNetwork->read(buffer) > 0U)) {
			if (m_tg_type == YSF) GetFromNetwork(buffer,&rptNetwork);
		}

		while ((m_fcsNetwork != NULL) && (m_fcsNetwork->read(buffer) > 0U)) {	
			if (m_tg_type == FCS) GetFromNetwork(buffer,&rptNetwork);
		}

        YSFPlayback(&rptNetwork);

		ms = m_stopWatch.elapsed();
		m_stopWatch.start();

		rptNetwork.clock(ms);
		if (m_dmrNetwork) m_dmrNetwork->clock(ms);

		m_ysfReflectors->clock(ms);
		m_fcsReflectors->clock(ms);
		m_dmrReflectors->clock(ms);
		m_nxdnReflectors->clock(ms);
		m_p25Reflectors->clock(ms);
		
		if (m_ysfNetwork != NULL)
			m_ysfNetwork->clock(ms);
		if (m_fcsNetwork != NULL)
			m_fcsNetwork->clock(ms);
		if (m_dmrNetwork != NULL) 
			m_dmrNetwork->clock(ms);		
			
		if (m_writer != NULL)
			m_writer->clock(ms);
		m_wiresX->clock(ms);

		if (m_inactivityTimer!=NULL) {
			m_inactivityTimer->clock(ms);
			if (m_inactivityTimer->isRunning() && m_inactivityTimer->hasExpired()) {
				if (m_dstid != m_original) {
					LogWarning("Inactivity Timer Fired.");				
					m_lostTimer.stop();
					m_ysf_callsign = m_callsign;
					startupLinking();
					m_lostTimer.start();
				}
				m_inactivityTimer->start();
			} 
		}
		
		if (m_xlxReflectors != NULL)
			m_xlxReflectors->clock(ms);
				
		m_lostTimer.clock(ms);
/*		if (m_lostTimer.isRunning() && m_lostTimer.hasExpired()) {
			LogWarning("Link has failed, polls lost");
			m_ysf_callsign = m_callsign;
			startupLinking();

			m_inactivityTimer->start();
			m_lostTimer.start();			
		} */

		if (ms < 5U)
			CThread::sleep(5U);
	}

	rptNetwork.close();

	if (m_APRS != NULL) {
		m_APRS->stop();
		delete m_APRS;
	}

	if (m_gps != NULL) {
		m_writer->close();
		delete m_writer;
		delete m_gps;
	}

	if (m_ysfNetwork != NULL) {
		m_ysfNetwork->close();
		delete m_ysfNetwork;
	}

	if (m_fcsNetwork != NULL) {
		m_fcsNetwork->close();
		delete m_fcsNetwork;
	}

	delete m_wiresX;
	delete m_storage;
	
	if (m_xlxReflectors != NULL)
		delete m_xlxReflectors;	
	
	::LogFinalise();

	return 0;
}

void CYSFGateway::createGPS()
{
	if (!m_conf.getAPRSEnabled())
		return;

	std::string hostname 	= m_conf.getAPRSServer();
	unsigned int port    	= m_conf.getAPRSPort();
	std::string password 	= m_conf.getAPRSPassword();
	std::string callsign 	= m_conf.getAPRSCallsign();
	std::string desc     	= m_conf.getAPRSDescription();
	std::string icon        = m_conf.getAPRSIcon();
	std::string beacon_text = m_conf.getAPRSBeaconText();
	bool followMode			= m_conf.getAPRSFollowMe();
	int beacon_time			= m_conf.getAPRSBeaconTime();	
		
	if (callsign.empty())
		callsign = m_callsign;
	
	LogMessage("APRS Parameters");
	LogMessage("    Callsign: %s", callsign.c_str());
	LogMessage("    Node Callsign: %s", m_callsign.c_str());
	LogMessage("    Server: %s", hostname.c_str());
	LogMessage("    Port: %u", port);
	LogMessage("    Passworwd: %s", password.c_str());
	LogMessage("    Description: %s", desc.c_str());
	LogMessage("    Icon: %s", icon.c_str());
	LogMessage("    Beacon Text: %s", beacon_text.c_str());
	LogMessage("    Follow Mode: %s", followMode ? "yes" : "no");	
	LogMessage("    Beacon Time: %d", beacon_time);	
	
	m_writer = new CAPRSWriter(callsign, password, hostname, port, followMode);

	unsigned int txFrequency = m_conf.getTxFrequency();
	unsigned int rxFrequency = m_conf.getRxFrequency();


	m_writer->setInfo(m_callsign, txFrequency, rxFrequency, desc, icon, beacon_text, beacon_time, followMode);
	bool enabled = m_conf.getMobileGPSEnabled();
	if (enabled) {
		std::string address = m_conf.getMobileGPSAddress();
		unsigned int port   = m_conf.getMobileGPSPort();

		m_writer->setMobileLocation(address, port);
	} else {
		float latitude  = m_conf.getLatitude();
		float longitude = m_conf.getLongitude();
		int height      = m_conf.getHeight();

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

void CYSFGateway::createWiresX(CYSFNetwork* rptNetwork, bool makeUpper)
{
	assert(rptNetwork != NULL);

	m_storage = new CWiresXStorage;
	m_wiresX = new CWiresX(m_storage, m_callsign, rptNetwork, makeUpper);
	m_dtmf = new CDTMF();
	
	std::string name = m_conf.getName();

	unsigned int txFrequency = m_conf.getTxFrequency();
	unsigned int rxFrequency = m_conf.getRxFrequency();
	
	m_wiresX->setInfo(name, txFrequency, rxFrequency, m_conf.getNetworkNoChange());
	
	m_parrotAddress = CUDPSocket::lookup(m_conf.getYSFNetworkParrotAddress());
	m_parrotPort = m_conf.getYSFNetworkParrotPort();

	m_ysfReflectors->setParrot(&m_parrotAddress,m_parrotPort);
	
	m_ysf2nxdnAddress = CUDPSocket::lookup(m_conf.getYSFNetworkYSF2NXDNAddress());
	m_ysf2nxdnPort = m_conf.getYSFNetworkYSF2NXDNPort();
	
	m_ysf2p25Address = CUDPSocket::lookup(m_conf.getYSFNetworkYSF2P25Address());
	m_ysf2p25Port = m_conf.getYSFNetworkYSF2P25Port();

	m_ysfReflectors->load();
	m_fcsReflectors->load();
	m_dmrReflectors->load();
	m_nxdnReflectors->load();
	m_p25Reflectors->load();
	
	m_wiresX->start();
}

void CYSFGateway::processWiresX(const unsigned char* buffer, unsigned char fi, unsigned char dt, unsigned char fn, unsigned char ft, unsigned char bn, unsigned char bt)
{
	assert(buffer != NULL);
    //LogMessage("Calling process with fi=%d,dt=%d,fn=%d,ft=%d,bn=%d,bt=%d",fi,dt,fn,ft,bn,bt);
	WX_STATUS status = m_wiresX->process(buffer + 35U, buffer + 14U, fi, dt, fn, ft, bn, bt);
	switch (status) {		
		case WXS_CONNECT: {
		    char tmp[14];
			::memcpy(tmp,buffer + 14U,10U);
			tmp[10]=0;			
			m_ysf_callsign = std::string(tmp);
			//LogMessage("Callsign connect: %s",m_ysf_callsign.c_str());
			m_srcid = findYSFID(m_ysf_callsign, true);	
			//LogMessage("m_srcid: %d",m_srcid);
			int ret = TG_Connect(m_wiresX->getDstID());
			if (ret) {
				LogMessage("Connected to %05d - \"%s\" has been requested by %10.10s", m_dstid, m_current.c_str(), m_ysf_callsign.c_str());
				if ((m_tg_type == YSF) || (m_tg_type == FCS)) m_wiresX->SendCReply();
			} else {
				LogMessage("Error with connect");
				m_wiresX->SendDReply();
			}				
		}
		break;
		default:
		break;
	}
}

void CYSFGateway::processDTMF(unsigned char* buffer, unsigned char dt)
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
		    char tmp[14];
			::memcpy(tmp,buffer + 14U,10U);
			tmp[10]=0;
			m_ysf_callsign = std::string(tmp);
			m_srcid = findYSFID(m_ysf_callsign, true);			
			std::string id = m_dtmf->getReflector();
			unsigned int tmp_dst_id = atoi(id.c_str());
			
			int ret = TG_Connect(tmp_dst_id);
			if (ret) {
				LogMessage("DTMF Connected to %05d - \"%s\" has been requested by %10.10s", m_dstid, m_current.c_str(), m_ysf_callsign.c_str());
				if ((m_tg_type == YSF) || (m_tg_type == FCS)) m_wiresX->SendCReply();
			} else {
				LogMessage("DTMF Error with connect");
				m_wiresX->SendDReply();
			}				
		}
		break;
		default:
		break;
	}			
}

std::string CYSFGateway::calculateLocator()
{
	std::string locator;

	float latitude  = m_conf.getLatitude();
	float longitude = m_conf.getLongitude();

	if (latitude < -90.0F || latitude > 90.0F)
		return "AA00AA";

	if (longitude < -360.0F || longitude > 360.0F)
		return "AA00AA";

	latitude += 90.0F;

	if (longitude > 180.0F)
		longitude -= 360.0F;

	if (longitude < -180.0F)
		longitude += 360.0F;

	longitude += 180.0F;

	float lon = ::floor(longitude / 20.0F);
	float lat = ::floor(latitude  / 10.0F);

	locator += 'A' + (unsigned int)lon;
	locator += 'A' + (unsigned int)lat;

	longitude -= lon * 20.0F;
	latitude  -= lat * 10.0F;

	lon = ::floor(longitude / 2.0F);
	lat = ::floor(latitude  / 1.0F);

	locator += '0' + (unsigned int)lon;
	locator += '0' + (unsigned int)lat;

	longitude -= lon * 2.0F;
	latitude  -= lat * 1.0F;

	lon = ::floor(longitude / (2.0F / 24.0F));
	lat = ::floor(latitude  / (1.0F / 24.0F));

	locator += 'A' + (unsigned int)lon;
	locator += 'A' + (unsigned int)lat;

	return locator;
}

bool is_number(const std::string& s)
{
    std::string::const_iterator it = s.begin();
    while (it != s.end() && std::isdigit(*it)) ++it;
    return !s.empty() && it == s.end();
}

bool CYSFGateway::startupLinking()
{
	int tmp_id = m_conf.getNetworkTypeStartup();	
	CReflector* reflector;
	
	switch(tmp_id) {
		case NONE:
			LogMessage("Error startup Type not defined.");
			return false;
			break;
		case YSF: {
			if (!ysfNetworkEnabled) return false;
			m_actual_ref=m_ysfReflectors;
		}
		break;
		case FCS: {
			if (!m_fcsNetworkEnabled) return false;
			m_actual_ref=m_fcsReflectors;
		}
		break;		
		case DMR: {
			if (!m_dmrNetworkEnabled) return false;
			m_actual_ref=m_dmrReflectors;
		}
		break; 
		case NXDN: {
			if (!m_nxdnNetworkEnabled) return false;
			m_actual_ref=m_nxdnReflectors;
		}
		break;
		case P25: {
			if (!m_p25NetworkEnabled) return false;
			m_actual_ref=m_p25Reflectors;
		}
		break;
	}
	m_wiresX->setReflectors(m_actual_ref);
	
	m_tg_type = (TG_TYPE) tmp_id;

	if ((m_tg_type == DMR) && !m_xlxmodule.empty() && !m_xlxConnected) {
		writeXLXLink(m_srcid, m_dstid, m_dmrNetwork);
		LogMessage("XLX, Linking to reflector XLX%03u, module %s", m_xlxrefl, m_xlxmodule.c_str());
		m_xlxConnected = true;
	} else {
		if ((m_tg_type == DMR) && m_ysf_callsign.empty()) {
			m_wiresX->setReflectors(m_dmrReflectors);				
			reflector = m_actual_ref->findById(std::to_string(m_dstid));
			if (reflector != NULL) {
				m_wiresX->setReflector(reflector, m_dstid);
				m_current = reflector->m_name;
				m_current.resize(YSF_CALLSIGN_LENGTH, ' ');	
				LogMessage("DMR Reflector: %s",m_current.c_str());
			}
			if (m_inactivityTimer!=NULL) m_inactivityTimer->start();			
			m_last_DMR_TG = m_dstid; 
			m_dmrNetwork->enable(true);
			return true;
		}
		if (is_number(m_startup)) reflector = m_actual_ref->findById(m_startup);
		else reflector = m_actual_ref->findByName(m_startup);		
		if (reflector != NULL) {
			if (TG_Connect(atoi(reflector->m_id.c_str()))) {
				LogMessage("Automatic (re-)connection to %5.5s - \"%s\"", reflector->m_id.c_str(), reflector->m_name.c_str());
				return true;
			}
			else LogMessage("Not Possible connection - %d", atoi(reflector->m_id.c_str()));	
		} else LogMessage("Unknown reflector - %s", m_startup);	
	}
	return false;
}

void CYSFGateway::AMBE_write(unsigned char* buffer, unsigned char fi, unsigned char dt, unsigned char fn, unsigned char ft, unsigned char bn, unsigned char bt) {
	static char tmp[40];
	static int count_file_AMBE=0;
	static FILE *file;
	static int m_ysfFrames;
				
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
				m_ysfFrames = 0U;				
			}
		} else if (fi == YSF_FI_TERMINATOR) {
			fclose(file);
			LogMessage("AMBE Closing %s file, %.1f seconds", tmp, float(m_ysfFrames) / 10.0F);
/*			int extraFrames = (m_hangTime / 100U) - m_ysfFrames - 2U;
			for (int i = 0U; i < extraFrames; i++)
				m_conv.putDummyYSF(); */	 			
			m_conv.putYSFEOT();
			m_ysfFrames = 0U;
		} else if (fi == YSF_FI_COMMUNICATIONS) {
			m_conv.putYSF_Mode2(buffer + 35U,file);
			m_ysfFrames++;
		}
	}	
}

bool CYSFGateway::TG_Connect(unsigned int dstID) {
	CReflector* reflector;
	std::string dst_str_ID = std::to_string(dstID);
    TG_TYPE last_type=m_tg_type;
	
	if (dstID < 6) {
		dstID--;
		if (dstID==0){
			if (!ysfNetworkEnabled) return false;
			dstID=1;
			m_tg_type=YSF;		
		}
		else if (dstID == DMR) {
			if (!m_dmrNetworkEnabled) return false;
			if (dstID != last_type) m_conv.reset();
			dstID=m_last_DMR_TG;
			m_tg_type=DMR;
		}
		else if (dstID == YSF) {
			if (!ysfNetworkEnabled) return false;
			if (last_type == DMR) {
				m_dmrNetwork->enable(false);					
				m_conv.reset();
			}				
			dstID=m_last_YSF_TG;
			m_tg_type=YSF;		
		}
		else if (dstID == FCS) {
			if (!m_fcsNetworkEnabled) return false;
			if (last_type == DMR) {
				m_dmrNetwork->enable(false);					
				m_conv.reset();	
			}
			dstID=m_last_FCS_TG;
			m_tg_type=FCS;		
		} else if (dstID == NXDN) {
			if (!m_nxdnNetworkEnabled) return false;
			if (last_type == DMR) {
				m_dmrNetwork->enable(false);					
				m_conv.reset();	
			}
			m_ysfNetwork->setDestination("YSF2NXDN", m_ysf2nxdnAddress, m_ysf2nxdnPort);
			m_ysfNetwork->writePoll(3U);						
			dstID=m_last_NXDN_TG;
			m_tg_type=NXDN;
		} else if (dstID == P25) {
			if (!m_p25NetworkEnabled) return false;
			if (last_type == DMR) {
				m_dmrNetwork->enable(false);					
				m_conv.reset();			
			}				
			m_ysfNetwork->setDestination("YSF2P25", m_ysf2p25Address, m_ysf2p25Port);
			m_ysfNetwork->writePoll(3U);					
			dstID=m_last_P25_TG;
			m_tg_type=P25;
		}
		
	}
	// Don´t try to reconnect to mode
	if ((dstID<6) && (dstID>1)) return false;
	dst_str_ID = std::to_string(dstID);
	switch (m_tg_type) {
		case NONE:
			LogMessage("Error startup Type not defined.");
			break;		
		case YSF:
		    if (dstID==1) {
				reflector = m_ysfReflectors->findById(dst_str_ID);
				LogMessage("Trying PARROT");					
				if (m_inactivityTimer!=NULL) m_inactivityTimer->stop();
				m_lostTimer.stop();

				m_current = "PARROT";
				m_current.resize(YSF_CALLSIGN_LENGTH, ' ');				
				m_ysfNetwork->setDestination(m_current, m_parrotAddress, m_parrotPort);
				m_ysfNetwork->writePoll(3U);

				if (m_inactivityTimer!=NULL) m_inactivityTimer->start();
				m_lostTimer.start();					
				m_tg_type = YSF;						

				m_dstid = dstID;
				m_wiresX->setReflectors(m_ysfReflectors);					
				m_wiresX->setReflector(reflector, m_dstid);			
			}
			else if (m_ysfNetwork != NULL) {
				if (m_inactivityTimer!=NULL) m_inactivityTimer->stop();
				m_lostTimer.stop();

				reflector = m_ysfReflectors->findById(dst_str_ID);
				if (reflector != NULL) {
					//LogMessage("Automatic (re-)connection to %5.5s - \"%s\"", reflector->m_id.c_str(), reflector->m_name.c_str());

					m_ysfNetwork->setDestination(reflector->m_name, reflector->m_address, reflector->m_port);
					m_ysfNetwork->writePoll(3U);

					m_current = reflector->m_name;
					m_current.resize(YSF_CALLSIGN_LENGTH, ' ');	
					if (m_inactivityTimer!=NULL) m_inactivityTimer->start();
					m_lostTimer.start();					
					m_tg_type = YSF;						

					m_dstid = dstID;
					m_wiresX->setReflectors(m_ysfReflectors);					
					m_wiresX->setReflector(reflector, m_dstid);
					m_last_YSF_TG = dstID;
				} else return false;
			}
			break;
		case FCS:		
			if (m_fcsNetwork != NULL) {
				if (m_inactivityTimer!=NULL) m_inactivityTimer->stop();
				m_lostTimer.stop();				

				reflector = m_fcsReflectors->findById(dst_str_ID);
				if (reflector != NULL) {
					
					bool ok = m_fcsNetwork->writeLink("FCS00" + reflector->m_id);
					if (ok) {
						//LogMessage("Automatic (re-)connection to %s", reflector->m_name.c_str());

						m_current = reflector->m_name;
						m_current.resize(YSF_CALLSIGN_LENGTH, ' ');	
						if (m_inactivityTimer!=NULL) m_inactivityTimer->start();
						m_lostTimer.start();
						m_tg_type = FCS;						

						m_dstid = dstID;
						m_last_FCS_TG = dstID;						
						m_wiresX->setReflectors(m_fcsReflectors);
						m_wiresX->setReflector(reflector, m_dstid);
						m_last_FCS_TG = dstID;
					} else return false;							
				} else return false;					
			}
			break;
		case DMR:
			m_dmrNetwork->enable(true);
			reflector = m_dmrReflectors->findById(dst_str_ID);
			if (reflector != NULL) {		
				//LogMessage("DMR connection to %s", reflector->m_name.c_str());				
				int tglistOpt = reflector->m_opt;

				switch (tglistOpt) {
					case 0:
						m_ptt_pc = false;
						m_dstid = dstID;
						m_ptt_dstid = m_dstid;
						m_dmrflco = FLCO_GROUP;
						LogMessage("Connect to TG %d has been requested by %s", m_dstid, m_ysf_callsign.c_str());
						break;
				
					case 1:
						m_ptt_pc = true;
						m_dstid = 9U;
						m_dmrflco = FLCO_GROUP;
						LogMessage("Connect to REF %d has been requested by %s", m_ptt_dstid, m_ysf_callsign.c_str());
						break;
					
					case 2:
						m_ptt_dstid = 0;
						m_ptt_pc = true;
						m_dstid = dstID;
						//m_dstid = m_wiresX->getFullDstID();
						m_dmrflco = FLCO_USER_USER;
						LogMessage("Connect to %d has been requested by %s", m_dstid, m_ysf_callsign.c_str());
						break;
				
					default:
						m_ptt_pc = false;
						m_dstid = dstID;
						//m_dstid = m_wiresX->getFullDstID();
						m_ptt_dstid = m_dstid;
						m_dmrflco = FLCO_GROUP;
						LogMessage("Connect to TG %d has been requested by %s", m_dstid, m_ysf_callsign.c_str());
						break;
				}

				if (m_enableUnlink && (m_ptt_dstid != m_idUnlink) && (m_ptt_dstid != 5000)) {
					 m_not_busy=false;
					 LogMessage("Sending DMR Disconnect: Src: %s Dst: %s%d", m_ysf_callsign.c_str(), m_flcoUnlink == FLCO_GROUP ? "TG " : "", m_idUnlink);

					 SendDummyDMR(m_srcid, m_idUnlink, m_flcoUnlink);

					m_unlinkReceived = false;
					m_TG_connect_state = WAITING_UNLINK;
				} else 
					m_TG_connect_state = SEND_REPLY;

				m_TGChange.start();				
				m_current = reflector->m_name;
				m_current.resize(YSF_CALLSIGN_LENGTH, ' ');	
				if (m_inactivityTimer!=NULL) m_inactivityTimer->start();
				m_tg_type = DMR;				
				m_last_DMR_TG = dstID; 
				m_wiresX->setReflector(reflector, m_dstid);
				m_wiresX->setReflectors(m_dmrReflectors);
				m_last_DMR_TG = dstID;
			} else return false;
			break;
		case NXDN:
			reflector = m_nxdnReflectors->findById(dst_str_ID);
			if (reflector != NULL) {		
				LogMessage("NXDN connection to %s", reflector->m_name.c_str());				

				m_current.assign(reflector->m_name);
				m_current.resize(YSF_CALLSIGN_LENGTH, ' ');	
				if (m_inactivityTimer!=NULL) m_inactivityTimer->start();
				m_lostTimer.start();
				m_tg_type = NXDN;				
				m_dstid = dstID;
				m_last_NXDN_TG = dstID;
				m_wiresX->setReflector(reflector, m_dstid);
				m_wiresX->setReflectors(m_nxdnReflectors);				
				m_wiresX->SendRConnect(m_ysfNetwork);
				m_last_NXDN_TG = dstID;
			} else return false;
			break;
		case P25:
			reflector = m_p25Reflectors->findById(dst_str_ID);
			if (reflector != NULL) {		
				LogMessage("P25 connection to %s", reflector->m_name.c_str());				

				m_current.assign(reflector->m_name);
				m_current.resize(YSF_CALLSIGN_LENGTH, ' ');	
				if (m_inactivityTimer!=NULL) m_inactivityTimer->start();
				m_lostTimer.start();
				m_tg_type = P25;				
				m_dstid = dstID;
				m_last_P25_TG = dstID;
				m_wiresX->setReflector(reflector, m_dstid);
				m_wiresX->setReflectors(m_p25Reflectors);				
				m_wiresX->SendRConnect(m_ysfNetwork);
				m_last_P25_TG = dstID;
			} else return false;
			break;
	}
	
	m_netDst = m_current;
	m_netDst.resize(YSF_CALLSIGN_LENGTH, ' ');
	
	return true;
}
	
void CYSFGateway::GetFromModem(CYSFNetwork* rptNetwork){
unsigned char buffer[200];
unsigned char d_buffer[200];
unsigned int len;
	
	while ((len=rptNetwork->read(buffer)) > 0U) {
		if (m_inactivityTimer!=NULL) m_inactivityTimer->start();		
		if (::memcmp(buffer, "YSFD", 4U) != 0U) continue;
		CYSFFICH fich;
		bool valid = fich.decode(buffer + 35U);
		if (valid) {
			unsigned char fi = fich.getFI();
			unsigned char dt = fich.getDT();
			unsigned char fn = fich.getFN();
			unsigned char ft = fich.getFT();
			unsigned char bn = fich.getBN();
			unsigned char bt = fich.getBT();
			
			if (m_saveAMBE) 
				AMBE_write(buffer, fi, dt, fn, ft, bn, bt);
			
			if (m_gps != NULL)
				m_gps->data(buffer + 14U, buffer + 35U, fi, dt, fn, ft, m_tg_type, m_dstid);			

			if (fi==YSF_FI_HEADER) {
				m_data_ready=false;
				m_exclude = (dt == YSF_DT_DATA_FR_MODE);
			}
			CYSFPayload payload;
			
			payload.readVDMode2Data(buffer + 35U, d_buffer);
			//CUtils::dump("Block received from modem",d_buffer,10U);
			//CUtils::dump("Block received from modem",buffer,len);
			//LogMessage("Packet len= %d, fi=%d,dt=%d,fn=%d,ft=%d,bn=%d,bt=%d.",len,fi,dt,fn,ft,bn,bt);	
			if (dt != YSF_DT_DATA_FR_MODE) {
				m_data_ready = true;
				if (m_tg_type!=DMR) m_rpt_buffer.addData(buffer,len);
				else {
					if (fi == YSF_FI_HEADER) {
						m_ysf_callsign = getSrcYSF(buffer);
						m_dmrNetwork->reset(2U);	// OE1KBC fix
						m_srcid = findYSFID(m_ysf_callsign, true);						
						m_conv.putYSFHeader();
						m_ysfFrames = 0U;
					} else if (fi == YSF_FI_TERMINATOR) {
						LogMessage("Received YSF Communication, %.1f seconds", float(m_ysfFrames) / 10.0F);
/*						int extraFrames = (m_hangTime / 100U) - m_ysfFrames - 2U;
						for (int i = 0U; i < extraFrames; i++)
							m_conv.putDummyYSF();	*/					
						m_conv.putYSFEOT();
						m_ysfFrames = 0U;					
					} else if (fi == YSF_FI_COMMUNICATIONS) {
						m_conv.putYSF(buffer + 35U);
						m_ysfFrames++;							
					}
				}				
			} else {
				if (fi == YSF_FI_COMMUNICATIONS) {		
					processDTMF(buffer, dt);
					processWiresX(buffer, fi, dt, fn, ft, bn, bt);
					if ((fn == ft) && (bn == bt)) {
						if (m_wiresX->sendNetwork()) {
							m_exclude=false;
							LogMessage("Data allowed to go");
						} 
						else LogMessage("Data not allowed to go to Network");
						m_data_ready = true;
					}
				}
				if (m_tg_type!=DMR) m_rpt_buffer.addData(buffer,len);
			}

		if ((buffer[34U] & 0x01U) == 0x01U) {
			if (m_gps != NULL)
				m_gps->reset();
			m_dtmf->reset();
			}
		}
	}
}

void CYSFGateway::BeaconLogic(void) {
	unsigned char buffer[40];
	unsigned int n;	
	static bool first_time_b = true;
	
		// If Beacon time start voice beacon transmit
		if (first_time_b || (m_not_busy && (m_beacon_Watch.elapsed()> (m_beacon_time*TIME_MIN)))) {
			m_not_busy=false;
			m_beacon_status = BE_INIT;
			m_bea_voice_Watch.start();
			m_beacon_Watch.start();
			first_time_b = false;
		}
		
		// Beacon Logic
		if ((m_beacon_status != BE_OFF) && (m_bea_voice_Watch.elapsed() > BEACON_PER)) {

			switch (m_beacon_status) {
				case BE_INIT:
						m_rcv_callsign = "BEACON";
						m_rcv_callsign.resize(YSF_CALLSIGN_LENGTH, ' ');
						m_gid = 0;
						if (m_APRS != NULL)
							m_APRS->get_gps_buffer(m_gps_buffer,(int)(m_conf.getLatitude() * 1000),(int)(m_conf.getLongitude() * 1000));
						else {
							::memcpy(m_gps_buffer, dt1_temp, 10U);
							::memcpy(m_gps_buffer + 10U, dt2_temp, 10U);							
						}
						m_file_out=fopen(beacon_name,"rb");
						if (!m_file_out) {
							LogMessage("Error opening file: %s.",beacon_name);
						}
						else {
							LogMessage("Beacon Init: %s.",beacon_name);
							//fread(buffer,4U,1U,file_out);
							m_conv.putDMRHeader();
							//m_ysfWatch.start();							
							m_beacon_status = BE_DATA;
						}
						m_bea_voice_Watch.start();
						break;

				case BE_DATA:
						n=fread(buffer,1U,24U,m_file_out);
						if (n>23U) {
							m_conv.AMB2YSF(buffer);
							m_conv.AMB2YSF(buffer+8U);
							m_conv.AMB2YSF(buffer+16U);
						} else {
							m_beacon_status = BE_EOT;
							m_conv.putDMREOT(true);
							if (m_file_out) fclose(m_file_out);
							m_beacon_Watch.start();
						}
						m_bea_voice_Watch.start();
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


char *get_radio(char c) {
	static char radio[10U];

	switch (c) {
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
	case 0x2BU: 
		::strcpy(radio, "FT-70");
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

void CYSFGateway::GetFromNetwork(unsigned char *buffer, CYSFNetwork* rtpNetwork) {
static unsigned char tmp[20];
static bool first_time;
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
		}
	
		CYSFFICH fich;
		bool valid = fich.decode(buffer + 35U);

		if (valid) {
			unsigned char fi = fich.getFI();
			unsigned char dt = fich.getDT();
			unsigned char fn = fich.getFN();
			unsigned char ft = fich.getFT();  // ft=6 no gps  ft=7 gps
			
			//LogMessage("RX Packet gid=%d, fi=%d,dt=%d,fn=%d,ft=%d.",m_gid,fi,dt,fn,ft);										
			if ((dt==YSF_DT_DATA_FR_MODE) || (dt==YSF_DT_VOICE_FR_MODE)) {
				// Data packets go direct to modem
				rtpNetwork->write(buffer);	
			} else if (dt==YSF_DT_VD_MODE2) {
				if (fi==YSF_FI_HEADER) {
					m_not_busy=false;
					m_rcv_callsign = getSrcYSF(buffer);
					LogMessage("Received voice data *%s* from *%s*.",m_rcv_callsign.c_str(),m_current.c_str());
					if (ft==6U) {
						if (m_APRS != NULL) m_APRS->get_gps_buffer(m_gps_buffer,m_rcv_callsign);
						else {
							::memcpy(m_gps_buffer, dt1_temp, 10U);
							::memcpy(m_gps_buffer + 10U, dt2_temp, 10U);
						}
					}
					m_gid = fich.getDGId();
					m_conv.putDMRHeader();
					first_time = true;
					m_open_channel=true;					
				} else if (fi==YSF_FI_COMMUNICATIONS) {
					// Test if late entry
					if (m_open_channel==false) {
					// if (m_rcv_callsign.empty()  || (strcmp(m_rcv_callsign.c_str(),"BEACON    ")==0)) {
						if (ft==6U) {
							if (m_APRS != NULL) m_APRS->get_gps_buffer(m_gps_buffer,m_rcv_callsign);
							else {
								::memcpy(m_gps_buffer, dt1_temp, 10U);
								::memcpy(m_gps_buffer + 10U, dt2_temp, 10U);
							}
						}
						//m_rcv_callsign = getSrcYSF(buffer);
						m_gid = fich.getDGId();
						LogMessage("Late Entry from %s",m_rcv_callsign.c_str());
						m_not_busy=false;
						m_conv.putDMRHeader();
						first_time = true;
						m_open_channel=true;						
					}
					// Update gps info for ft=6
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
					m_rcv_callsign = getSrcYSF(buffer);
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
							}						
						} else {
							if ((*(tmp + 5U) != 0x00) || (*(tmp + 2U) != 0x62)) {
								memcpy(m_gps_buffer,tmp,10U);
								ysfPayload.readVDMode2Data(buffer + 35U, m_gps_buffer + 10U);
								if (first_time) {
									CUtils::dump("GPS Real info found",m_gps_buffer,20U);
									LogMessage("Radio: %s.",get_radio(*(tmp+4)));
									first_time = false;
								}
							}
						}
					}
					m_conv.putVCH(buffer + 35U);
				} else if (fi==YSF_FI_TERMINATOR) {
					LogMessage("YSF EOT received");
					//m_not_busy=true;
					m_conv.putDMREOT(true);  // changed from false
				}
			}
		}
	}
	m_lostTimer.start();
}	


void CYSFGateway::YSFPlayback(CYSFNetwork *rptNetwork) {

	// YSF Playback 
	if ((m_ysfWatch.elapsed() > YSF_FRAME_PER) && (m_open_channel || (m_beacon_status!=BE_OFF))) {
		// Playback YSF
		::memset(m_ysfFrame+35U,0U,200U);
		unsigned int ysfFrameType = m_conv.getYSF(m_ysfFrame + 35U);
		
		if(ysfFrameType == TAG_HEADER) {
			//m_not_busy = false;
			m_ysf_cnt = 0U;

			::memcpy(m_ysfFrame + 0U, "YSFD", 4U);
			::memcpy(m_ysfFrame + 4U, m_current.c_str(), YSF_CALLSIGN_LENGTH);
//			else ::memcpy(m_ysfFrame + 4U, m_netDst.c_str(), YSF_CALLSIGN_LENGTH);
			::memcpy(m_ysfFrame + 14U, m_rcv_callsign.c_str(), YSF_CALLSIGN_LENGTH);
			::memcpy(m_ysfFrame + 24U, "ALL       ", YSF_CALLSIGN_LENGTH);
			m_ysfFrame[34U] = 0U; // Net frame counter

			CSync::addYSFSync(m_ysfFrame + 35U);

			// Set the FICH
			CYSFFICH fich;
			fich.setFI(YSF_FI_HEADER);
			fich.setCS(2U);
			fich.setCM(0U);			
			fich.setFN(0U);
			fich.setFT(7U);
			fich.setDev(0U);
			fich.setDT(YSF_DT_VD_MODE2);			
			fich.setDGId(m_gid);
			fich.setMR(2U);
			fich.encode(m_ysfFrame + 35U);

			unsigned char csd1[20U], csd2[20U];
			memset(csd1, '*', YSF_CALLSIGN_LENGTH);
			memcpy(csd1 + YSF_CALLSIGN_LENGTH, m_rcv_callsign.c_str(), YSF_CALLSIGN_LENGTH);
			memset(csd2 , ' ', YSF_CALLSIGN_LENGTH + YSF_CALLSIGN_LENGTH);

			CYSFPayload payload;
			payload.writeHeader(m_ysfFrame + 35U, csd1, csd2);
			//LogMessage("Header Playback");
			rptNetwork->write(m_ysfFrame);

			m_ysf_cnt++;
			m_ysfWatch.start();
		}
		else if (ysfFrameType == TAG_EOT) {
			unsigned int fn = (m_ysf_cnt-1) % 8U;
			// LogMessage("EOT Playback before: %d",fn);		
			
			if (fn!=7) {
			 	LogMessage("YSFPlayback fn=%d Adding %d blocks",fn,7-fn);
			 	for (unsigned int i=0; i<(7U-fn);i++) 
					m_conv.putDMRSilence();
			 	m_conv.putDMREOT(false);
				m_ysfWatch.start();
			 	return;
			}

			::memcpy(m_ysfFrame + 0U, "YSFD", 4U);
			::memcpy(m_ysfFrame + 4U, m_current.c_str(), YSF_CALLSIGN_LENGTH);
//			else ::memcpy(m_ysfFrame + 4U, m_netDst.c_str(), YSF_CALLSIGN_LENGTH);
			::memcpy(m_ysfFrame + 14U, m_rcv_callsign.c_str(), YSF_CALLSIGN_LENGTH);
			::memcpy(m_ysfFrame + 24U, "ALL       ", YSF_CALLSIGN_LENGTH);
			m_ysfFrame[34U] = (m_ysf_cnt & 0x7FU) <<1; // Net frame counter

			CSync::addYSFSync(m_ysfFrame + 35U);

			// Set the FICH
			CYSFFICH fich;
			fich.setFI(YSF_FI_TERMINATOR);
			fich.setCS(2U);
			fich.setCM(0U);							
			fich.setFN(0U);
			fich.setFT(7U);
			fich.setDev(0U);
			fich.setDT(YSF_DT_VD_MODE2);
			//fich.setSQL(1U);			
			fich.setDGId(m_gid);
			fich.setMR(2U);
			fich.encode(m_ysfFrame + 35U);

			unsigned char csd1[20U], csd2[20U];
			memset(csd1, '*', YSF_CALLSIGN_LENGTH);
			memcpy(csd1 + YSF_CALLSIGN_LENGTH, m_rcv_callsign.c_str(), YSF_CALLSIGN_LENGTH);
			memset(csd2, ' ', YSF_CALLSIGN_LENGTH + YSF_CALLSIGN_LENGTH);				

			CYSFPayload payload;
			payload.writeHeader(m_ysfFrame + 35U, csd1, csd2);
			//LogMessage("EOT Playback");
			rptNetwork->write(m_ysfFrame);
			m_not_busy=true;
			m_open_channel=false;
			if (m_beacon_status != BE_OFF) m_beacon_status = BE_OFF;		
		}
		else if (ysfFrameType == TAG_DATA) {
			CYSFFICH fich;
			CYSFPayload ysfPayload;

			unsigned int fn = (m_ysf_cnt - 1U) % 8U;

			::memcpy(m_ysfFrame + 0U, "YSFD", 4U);
			::memcpy(m_ysfFrame + 4U, m_current.c_str(), YSF_CALLSIGN_LENGTH);
			//else ::memcpy(m_ysfFrame + 4U, m_netDst.c_str(), YSF_CALLSIGN_LENGTH);
			::memcpy(m_ysfFrame + 14U, m_rcv_callsign.c_str(), YSF_CALLSIGN_LENGTH);
			::memcpy(m_ysfFrame + 24U, "ALL       ", YSF_CALLSIGN_LENGTH);

			// Add the YSF Sync
			CSync::addYSFSync(m_ysfFrame + 35U);
			switch (fn) {
				case 0:
					ysfPayload.writeVDMode2Data(m_ysfFrame + 35U, (const unsigned char*)"**********");
					break;
				case 1:
					ysfPayload.writeVDMode2Data(m_ysfFrame + 35U, (const unsigned char*)m_rcv_callsign.c_str());
					break;
				case 2:
					ysfPayload.writeVDMode2Data(m_ysfFrame + 35U, (const unsigned char*)m_current.c_str());
					break; 						
				case 6:
					if ((m_tg_type == DMR) && (m_beacon_status == BE_OFF)) {
						if (m_APRS != NULL) m_APRS->get_gps_buffer(m_gps_buffer,m_rcv_callsign);
						else {
							::memcpy(m_gps_buffer, dt1_temp, 10U);
							::memcpy(m_gps_buffer + 10U, dt2_temp, 10U);
						}
					}
					ysfPayload.writeVDMode2Data(m_ysfFrame + 35U, (const unsigned char*) m_gps_buffer); 
					break;
				case 7:
					ysfPayload.writeVDMode2Data(m_ysfFrame + 35U, (const unsigned char*) m_gps_buffer + 10U);
					break;
				default:
					ysfPayload.writeVDMode2Data(m_ysfFrame + 35U, (const unsigned char*)"          ");
			}

			// Set the FICH
			fich.setFI(YSF_FI_COMMUNICATIONS);
			fich.setCS(2U);
			fich.setCM(0U);						
			fich.setFN(fn);
			fich.setFT(7U);
			fich.setDev(0U);
			fich.setDT(YSF_DT_VD_MODE2);		
			fich.setMR(YSF_MR_BUSY);
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
		}
	}	
	
}

void CYSFGateway::SendDummyDMR(unsigned int srcid,unsigned int dstid, FLCO dmr_flco)
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
}

unsigned int CYSFGateway::findYSFID(std::string cs, bool showdst)
{
	std::string cstrim;
	bool dmrpc = false;
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

		if (m_dmrflco == FLCO_USER_USER)
			dmrpc = true;
		else if (m_dmrflco == FLCO_GROUP)
			dmrpc = false;

		if (id == 0) LogMessage("Not DMR ID %s->%s found, drooping voice data.",cs.c_str(),cstrim.c_str());
		else {
			if (showdst)
				LogMessage("DMR ID of %s: %u, DstID: %s%u", cstrim.c_str(), id, dmrpc ? "" : "TG ", m_dstid);
			else
				LogMessage("DMR ID of %s: %u", cstrim.c_str(), id);
		}
	} else id=0;

	//LogMessage("id=%d",id);

	return id;
}

std::string CYSFGateway::getSrcYSF(const unsigned char* buffer)
{
	unsigned char cbuffer[155U];
	CYSFPayload ysfPayload;
	std::string rcv_callsign;
	char tmp[20];
	
	::memcpy(cbuffer,buffer,155U);
	ysfPayload.processHeaderData(cbuffer + 35U);
	rcv_callsign=ysfPayload.getSource();
	if (rcv_callsign.empty()) {
		::memcpy(tmp,cbuffer+14U,10U);
		tmp[10U]=0;
		rcv_callsign = std::string(tmp);
	}
	strcpy(tmp,rcv_callsign.c_str());
//	LogMessage("Antes: %s",tmp);
	unsigned int i=0;
	while (i<strlen(tmp) && isalnum(tmp[i])) {
		i++;
	}
	tmp[i]=0U;
	rcv_callsign = std::string(tmp);
	rcv_callsign.resize(YSF_CALLSIGN_LENGTH, ' ');
//	LogMessage("Despues: %s",tmp);	
	return rcv_callsign;
}

bool CYSFGateway::createDMRNetwork()
{
	std::string address  = m_conf.getDMRNetworkAddress();
	m_xlxmodule          = m_conf.getDMRXLXModule();
	m_xlxrefl            = m_conf.getDMRXLXReflector();
	unsigned int port    = m_conf.getDMRNetworkPort();
	unsigned int local   = m_conf.getDMRNetworkLocal();
	std::string password = m_conf.getDMRNetworkPassword();
	bool debug           = m_conf.getNetworkDebug();
	bool slot1           = false;
	bool slot2           = true;
	bool duplex          = false;
	HW_TYPE hwType       = HWT_MMDVM;
	
	if (address.empty()) address = "84.232.5.113";
	unsigned int jitter=500U;
	if (port==0) port = 62031U;
    if (local==0) local = 62032U;
	if (password.empty()) password = "passw0rd";
	
	m_srcHS = m_conf.getId();
	m_colorcode = 1U;
	m_idUnlink = m_conf.getDMRNetworkIDUnlink();
	bool pcUnlink = m_conf.getDMRNetworkPCUnlink();

	if (m_xlxmodule.empty()) {
		//m_dstid = getTg(m_srcHS);
		//LogMessage("getTG returns m_dstid %d",m_dstid);
		//if (m_dstid==0) {
			m_tgConnected=false; 
			m_dstid = m_conf.getDMRStartup();
			m_dmrpc = 0;
		// }
		// else {
		// 	m_tgConnected=true;
		// 	m_dmrpc = 0;
		// }
	}
	else {
		const char *xlxmod = m_xlxmodule.c_str();
		m_dstid = 4000 + xlxmod[0] - 64;
		m_dmrpc = 0;

		CReflector* reflector = m_xlxReflectors->findById(std::to_string(m_xlxrefl));
		if (reflector == NULL)
			return false;
		
		char tmp[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(reflector->m_address), tmp, INET_ADDRSTRLEN);
		address = std::string(tmp);
	} 

	if (pcUnlink)
		m_flcoUnlink = FLCO_USER_USER;
	else
		m_flcoUnlink = FLCO_GROUP;

	if (m_srcHS > 99999999U)
		m_defsrcid = m_srcHS / 100U;
	else if (m_srcHS > 9999999U)
		m_defsrcid = m_srcHS / 10U;
	else
		m_defsrcid = m_srcHS;

	m_srcid = m_defsrcid;
	m_enableUnlink = m_conf.getDMRNetworkEnableUnlink();
	
	LogMessage("DMR Network Parameters");
	LogMessage("    ID: %u", m_srcHS);
	LogMessage("    Default SrcID: %u", m_defsrcid);
	if (!m_xlxmodule.empty()) {
		LogMessage("    XLX Reflector: %d", m_xlxrefl);
		LogMessage("    XLX Module: %s (%d)", m_xlxmodule.c_str(), m_dstid);
	}
	else { 
		LogMessage("    Address: %s", address.c_str());
	} 
	LogMessage("    Port: %u", port);
	LogMessage("    Send %s%u Disconect: %s", pcUnlink ? "" : "TG ", m_idUnlink, (m_enableUnlink) ? "Yes":"No");
	if (local > 0U)
		LogMessage("    Local: %u", local);
	else
		LogMessage("    Local: random"); 
	LogMessage("    Jitter: %ums", jitter);

	m_dmrNetwork = new CDMRNetwork(address, port, local, m_srcHS, password, duplex, VERSION, debug, slot1, slot2, hwType, jitter);

	std::string options = m_conf.getDMRNetworkOptions();
	if (!options.empty()) {
		LogMessage("    Options: %s", options.c_str());
		m_dmrNetwork->setOptions(options);
	}

	unsigned int rxFrequency = m_conf.getRxFrequency();
	unsigned int txFrequency = m_conf.getTxFrequency();
	unsigned int power       = m_conf.getPower();
	float latitude           = m_conf.getLatitude();
	float longitude          = m_conf.getLongitude();
	int height               = m_conf.getHeight();
	std::string location     = m_conf.getLocation();
	std::string description  = m_conf.getDescription();
	std::string url          = m_conf.getURL();

	LogMessage("Info Parameters");
	LogMessage("    Callsign: %s", m_callsign.c_str());
	LogMessage("    RX Frequency: %uHz", rxFrequency);
	LogMessage("    TX Frequency: %uHz", txFrequency);
	LogMessage("    Power: %uW", power);
	LogMessage("    Latitude: %fdeg N", latitude);
	LogMessage("    Longitude: %fdeg E", longitude);
	LogMessage("    Height: %um", height);
	LogMessage("    Location: \"%s\"", location.c_str());
	LogMessage("    Description: \"%s\"", description.c_str());
	LogMessage("    URL: \"%s\"", url.c_str());

	m_dmrNetwork->setConfig(m_callsign, rxFrequency, txFrequency, power, m_colorcode, 999U, 999U, height, location, description, url);

	bool ret = m_dmrNetwork->open();
	if (!ret) {
		delete m_dmrNetwork;
		m_dmrNetwork = NULL;
		return false;
	}

	return true;
}

int CYSFGateway::getTg(int m_srcHS){
	int api_tg=0;
	int nDataLength;
	const unsigned int TIMEOUT = 10U;
	const unsigned int BUF_SIZE = 1024U;
	unsigned char *buffer;

	buffer = (unsigned char *) malloc(BUF_SIZE);
	if (!buffer) {
		LogMessage("Get_TG: Could not allocate memory.");
		return 0U;
	}

	std::string url = "/v1.0/repeater/?action=PROFILE&q=" + std::to_string(m_srcHS);
	std::string get_http = "GET " + url + " HTTP/1.1\r\nHost: api.brandmeister.network\r\nUser-Agent: YSF2DMR/0.12\r\n\r\n";

	CTCPSocket sockfd("api.brandmeister.network", 80);

	bool ret = sockfd.open();
	if (!ret){
		LogMessage("Get_TG: Could not connect to API.");
		return 0;
	}

	sockfd.write((const unsigned char*)get_http.c_str(), strlen(get_http.c_str()));
	while ((nDataLength = sockfd.read(buffer, BUF_SIZE, TIMEOUT)) > 0){
		int i,j;
		char tmp_str[20];
		for (i = 0; i < nDataLength; i++) {
			if (buffer[i] == 'o') {
				if ((i+1)>nDataLength) {nDataLength = sockfd.read(buffer, BUF_SIZE, TIMEOUT);i=0;}
				else i++;
				if (buffer[i] == 'u') {
					if ((i+1)>nDataLength) {nDataLength = sockfd.read(buffer, BUF_SIZE, TIMEOUT);i=0;}
					else i++;
					if (buffer[i] == 'p') {
						if ((i+1)>nDataLength) {nDataLength = sockfd.read(buffer, BUF_SIZE, TIMEOUT);i=0;}
						else i++;
						if (buffer[i] == '\"') {
							if ((i+1)>nDataLength) {nDataLength = sockfd.read(buffer, BUF_SIZE, TIMEOUT);i=0;}
							else i++;
							if (buffer[i] == ':') {
								i++;
								if ((i+8)>nDataLength) {
									int tmp=nDataLength-i;
									::memcpy(tmp_str, buffer + i, tmp);
									LogMessage("Reading inside");
									nDataLength = sockfd.read(buffer, BUF_SIZE, TIMEOUT);
									i=0;
									::memcpy(tmp_str+tmp, buffer, 8-tmp);
								}
								else ::memcpy(tmp_str, buffer + i, 8U);

								for (j=0;j<8;j++) if (tmp_str[j]==',') tmp_str[j]=0;
								tmp_str[8] = 0;
								api_tg=atoi(tmp_str);
								break;
							}
						}
					}
				}
			}
		}
	}

	sockfd.close();
	free(buffer);
	return api_tg;
}

void CYSFGateway::writeXLXLink(unsigned int srcId, unsigned int dstId, CDMRNetwork* network)
{
	assert(network != NULL);

	unsigned int streamId = ::rand() + 1U;

	CDMRData data;

	data.setSlotNo(XLX_SLOT);
	data.setFLCO(FLCO_USER_USER);
	data.setSrcId(srcId);
	data.setDstId(dstId);
	data.setDataType(DT_VOICE_LC_HEADER);
	data.setN(0U);
	data.setStreamId(streamId);

	unsigned char buffer[DMR_FRAME_LENGTH_BYTES];

	CDMRLC lc;
	lc.setSrcId(srcId);
	lc.setDstId(dstId);
	lc.setFLCO(FLCO_USER_USER);

	CDMRFullLC fullLC;
	fullLC.encode(lc, buffer, DT_VOICE_LC_HEADER);

	CDMRSlotType slotType;
	slotType.setColorCode(XLX_COLOR_CODE);
	slotType.setDataType(DT_VOICE_LC_HEADER);
	slotType.getData(buffer);

	CSync::addDMRDataSync(buffer, true);

	data.setData(buffer);

	for (unsigned int i = 0U; i < 3U; i++) {
		data.setSeqNo(i);
		network->write(data);
	}

	data.setDataType(DT_TERMINATOR_WITH_LC);

	fullLC.encode(lc, buffer, DT_TERMINATOR_WITH_LC);

	slotType.setDataType(DT_TERMINATOR_WITH_LC);
	slotType.getData(buffer);

	data.setData(buffer);

	for (unsigned int i = 0U; i < 2U; i++) {
		data.setSeqNo(i + 3U);
		network->write(data);
	}
}

void CYSFGateway::DMR_reconect_logic(void){
static bool first_time=true;

	if (first_time && (m_tg_type != DMR)) first_time = false;
	
	if ((m_tg_type==DMR) && first_time && m_dmrNetwork->isConnected()) {
		if (!m_tgConnected){
			if (m_srcHS>9999999U) m_srcid = m_srcHS / 100U;
			else m_srcid=m_srcHS;	
			if (m_enableUnlink) {
				LogMessage("Sending DMR Disconnect: Src: %d Dst: %s%d", m_srcid, m_flcoUnlink == FLCO_GROUP ? "TG " : "", m_idUnlink);			
				SendDummyDMR(m_srcid, m_idUnlink, m_flcoUnlink);				
				m_ptt_dstid=m_dstid;
				m_unlinkReceived = false;
				m_TG_connect_state = WAITING_UNLINK;
				m_tgConnected = true;
				m_TGChange.start();					
			} else {
				SendDummyDMR(m_srcid, m_dstid, m_dmrflco);				
			}
			m_tgConnected = true;
			LogMessage("Initial linking to TG %d.", m_dstid);

		} else {
			//LogMessage("Connecting to TG %d.", m_dstid);
			SendDummyDMR(m_srcid, m_dstid, m_dmrflco);
		}

		if (!m_xlxmodule.empty() && !m_xlxConnected) {
			writeXLXLink(m_srcid, m_dstid, m_dmrNetwork);
			LogMessage("XLX, Linking to reflector XLX%03u, module %s", m_xlxrefl, m_xlxmodule.c_str());
			m_xlxConnected = true;
		}
		first_time = false;
	}

	// TG Connection safe process at init
	// To unlink old dynamic TG
	if ((m_dmrNetwork!=NULL) && (m_tg_type==DMR)) { // && (m_tg_type==DMR)) {
		if (m_dmrNetwork->isConnected()) {
			switch (m_TG_connect_state) {
				// case WAITING_SEND_UNLINK:
				// 	if (m_not_busy) {
				// 		LogMessage("Sending DMR Disconnect: Src: %d Dst: %s%d", m_srcid, m_flcoUnlink == FLCO_GROUP ? "TG " : "", m_idUnlink);			
				// 		SendDummyDMR(m_srcid, m_idUnlink, m_flcoUnlink);
				// 		m_TGChange.start();
				// 		m_TG_connect_state = WAITING_UNLINK;
				// 	}			
				//     break;
				case WAITING_UNLINK:
					if (m_unlinkReceived) {
						LogMessage("Unlink Received");
						m_TGChange.start();
						m_TG_connect_state = SEND_REPLY;
						m_unlinkReceived = false;
					}
					break;
				case SEND_REPLY:
					if (m_TGChange.elapsed() > 600) {
						m_TGChange.start();
						m_TG_connect_state = SEND_PTT;
						m_wiresX->SendCReply();
					//	m_tgConnected = false;
					}
					break;
				case SEND_PTT:
					if (m_not_busy && !m_wiresX->isBusy()) { //} && m_TGChange.elapsed() > 600) {
						m_TGChange.start();
						m_lostTimer.start();
						m_TG_connect_state = TG_NONE;
						if (m_ptt_dstid) {
							LogMessage("Sending PTT: Src: %s Dst: %s%d", m_callsign.c_str(), m_ptt_pc ? "" : "TG ", m_ptt_dstid);
							SendDummyDMR(m_srcid, m_ptt_dstid, m_ptt_pc ? FLCO_USER_USER : FLCO_GROUP);
						}
//						m_not_busy=true;
					}
					break;
				default: 
					break;
			}

			if ((m_TG_connect_state != TG_NONE) && (m_TGChange.elapsed() > 12000)) {
				LogMessage("Timeout changing TG");
				m_TG_connect_state = TG_NONE;
				m_wiresX->SendDReply();				
				m_not_busy=true;
				m_lostTimer.start();
			} 
		} 
	} 
}

void CYSFGateway::DMR_send_Network(void) {
static unsigned int m_fill=0;
static unsigned int m_actual_step = 0;
static bool sending_silence=false;
	
	if ((m_tg_type==DMR) && (m_dmrNetwork!=NULL) && (m_dmrWatch.elapsed() > DMR_FRAME_PER)) {			
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


void CYSFGateway::DMR_get_Modem(unsigned int ms) {
	CDMRData tx_dmrdata;	

		while (m_dmrNetwork->read(tx_dmrdata) > 0U)  {
			if (((m_TG_connect_state==TG_NONE) || (m_TG_connect_state==WAITING_UNLINK)) && (m_tg_type==DMR) && !m_wiresX->isBusy()) {
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
							break;
						}
						
						if (!m_unlinkReceived && ((SrcId == 4000U) || (SrcId==m_dstid)))
							m_unlinkReceived = true;

						LogMessage("DMR received end of voice transmission, %.1f seconds", float(m_dmrFrames) / 16.667F);
						m_conv.putDMREOT(true);
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
							else m_netDst = "UNKNOW";
						}
						m_conv.putDMRHeader();
						LogMessage("DMR audio received from %s to %s", m_rcv_callsign.c_str(), m_netDst.c_str());
						m_dmrinfo = true;
						m_rcv_callsign.resize(YSF_CALLSIGN_LENGTH, ' ');
						m_netDst.resize(YSF_CALLSIGN_LENGTH, ' ');
						m_dmrFrames = 0U;
						m_firstSync = false;
						m_open_channel=true;
						m_not_busy = false;
					}

					if(DataType == DT_VOICE_SYNC)
						m_firstSync = true;

					if((DataType == DT_VOICE_SYNC || DataType == DT_VOICE) && m_firstSync) {
						unsigned char dmr_frame[50];

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
								else m_netDst = "UNKNOW";
							}
							LogMessage("DMR audio late entry received from %s to %s", m_rcv_callsign.c_str(), m_netDst.c_str());
							m_rcv_callsign.resize(YSF_CALLSIGN_LENGTH, ' ');
							m_netDst.resize(YSF_CALLSIGN_LENGTH, ' ');
							m_dmrinfo = true;
							m_open_channel=true;
						}

						m_conv.putDMR(dmr_frame); // Add DMR frame for YSF conversion
						m_not_busy = false;
						m_dmrFrames++;
					}
				}
				else {
					if(DataType == DT_VOICE_SYNC || DataType == DT_VOICE) {
						unsigned char dmr_frame[50];
						tx_dmrdata.getData(dmr_frame);
						m_conv.putDMR(dmr_frame); // Add DMR frame for YSF conversion
						m_dmrFrames++;
						if (m_open_channel == false) m_open_channel=true;
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
			m_lostTimer.start();
			}
		}		
}
