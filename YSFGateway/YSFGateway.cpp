/*
*   Copyright (C) 2016-2019 by Jonathan Naylor G4KLX
*   Copyright (C) 2018,2019 by Andy Uribe CA6JAU
*   Copyright (C) 2019,2020 by Manuel Sanchez EA7EE
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

const char* HEADER1 = "This software is for use on amateur radio networks only,";
const char* HEADER2 = "it is to be used for educational purposes only. Its use on";
const char* HEADER3 = "commercial networks is strictly prohibited.";
const char* HEADER4 = "Copyright(C) 2018,2019 by CA6JAU, EA7EE, G4KLX and others";
char const *text_type[6] = {"NONE","YSF ","FCS ","DMR ","NXDN","P25 "};

bool first_time_DMR = true;
bool first_time_YSF = true;

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
m_conf(configFile),
m_ysfReflectors(NULL),
m_fcsReflectors(NULL),
m_dmrReflectors(NULL),
m_nxdnReflectors(NULL),
m_p25Reflectors(NULL),
m_actual_ref(NULL),
m_wiresX(NULL),
m_ptt_pc(false),
m_ptt_dstid(1U),
m_ysfNetwork(NULL),
m_fcsNetwork(NULL),
m_dmrNetwork(NULL),
m_current(),
m_startup(),
m_inactivityTimer(NULL),
m_lostTimer(1000U, 0U, 120U),
m_fcsNetworkEnabled(false),
m_dmrNetworkEnabled(false),
m_idUnlink(4000U),
m_flcoUnlink(FLCO_GROUP),
m_NoChange(false),
m_tg_type(DMR),
m_last_DMR_TG(0U),
m_last_YSF_TG(0U),
m_last_FCS_TG(0U),
m_last_P25_TG(0U),
m_enableUnlink(false),
m_parrotAddress(),
m_parrotPort(),
m_ysf2nxdnAddress(),
m_ysf2nxdnPort(),
m_ysf2p25Address(),
m_ysf2p25Port(),
m_tgConnected(false),
m_TG_connect_state(TG_NONE),
m_xlxmodule(),
m_xlxConnected(false),
m_xlxReflectors(NULL),
m_xlxrefl(0U),
m_remoteSocket(NULL),
m_Streamer(NULL)
{

}

CYSFGateway::~CYSFGateway()
{

}

int get_ysfid(std::string name) {
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

	return hash % 100000U;
}

bool first_time_ysf_dgid = true;

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
	
	std::string m_callsign = m_conf.getCallsign();
	m_Streamer = new CStreamer(&m_conf);
	
	bool debug            = m_conf.getNetworkDebug();
	in_addr rptAddress    = CUDPSocket::lookup(m_conf.getRptAddress());
	unsigned int rptPort  = m_conf.getRptPort();
	std::string myAddress = m_conf.getMyAddress();
	unsigned int myPort   = m_conf.getMyPort();
	m_NoChange			  = m_conf.getNetworkNoChange();
	

	// Get timeout and beacon times from Conf.cpp
	unsigned int m_timeout_time = m_conf.getNetworkInactivityTimeout();

	unsigned int reloadTime = m_conf.getNetworkReloadTime();
	bool wiresXMakeUpper = m_conf.getWiresXMakeUpper();

	ysfNetworkEnabled = m_conf.getYSFNetworkEnabled();
	m_fcsNetworkEnabled = m_conf.getFCSNetworkEnabled();
	m_dmrNetworkEnabled = m_conf.getDMRNetworkEnabled();
	m_nxdnNetworkEnabled = m_conf.getNXDNNetworkEnabled();
	m_p25NetworkEnabled = m_conf.getP25NetworkEnabled();
	
	m_startup = m_conf.getNetworkStartup();
	m_last_DMR_TG = m_conf.getDMRStartup();
	m_last_YSF_TG = m_conf.getYSFStartup();
	m_last_FCS_TG = m_conf.getFCSStartup();
	m_last_NXDN_TG = m_conf.getNXDNStartup();
	m_last_P25_TG = m_conf.getP25Startup();
	m_DGID = m_conf.getStartupDGID();
	
	m_tg_type = (enum TG_TYPE) m_conf.getNetworkTypeStartup();
	m_Streamer->put_tgType(m_tg_type);
	m_ysfoptions = m_conf.getYSFNetworkOptions();
	m_fcsoptions = m_conf.getFCSNetworkOptions();

	LogInfo("General Parameters");
	//unsigned int m_original = atoi(m_startup.c_str());
	LogInfo("    Startup TG: %s", m_startup.c_str());
    LogInfo("    Startup Network Type: %s",	text_type[m_conf.getNetworkTypeStartup()]);
	LogInfo("    Timeout TG Time: %d min", m_timeout_time);
    LogInfo("    TG List Reload Time: %d min", reloadTime);
	LogInfo("    Make Upper: %s", wiresXMakeUpper ? "yes" : "no");
	LogInfo("    No Change option: %s", m_NoChange ? "yes" : "no");
	LogInfo("    YSF Enabled: %s", ysfNetworkEnabled ? "yes" : "no");
	LogInfo("    FCS Enabled: %s", m_fcsNetworkEnabled ? "yes" : "no");
	LogInfo("    DMR Enabled: %s", m_dmrNetworkEnabled ? "yes" : "no");
	LogInfo("    NXDN Enabled: %s", m_nxdnNetworkEnabled ? "yes" : "no");
	LogInfo("    P25 Enabled: %s", m_p25NetworkEnabled ? "yes" : "no");			
	LogInfo("    YSF Startup: %d", m_last_YSF_TG);
	LogInfo("    YSF Startup DG-ID: %d", m_DGID);
	LogInfo("    FCS Startup: %d", m_last_FCS_TG);
	LogInfo("    DMR Startup: %d", m_last_DMR_TG);
	LogInfo("    NXDN Startup: %d", m_last_NXDN_TG);			
	LogInfo("    P25 Startup: %d", m_last_P25_TG);	
	LogInfo("    YSF Options: %s", m_ysfoptions.c_str());			
	LogInfo("    FCS Options: %s", m_fcsoptions.c_str());	
	LogInfo("    Jitter: %d mseg", m_conf.getJitter()*100);

	unsigned int txFrequency = m_conf.getTxFrequency();
	unsigned int rxFrequency = m_conf.getRxFrequency();
	std::string locator = calculateLocator();
	//unsigned int id = m_conf.getId();

	unsigned int ysf_id = get_ysfid(m_callsign);

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

		m_ysfNetwork = new CYSFNetwork(ysfPort,  m_callsign, rxFrequency, txFrequency, locator, m_conf.getLocation(), ysf_id, debug);			
		ret = m_ysfNetwork->open();
		if (!ret) {
			::LogError("Cannot open the YSF reflector network port");
			::LogFinalise();
			return 1;
		}
	}

	if (m_fcsNetworkEnabled) {
		unsigned int fcsPort = m_conf.getFCSNetworkPort();

		m_fcsNetwork = new CFCSNetwork(fcsPort, m_callsign, rxFrequency, txFrequency, locator, m_conf.getLocation(), ysf_id, debug);
		ret = m_fcsNetwork->open();
		if (!ret) {
			::LogError("Cannot open the FCS reflector network port");
			::LogFinalise();
			return 1;
		}
	}
	
	if (m_dmrNetworkEnabled) {
		LogMessage("Opening DMR network connection");
		ret = createDMRNetwork(m_callsign);
		if (!ret) {
			::LogError("Cannot open DMR Network");
			::LogFinalise();
			return 1;
		}	
	}
	
	if (m_timeout_time>0) {
		m_inactivityTimer = new CTimer(1000U,m_timeout_time * 60U);
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
	LogInfo("    DMR/DMR+ List: %s", file_dmr.c_str());
	LogInfo("    NXDN List: %s", file_nxdn.c_str());
	LogInfo("    P25 List: %s", file_p25.c_str());
	
	m_ysfReflectors = new CReflectors(file_ysf, YSF, reloadTime, wiresXMakeUpper);
	m_fcsReflectors = new CReflectors(file_fcs, FCS, reloadTime, wiresXMakeUpper);
	if (m_conf.getDMRNetworkEnableUnlink()) m_dmrReflectors = new CReflectors(file_dmr, DMR, reloadTime, wiresXMakeUpper);
	else m_dmrReflectors = new CReflectors(file_dmr, DMRP, reloadTime, wiresXMakeUpper);
	m_nxdnReflectors = new CReflectors(file_nxdn, NXDN, reloadTime, wiresXMakeUpper);
	m_p25Reflectors = new CReflectors(file_p25, P25, reloadTime, wiresXMakeUpper);
	
	std::string fileName    = m_conf.getDMRXLXFile();
	if (!fileName.empty()) {
		m_xlxReflectors = new CReflectors(fileName, DMR, reloadTime, wiresXMakeUpper);
		m_xlxReflectors->load();
	}

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

	// m_ysfReflectors->reload();
	// m_fcsReflectors->reload();
	// m_dmrReflectors->reload();
	// m_nxdnReflectors->reload();
	// m_p25Reflectors->reload();

	if (m_conf.getRemoteCommandsEnabled()) {
		m_remoteSocket = new CUDPSocket(m_conf.getRemoteCommandsPort());
		ret = m_remoteSocket->open();
		if (!ret) {
			delete m_remoteSocket;
			m_remoteSocket = NULL;
		}
	}
//	LogMessage("Before create WiresX");
	m_wiresX = m_Streamer->createWiresX(&rptNetwork, m_conf.getWiresXMakeUpper(), m_callsign, m_conf.getLocation());
//	LogMessage("After create WiresX");	
	m_Streamer->createGPS(m_callsign);	
	m_Streamer->setBeacon(m_inactivityTimer,&m_lostTimer, m_NoChange, m_DGID);	
	m_Streamer->Init(&rptNetwork, m_ysfNetwork, m_fcsNetwork, m_dmrNetwork, m_dmrReflectors);

	if (startupLinking()==false) {
		LogMessage("Cannot conect to startup reflector. Exiting...");
		rptNetwork.close();

		if (m_ysfNetwork != NULL) {
			m_ysfNetwork->close();
			delete m_ysfNetwork;
		}

		if (m_fcsNetwork != NULL) {
			m_fcsNetwork->close();
			delete m_fcsNetwork;
		}
		
		if (m_xlxReflectors != NULL)
			delete m_xlxReflectors;	

		delete m_Streamer;
		
		::LogFinalise();

		return 0;
	};
	
	m_stopWatch.start();
	m_dgid_timer.start();

	LogMessage("Starting YSFGateway-%s", VERSION);

	m_TG_connect_state = TG_NONE;
	unsigned int ms=0;


	
	for (;end == 0;) {

		// DMR connect logic
		if (m_dmrNetworkEnabled) DMR_reconect_logic();

		// Remote Processing
		if (m_remoteSocket != NULL)
			processRemoteCommands();		

		ms = m_stopWatch.elapsed();
		m_stopWatch.start();

		// Newtowrk data input/output
		m_Streamer->clock(m_TG_connect_state, ms);	

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

		if (m_inactivityTimer!=NULL) {
			m_inactivityTimer->clock(ms);
			if (m_inactivityTimer->isRunning() && m_inactivityTimer->hasExpired()) {
				LogMessage("Inactivity Timer Fired.");				
				if (m_original != m_current_num) {			
					m_lostTimer.stop();
					startupReLinking();
					m_lostTimer.start();
				} 
				if (m_TG_connect_state == TG_DISABLE) m_TG_connect_state = TG_NONE;
				if (m_ysfNetwork!= NULL && (m_tg_type==YSF)) {
					m_ysfNetwork->id_query_response();
					first_time_ysf_dgid = true;
					m_dgid_timer.start();
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
		if ((m_ysfNetwork != NULL) && first_time_ysf_dgid && (m_tg_type == YSF)) {
			if (m_ysfNetwork->id_getresponse() || (m_dgid_timer.elapsed() > 4000U)) {
				m_Streamer->SendDummyYSF(m_ysfNetwork,m_DGID);
				first_time_ysf_dgid = false;
			}
		}

		if (ms < 5U)
			CThread::sleep(5U);

		// Change TG
		WX_STATUS state = m_Streamer->change_TG();
		if (state == WXS_CONNECT) {
			//LogMessage("m_srcid: %d",m_srcid);
			if (m_NoChange) {
				LogMessage("Not allow to connect to other reflectors.");
				//m_wiresX->SendDReply();
			} else {	
				unsigned int tmp_dstid = m_Streamer->get_dstid(); 
				m_TG_connect_state = TG_NONE;
				//unsigned int tmp_srcid = m_Streamer->get_srcid(); 	
				//LogMessage("m_srcid: %d, m_dstid",tmp_srcid, tmp_dstid);	
				int ret = TG_Connect(tmp_dstid);
				if (ret) {
					LogMessage("Connected to %05d - \"%s\" has been requested by %10.10s", tmp_dstid, m_Streamer->getNetDst().c_str(), m_Streamer->get_ysfcallsign().c_str());
					if ((m_tg_type == YSF) || (m_tg_type == FCS))m_wiresX->SendCReply();
				} 
				// else {
				// 	LogMessage("Error with connect");
				// 	m_wiresX->SendDReply();
				// }	
			}				
		} else if (state == WXS_DISCONNECT && (m_NoChange == false)) {
			if (m_TG_connect_state == TG_DISABLE) {
				m_TG_connect_state = TG_NONE;
				m_wiresX->SendCReply();
			}
			else {
				m_TG_connect_state = TG_DISABLE;
				m_wiresX->SendDReply();
			}
		}

	}

	rptNetwork.close();

	if (m_ysfNetwork != NULL) {
		m_ysfNetwork->close();
		delete m_ysfNetwork;
	}

	if (m_fcsNetwork != NULL) {
		m_fcsNetwork->close();
		delete m_fcsNetwork;
	}

	if (m_remoteSocket != NULL) {
		m_remoteSocket->close();
		delete m_remoteSocket;
	}
	
	if (m_xlxReflectors != NULL)
		delete m_xlxReflectors;	

	delete m_Streamer;
//	delete m_storage;
	
	::LogFinalise();

	return 0;
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


void CYSFGateway::startupReLinking()
{
	int tmp_id = m_conf.getNetworkTypeStartup();
	if (m_tg_type != tmp_id) {
		switch (tmp_id)
		{
		case YSF:
			TG_Connect(2);
			if (m_original != m_last_YSF_TG) 
				TG_Connect(m_original);
			break;
		case FCS:
			TG_Connect(3);
			if (m_original != m_last_FCS_TG)
				TG_Connect(m_original);
			break;
		case DMR:
			TG_Connect(4);
			if (m_original != m_last_DMR_TG)
				TG_Connect(m_original);
			break;
		case P25:
			TG_Connect(5);
			if (m_original != m_last_P25_TG)
				TG_Connect(m_original);			
			break;
		case NXDN:
			TG_Connect(6);
			if (m_original != m_last_NXDN_TG)
				TG_Connect(m_original);				
			break;
		default:
			break;
		}
	} else
		TG_Connect(m_original);
	m_wiresX->SendCReply();
}

bool CYSFGateway::startupLinking()
{
	int tmp_id = m_conf.getNetworkTypeStartup();
	CReflector* reflector;
	unsigned int dstId;

	LogMessage("Entrado en startup: %d",tmp_id);

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
	LogMessage("Antes de Fijar reflectores en startup");
	m_wiresX->setReflectors(m_actual_ref);
	LogMessage("Fijado reflectores en startup");
	m_tg_type = (TG_TYPE) tmp_id;
	m_Streamer->put_tgType(m_tg_type);

	if ((m_tg_type == DMR) && !m_xlxmodule.empty() && !m_xlxConnected) {
		writeXLXLink(m_Streamer->get_srcid(), m_Streamer->get_dstid(), m_dmrNetwork);
		LogMessage("XLX, Linking to reflector XLX%03u, module %s", m_xlxrefl, m_xlxmodule.c_str());
		m_xlxConnected = true;
	} else {
		if ((m_tg_type == DMR)) { // && m_ysf_callsign.empty()) {
			m_wiresX->setReflectors(m_dmrReflectors);
			dstId = m_Streamer->get_dstid();
			reflector = m_dmrReflectors->findById(std::to_string(dstId));
			if (reflector != NULL) {
				m_current = reflector->m_name;
				m_current.resize(YSF_CALLSIGN_LENGTH, ' ');
				LogMessage("DMR TG: %s",m_current.c_str());
				m_wiresX->setReflector(reflector->m_name, dstId,NULL);
			} else {
				m_current = std::string("TG") + std::to_string(dstId);
				m_current.resize(YSF_CALLSIGN_LENGTH, ' ');
				LogMessage("DMR TG: %s",m_current.c_str());
				m_wiresX->setReflector("", dstId,NULL);
			}
			m_last_DMR_TG = dstId; 
			m_dmrNetwork->enable(true);
		} else {

			LogMessage("listo para buscar reflector %s en startup",m_startup.c_str());
			if (is_number(m_startup)) {
				m_startup.erase(0, m_startup.find_first_not_of('0'));
				reflector = m_actual_ref->findById(m_startup);
			}
			else reflector = m_actual_ref->findByName(m_startup);
			if (reflector != NULL) {
				LogMessage("Encontrado reflector %s en startup",m_startup.c_str());
				dstId = atoi(reflector->m_id.c_str());
			}
			else {
				LogMessage("Unknown reflector - %s", m_startup.c_str());
				return false;
			}
		}
			LogMessage("Antes de llamar a TGCOnnect en startup");
		if (TG_Connect(dstId)) {
			LogMessage("Automatic (re-)connection to %5.5s - \"%s\"", reflector->m_id.c_str(), reflector->m_name.c_str());
			m_original = dstId;
			return true;
		} else LogMessage("Not Possible connection - %d", atoi(reflector->m_id.c_str()));
	}
	return false;
}

bool CYSFGateway::TG_Connect(unsigned int dstID) {
	char tmp[20];
	CReflector* reflector;
	std::string dst_str_ID = std::to_string(dstID);
    TG_TYPE last_type=m_tg_type;
	int tglistOpt,i;

	if (dstID < 6) {
		dstID--;
		if (dstID==0){
			if (!ysfNetworkEnabled) return false;
			dstID=1;
			m_tg_type=YSF;
			m_Streamer->put_tgType(m_tg_type);		
		} else if (dstID == DMR) {
			if (!m_dmrNetworkEnabled) return false;
			//if (dstID != last_type) m_conv.reset()
			dstID=m_last_DMR_TG;
			m_tg_type=DMR;
			m_Streamer->put_tgType(m_tg_type);
		} else if (dstID == YSF) {
			if (!ysfNetworkEnabled) return false;
			if (last_type == DMR) {
				m_dmrNetwork->enable(false);					
				//m_conv.reset();
			}		
			dstID=m_last_YSF_TG;
			m_tg_type=YSF;
			m_Streamer->put_tgType(m_tg_type);		
		} else if (dstID == FCS) {
			if (!m_fcsNetworkEnabled) return false;
			if (last_type == DMR) {
				m_dmrNetwork->enable(false);					
				//m_conv.reset();	
			}
			dstID=m_last_FCS_TG;
			m_tg_type=FCS;
			m_Streamer->put_tgType(m_tg_type);		
		} else if (dstID == NXDN) {
			if (!m_nxdnNetworkEnabled) return false;
			if (last_type == DMR) {
				m_dmrNetwork->enable(false);					
				//m_conv.reset();	
			}
			m_ysfNetwork->setDestination("YSF2NXDN", m_ysf2nxdnAddress, m_ysf2nxdnPort);
			m_ysfNetwork->writePoll(3U);						
			dstID=m_last_NXDN_TG;
			m_tg_type=NXDN;
			m_Streamer->put_tgType(m_tg_type);
		} else if (dstID == P25) {
			if (!m_p25NetworkEnabled) return false;
			if (last_type == DMR) {
				m_dmrNetwork->enable(false);					
			//	m_conv.reset();			
			}				
			m_ysfNetwork->setDestination("YSF2P25", m_ysf2p25Address, m_ysf2p25Port);
			m_ysfNetwork->writePoll(3U);					
			dstID=m_last_P25_TG;
			m_tg_type=P25;
			m_Streamer->put_tgType(m_tg_type);
		}
	}

	// Don´t try to reconnect to mode
	if ((dstID<6) && (dstID>1)) return false;
	dst_str_ID = std::to_string(dstID);
	switch (m_tg_type) {
		case NONE:
			LogMessage("Error startup Type not defined.");
			break;		
		case YSF: {
		    if (dstID==1) {
				if ((last_type == YSF) && (m_ysfNetwork != NULL) ) {
					m_ysfNetwork->writeUnlink(3U);
					m_ysfNetwork->clearDestination();
				}			
				else if ((last_type == FCS) && (m_ysfNetwork != NULL))  {
					m_fcsNetwork->writeUnlink(3U);
					m_fcsNetwork->clearDestination();
				}
				reflector = m_ysfReflectors->findById(dst_str_ID);
				LogMessage("Trying PARROT");					
				m_lostTimer.stop();
				m_current = "PARROT";
				m_current_num = 1;
				m_current.resize(YSF_CALLSIGN_LENGTH, ' ');				
				m_ysfNetwork->setDestination(m_current, m_parrotAddress, m_parrotPort);
				m_ysfNetwork->writePoll(3U);
				m_lostTimer.start();					
				m_tg_type = YSF;
				m_Streamer->put_tgType(m_tg_type);						
				m_Streamer->put_dstid(dstID);
				m_wiresX->setReflectors(m_ysfReflectors);					
				m_wiresX->setReflector(reflector->m_name, m_Streamer->get_dstid(),NULL);	

			} else if (m_ysfNetwork != NULL) {
				m_lostTimer.stop();

				reflector = m_ysfReflectors->findById(dst_str_ID);
				if (reflector != NULL) {
					// Close connection
					if ((last_type == YSF) && (m_ysfNetwork != NULL) ) {
						m_ysfNetwork->writeUnlink(3U);
						m_ysfNetwork->clearDestination();
					} else if ((last_type == FCS) && (m_ysfNetwork != NULL))  {
						m_fcsNetwork->writeUnlink(3U);
						m_fcsNetwork->clearDestination();
					}						 
					m_ysfNetwork->setDestination(reflector->m_name, reflector->m_address, reflector->m_port);
					if (!m_ysfoptions.empty()) m_ysfNetwork->setOptions(m_ysfoptions);					
					m_ysfNetwork->writePoll(3U);
					m_current_num = atoi(reflector->m_id.c_str());
					m_current = reflector->m_name;
					m_current.resize(YSF_CALLSIGN_LENGTH, ' ');	
					m_lostTimer.start();					
					m_tg_type = YSF;
					m_Streamer->put_tgType(m_tg_type);						
					m_Streamer->put_dstid(dstID);
					m_wiresX->setReflectors(m_ysfReflectors);					
					m_wiresX->setReflector(reflector->m_name, dstID, m_ysfNetwork);
					m_last_YSF_TG = dstID;
					first_time_ysf_dgid = true;
					m_dgid_timer.start();
					} else return false;
				}  else return false;
			}
			break;
		case FCS:		
			if (m_fcsNetwork != NULL) {
				m_lostTimer.stop();				
				LogMessage("Conecting to FCS  %s.",dst_str_ID.c_str());
				reflector = m_fcsReflectors->findById(dst_str_ID);
				if (reflector != NULL) {
					// Close connection
					if ((last_type == YSF) && (m_ysfNetwork != NULL) ) {
						m_ysfNetwork->writeUnlink(3U);
						m_ysfNetwork->clearDestination();
					} else if ((last_type == FCS) && (m_ysfNetwork != NULL))  {
						m_fcsNetwork->writeUnlink(3U);
						m_fcsNetwork->clearDestination();
					}	
					sprintf(tmp,"FCS%05d",atoi(reflector->m_id.c_str()));
					LogMessage("FCS Reflector: %s",tmp);				
					bool ok = m_fcsNetwork->writeLink(std::string(tmp));
					m_fcsNetwork->setOptions(m_fcsoptions);	
					if (ok) {
						m_current = reflector->m_name;
						m_current.resize(YSF_CALLSIGN_LENGTH, ' ');	
						m_lostTimer.start();
						m_tg_type = FCS;
						m_Streamer->put_tgType(m_tg_type);						
						m_Streamer->put_dstid(dstID);
						m_last_FCS_TG = dstID;						
						m_wiresX->setReflectors(m_fcsReflectors);
						m_wiresX->setReflector(reflector->m_name, dstID,NULL);
						m_current_num = atoi(reflector->m_id.c_str());
						m_last_FCS_TG = dstID;
					} else return false;							
				} else return false;					
			}
			break;
		case DMR:
		case DMRP:
			m_dmrNetwork->enable(true);
			reflector = m_dmrReflectors->findById(dst_str_ID);
			if (reflector == NULL) {
				for (i=1;i<10;i++) {
					dst_str_ID = std::to_string((i*100000U)+dstID);
					LogMessage("Trying %s",dst_str_ID.c_str());
					reflector = m_dmrReflectors->findById(dst_str_ID);
					if (reflector != NULL) {
						dstID = atoi(dst_str_ID.c_str());
						break;
					}
				}
			}
			if (reflector == NULL) {
				if (m_enableUnlink) tglistOpt = 0;
				else tglistOpt = 1;
			} else tglistOpt = reflector->m_opt;
		
			// if (reflector != NULL) LogMessage("DMR connection to %s", reflector->m_name.c_str());
			// else LogMessage("DMR connection to TG%d", dstID);

			switch (tglistOpt) {
				case 0:
					m_ptt_pc = false;
					m_Streamer->put_dstid(dstID);
					m_ptt_dstid = m_Streamer->get_dstid();
					m_Streamer->put_dmrflco(FLCO_GROUP);
					//LogMessage("Connect to TG %d has been requested", m_Streamer->get_dstid());
					break;
			
				case 1:
					m_ptt_pc = true;
					m_Streamer->put_dstid(9U);
					m_ptt_dstid = dstID;
					m_Streamer->put_dmrflco(FLCO_GROUP);
					//LogMessage("Connect to REF %d has been requested", m_ptt_dstid);
					break;
				
				case 2:
					m_ptt_dstid = 0;
					m_ptt_pc = true;
					m_Streamer->put_dstid(dstID);
					m_Streamer->put_dmrflco(FLCO_USER_USER);
					//LogMessage("Connect to %d has been requested", m_Streamer->get_dstid());
					break;
			
				default:
					m_ptt_pc = false;
					m_Streamer->put_dstid(dstID);
					m_ptt_dstid = m_Streamer->get_dstid();
					m_Streamer->put_dmrflco(FLCO_GROUP);
					//LogMessage("Connect to TG %d has been requested", m_Streamer->get_dstid());
					break;
			}

			if ((dstID != m_last_DMR_TG) || first_time_DMR) {	
				if (m_enableUnlink && (m_ptt_dstid != m_idUnlink) && (m_ptt_dstid != 5000)) {
						//m_not_busy=false;
						LogMessage("Sending DMR Disconnect: Src: %d Dst: %s%d", m_Streamer->get_srcid(), m_flcoUnlink == FLCO_GROUP ? "TG " : "", m_idUnlink);

						m_Streamer->SendDummyDMR(m_Streamer->get_srcid(), m_idUnlink, m_flcoUnlink);

					//m_unlinkReceived = false;
					m_TG_connect_state = WAITING_UNLINK;
				} else 
					m_TG_connect_state = SEND_REPLY;
				m_TGChange.start();	
			} else {
				m_TG_connect_state = SEND_REPLY;
				m_TGChange.start();					
			}

			if (reflector == NULL) {
				m_current = std::string("TG") + std::to_string(dstID);
				m_current_num = dstID;
			} else {
				m_current = reflector->m_name;
				m_current_num = atoi(reflector->m_id.c_str());
			}
			m_current.resize(YSF_CALLSIGN_LENGTH, ' ');	
			m_tg_type = DMR;
			m_Streamer->put_tgType(m_tg_type);				
			if (reflector == NULL) m_wiresX->setReflector("", dstID,NULL);
			else m_wiresX->setReflector(reflector->m_name, dstID,NULL);
			m_wiresX->setReflectors(m_dmrReflectors);
			// if (dstID == m_last_DMR_TG && !first_time_DMR) {
			// 	m_wiresX->SendCReply();
			// }
			if (first_time_DMR) first_time_DMR = false;
			m_last_DMR_TG = dstID;
			break;
		case NXDN:
			reflector = m_nxdnReflectors->findById(dst_str_ID);
			if (reflector != NULL) {		
				LogMessage("NXDN connection to %s", reflector->m_name.c_str());				

				m_current.assign(reflector->m_name);
				m_current.resize(YSF_CALLSIGN_LENGTH, ' ');	
				m_lostTimer.start();
				m_tg_type = NXDN;
				m_Streamer->put_tgType(m_tg_type);				
				m_Streamer->put_dstid(dstID);
				m_last_NXDN_TG = dstID;
				m_wiresX->setReflector(reflector->m_name, m_Streamer->get_dstid(),NULL);
				m_wiresX->setReflectors(m_nxdnReflectors);
				m_wiresX->SendCReply();				
				//m_wiresX->SendRConnect(m_ysfNetwork);
				m_current_num = atoi(reflector->m_id.c_str());
				m_last_NXDN_TG = dstID;
			} else return false;
			break;
		case P25:
			reflector = m_p25Reflectors->findById(dst_str_ID);
			if (reflector != NULL) {		
				LogMessage("P25 connection to %s", reflector->m_name.c_str());				

				m_current.assign(reflector->m_name);
				m_current.resize(YSF_CALLSIGN_LENGTH, ' ');	
				m_lostTimer.start();
				m_tg_type = P25;
				m_Streamer->put_tgType(m_tg_type);				
				m_Streamer->put_dstid(dstID);
				m_last_P25_TG = dstID;
				m_wiresX->setReflector(reflector->m_name, m_Streamer->get_dstid(),NULL);
				m_wiresX->setReflectors(m_p25Reflectors);
				m_wiresX->SendCReply();
				//m_wiresX->SendRConnect(m_ysfNetwork);
				m_current_num = atoi(reflector->m_id.c_str());
				m_last_P25_TG = dstID;
			} else return false;
			break;
	}
	
	m_current.resize(YSF_CALLSIGN_LENGTH, ' ');
	m_Streamer->putNetDst(m_current);
	
	return true;
}

bool CYSFGateway::createDMRNetwork(std::string callsign)
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
	unsigned int 		m_defsrcid;
	bool 				m_dmrpc;
	
	if (address.empty()) return false;
	unsigned int jitter=500U;
	if (port==0) port = 62031U;
    if (local==0) local = 62032U;
	if (password.empty()) return false;
	
	unsigned int m_srcHS = m_conf.getId();
	unsigned int m_colorcode = 1U;
	m_idUnlink = m_conf.getDMRNetworkIDUnlink();
	bool pcUnlink = m_conf.getDMRNetworkPCUnlink();

	if (m_xlxmodule.empty()) {
		//m_dstid = getTg(m_srcHS);
		//LogMessage("getTG returns m_dstid %d",m_dstid);
		//if (m_dstid==0) {
			m_tgConnected=false; 
			m_Streamer->put_dstid(m_conf.getDMRStartup());
			m_dmrpc = 0;
		// }
		// else {
		// 	m_tgConnected=true;
		// 	m_dmrpc = 0;
		// }
	}
	else {
		const char *xlxmod = m_xlxmodule.c_str();
		m_Streamer->put_dstid(4000 + xlxmod[0] - 64);
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

	//m_srcid = m_defsrcid;
	m_enableUnlink = m_conf.getDMRNetworkEnableUnlink();
	
	LogMessage("DMR Network Parameters");
	LogMessage("    ID: %u", m_srcHS);
	LogMessage("    Default SrcID: %u", m_defsrcid);
	if (!m_xlxmodule.empty()) {
		LogMessage("    XLX Reflector: %d", m_xlxrefl);
		LogMessage("    XLX Module: %s (%d)", m_xlxmodule.c_str(), m_Streamer->get_dstid());
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
	LogMessage("    Callsign: %s", callsign.c_str());
	LogMessage("    RX Frequency: %uHz", rxFrequency);
	LogMessage("    TX Frequency: %uHz", txFrequency);
	LogMessage("    Power: %uW", power);
	LogMessage("    Latitude: %fdeg N", latitude);
	LogMessage("    Longitude: %fdeg E", longitude);
	LogMessage("    Height: %um", height);
	LogMessage("    Location: \"%s\"", location.c_str());
	LogMessage("    Description: \"%s\"", description.c_str());
	LogMessage("    URL: \"%s\"", url.c_str());

	m_dmrNetwork->setConfig(callsign, rxFrequency, txFrequency, power, m_colorcode, 999U, 999U, height, location, description, url);

	bool ret = m_dmrNetwork->open();
	if (!ret) {
		delete m_dmrNetwork;
		m_dmrNetwork = NULL;
		return false;
	}

	if (m_dmrpc)
		m_Streamer->put_dmrflco(FLCO_USER_USER);
	else
		m_Streamer->put_dmrflco(FLCO_GROUP);

	return true;
}

int CYSFGateway::getTg(int srcHS){
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

	std::string url = "/v1.0/repeater/?action=PROFILE&q=" + std::to_string(srcHS);
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
unsigned int tmp_srcid;

	tmp_srcid = m_conf.getId();
	if (tmp_srcid>9999999U) tmp_srcid = tmp_srcid / 100U;

	if (first_time && (m_tg_type != DMR)) first_time = false;
	
	if ((m_tg_type==DMR) && first_time && m_dmrNetwork->isConnected()) {
		if (!m_tgConnected){

			if (m_enableUnlink) {
				LogMessage("Sending DMR Disconnect: Src: %d Dst: %s%d", tmp_srcid, m_flcoUnlink == FLCO_GROUP ? "TG " : "", m_idUnlink);			
				m_Streamer->SendDummyDMR(tmp_srcid, m_idUnlink, m_flcoUnlink);				
				m_ptt_dstid=m_Streamer->get_dstid();
				//m_unlinkReceived = false;
				m_TG_connect_state = WAITING_UNLINK;
				m_tgConnected = true;
				m_TGChange.start();					
			} else {
				m_Streamer->SendDummyDMR(tmp_srcid, m_Streamer->get_dstid(), m_Streamer->get_dmrflco());				
			}
			m_tgConnected = true;
			LogMessage("Initial linking to TG %d.", m_Streamer->get_dstid());

		} else {
			//LogMessage("Connecting to TG %d.", m_dstid);
			m_Streamer->SendDummyDMR(tmp_srcid, m_Streamer->get_dstid(), m_Streamer->get_dmrflco());
		}

		if (!m_xlxmodule.empty() && !m_xlxConnected) {
			writeXLXLink(tmp_srcid, m_Streamer->get_dstid(), m_dmrNetwork);
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
					if (m_Streamer->unkinkReceived()) {
						LogMessage("DMR Unlink Received");
						m_TGChange.start();
						m_TG_connect_state = SEND_REPLY;
						//m_unlinkReceived = false;
					}
					break;
				case SEND_REPLY:
					if (m_Streamer->not_busy() && m_TGChange.elapsed() > 600) {
						m_TGChange.start();
						m_TG_connect_state = SEND_PTT;				
						m_wiresX->SendCReply();
					//	m_tgConnected = false;
					}
					break;
				case SEND_PTT:
				//	if (m_TGChange.elapsed() > 200) {				
					if (m_Streamer->not_busy() && !m_wiresX->isBusy() && (m_TGChange.elapsed() > 900U)) {
						m_TGChange.start();
						m_lostTimer.start();
						m_TG_connect_state = TG_NONE;
						if (m_ptt_dstid) {
							LogMessage("Sending PTT: Src: %d Dst: %s%d", tmp_srcid, m_ptt_pc ? "" : "TG ", m_ptt_dstid);
							m_Streamer->SendFinalPTT();
							//m_Streamer->SendDummyDMR(tmp_srcid, m_ptt_dstid, m_ptt_pc ? FLCO_USER_USER : FLCO_GROUP);
						}
						//m_not_busy=true;
					}
					break;
				case TG_NONE:
				case WAITING_SEND_UNLINK:
				default: 
					break;
			}

			if ((m_TG_connect_state != TG_NONE) && (m_TG_connect_state != TG_DISABLE) && (m_TGChange.elapsed() > 30000U)) {
				LogMessage("Timeout changing TG");
				m_TG_connect_state = TG_NONE;
				m_wiresX->SendCReply();				
				//m_not_busy=true;
				m_lostTimer.start();
				if (m_ptt_dstid) {
					LogMessage("Sending PTT in TG Timeout: Src: %d Dst: %s%d", tmp_srcid, m_ptt_pc ? "" : "TG ", m_ptt_dstid);
					m_Streamer->SendFinalPTT();
					//m_Streamer->SendDummyDMR(tmp_srcid, m_ptt_dstid, m_ptt_pc ? FLCO_USER_USER : FLCO_GROUP);
				}				
			} 
		} 
	} 
}

void CYSFGateway::processRemoteCommands()
{
	unsigned char buffer[200U];
	in_addr address;
	int tmp_dst_id;
	unsigned int port;
	int ret;

	int res = m_remoteSocket->read(buffer, 200U, address, port);
	if (res > 0) {
		buffer[res] = '\0';
		if (::memcmp(buffer + 0U, "LinkYSF", 7U) == 0) {
			std::string id = std::string((char*)(buffer + 7U));
			//if ((m_tg_type == DMR) || (m_tg_type == P25) || (m_tg_type == NXDN)) return;
			LogMessage("Triying to remote conect to YSF %s.",id.c_str());
			// CReflector* reflector = m_ysfReflectors->findById(id);
			// if (reflector == NULL)
			// 	reflector = m_ysfReflectors->findByName(id);
			// if (reflector != NULL) {		
				tmp_dst_id = atoi(id.c_str());
				// if (tmp_dst_id==0) {
				// 	LogMessage("Reflector YSF non valid: %s",reflector->m_id.c_str());
				// 	return;
				// }
				if ((m_tg_type != YSF) ) {
					m_last_YSF_TG=tmp_dst_id;
					ret=TG_Connect(2);
				}
				else ret = TG_Connect(tmp_dst_id);
				if (ret) {
					LogMessage("Remote Connected to YSF %05d - \"%s\" has been requested", tmp_dst_id, m_current.c_str());
					if ((m_tg_type == YSF) || (m_tg_type == FCS)) m_wiresX->SendCReply();
				} else {
					LogMessage("Remote Error with YSF connect");
					//m_wiresX->SendDReply();
				}
			// }
		} else if (::memcmp(buffer + 0U, "LinkFCS", 7U) == 0) {
			std::string id = std::string((char*)(buffer + 7U));
			//if ((m_tg_type == DMR) || (m_tg_type == P25) || (m_tg_type == NXDN)) return;
			LogMessage("Triying to remote conect to FCS %s.",id.c_str());
			// CReflector* reflector = m_fcsReflectors->findById(atoi(id.c_str()));
			// if (reflector == NULL)
			// 	reflector = m_fcsReflectors->findByName(id);
			// if (reflector != NULL) {				
			// 	int tmp_dst_id = atoi(reflector->m_id.c_str());
			// 	if (tmp_dst_id==0) {
			// 		LogMessage("Reflector FCS non valid: %s",reflector->m_id.c_str());
			// 		return;
			// 	}
				tmp_dst_id=atoi(id.c_str());
				if ((m_tg_type != FCS)) {
					m_last_FCS_TG=tmp_dst_id;					
					ret=TG_Connect(3);
				}
				else ret = TG_Connect(tmp_dst_id);
				if (ret) {
					LogMessage("Remote Connected to FCS %05d - \"%s\" has been requested", tmp_dst_id, m_current.c_str());
					m_wiresX->SendCReply();
				} else {
					LogMessage("Remote Error with FCS connect");
					//m_wiresX->SendDReply();
				}
			// }		
		} else if (::memcmp(buffer + 0U, "LinkDMR", 7U) == 0) {
			std::string id = std::string((char*)(buffer + 7U));
			if ((m_tg_type == DMR) || (m_tg_type == P25) || (m_tg_type == NXDN)) return;
			LogMessage("Triying to remote conect to DMR %s.",id.c_str());
			// CReflector* reflector = m_fcsReflectors->findById(atoi(id.c_str()));
			// if (reflector == NULL)
			// 	reflector = m_fcsReflectors->findByName(id);
			// if (reflector != NULL) {				
			// 	int tmp_dst_id = atoi(reflector->m_id.c_str());
			// 	if (tmp_dst_id==0) {
			// 		LogMessage("Reflector FCS non valid: %s",reflector->m_id.c_str());
			// 		return;
			// 	}
				tmp_dst_id=atoi(id.c_str());
				if ((m_tg_type != DMR)) {
					m_last_DMR_TG=tmp_dst_id;					
					ret=TG_Connect(4);
				}
				else ret = TG_Connect(tmp_dst_id);
				if (ret) {
					LogMessage("Remote Connected to DMR %05d - \"%s\" has been requested", tmp_dst_id, m_current.c_str());
					m_wiresX->SendCReply();
				} else {
					LogMessage("Remote Error with DMR connect");
					//m_wiresX->SendDReply();
				}
			// }		
		} else {
			CUtils::dump("Invalid remote command received", buffer, res);
		}
	}
}
