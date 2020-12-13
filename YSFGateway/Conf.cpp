/*
 *   Copyright (C) 2015-2019 by Jonathan Naylor G4KLX
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

#include "Conf.h"
#include "Log.h"
#include "Reflectors.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

const int BUFFER_SIZE = 500;

enum SECTION {
  SECTION_NONE,
  SECTION_GENERAL,
  SECTION_INFO,
  SECTION_LOG,
  SECTION_APRS_FI,
  SECTION_STORAGE,  
  SECTION_NETWORK,
  SECTION_YSF_NETWORK,
  SECTION_FCS_NETWORK,
  SECTION_DMR_NETWORK,  
  SECTION_MOBILE_GPS,
  SECTION_REMOTE_COMMANDS
};

CConf::CConf(const std::string& file) :
m_file(file),
m_callsign(),
m_id(0U),
m_rptAddress(),
m_rptPort(0U),
m_myAddress(),
m_myPort(0U),
m_wiresXMakeUpper(true),
m_daemon(false),
m_BeaconTime(0U),
m_SaveAMBE(false),
m_AMBECompA(0U),
m_AMBECompB(0U),
m_rxFrequency(0U),
m_txFrequency(0U),
m_power(0U),
m_latitude(0.0F),
m_longitude(0.0F),
m_height(0),
m_name(),
m_location(),
m_description(),
m_url(),
m_logDisplayLevel(0U),
m_logFileLevel(0U),
m_logFilePath(),
m_logFileRoot(),
m_aprsEnabled(false),
m_aprsServer(),
m_aprsPort(0U),
m_aprsPassword(),
m_aprsCallsign(),
m_aprsAPIKey(""),
m_aprsRefresh(120),
m_aprsDescription("YSF Node"),
m_icon("YY"),
m_beacon_text("YSF MMDVM Node"),
m_aprs_beacon_time(20U),
m_aprs_follow_me(false),
m_networkStartup(),
m_networkTypeStartup("YSF"),
m_networkInactivityTimeout(0U),
m_networkDebug(false),
m_networkNoChange(false),
m_NetworkReloadTime(0U),
m_ysfNetworkEnabled(false),
m_ysfNetworkOptions(""),
m_ysfNetworkPort(0U),
m_ysfNetworkHosts(),
m_ysfNetworkParrotAddress("127.0.0.1"),
m_ysfNetworkParrotPort(42012U),
m_ysfNetworkYSF2NXDNAddress("127.0.0.1"),
m_ysfNetworkYSF2NXDNPort(0U),
m_nxdnNetworkEnabled(false),
m_nxdnNetworkFile(),
m_ysfNetworkYSF2P25Address("127.0.0.1"),
m_ysfNetworkYSF2P25Port(0U),
m_p25NetworkEnabled(false),
m_p25NetworkFile(),
m_fcsNetworkEnabled(false),
m_fcsNetworkFile(),
m_fcsNetworkPort(0U),
m_fcsNetworkOptions(""),
m_dmrNetworkEnabled(false),
m_dmrStartup(),
m_dmrNetworkFile(),
m_dmrNetworkAddress(),
m_dmrNetworkPort(0U),
m_dmrNetworkLocal(0U),
m_dmrNetworkPassword(),
m_dmrNetworkOptions(),
m_dmrNetworkEnableUnlink(true),
m_dmrNetworkIDUnlink(4000U),
m_dmrNetworkPCUnlink(false),
m_dmrIdLookupFile(),
m_dmrIdLookupTime(0U),
m_mobileGPSEnabled(false),
m_mobileGPSAddress(),
m_mobileGPSPort(0U),
m_remoteCommandsEnabled(false),
m_remoteCommandsPort(6073U),
m_newspath("/tmp/news"),
m_beaconpath("/usr/local/sbin/beacon.amb"),
m_ysfDGID(0U)
{
}

CConf::~CConf()
{
}

bool CConf::read()
{
  FILE* fp = ::fopen(m_file.c_str(), "rt");
  if (fp == NULL) {
    ::fprintf(stderr, "Couldn't open the .ini file - %s\n", m_file.c_str());
    return false;
  }

  SECTION section = SECTION_NONE;

  char buffer[BUFFER_SIZE];
  while (::fgets(buffer, BUFFER_SIZE, fp) != NULL) {
    if (buffer[0U] == '#')
      continue;

    if (buffer[0U] == '[') {
      if (::strncmp(buffer, "[General]", 9U) == 0)
        section = SECTION_GENERAL;
	  else if (::strncmp(buffer, "[Info]", 6U) == 0)
		  section = SECTION_INFO;
	  else if (::strncmp(buffer, "[Log]", 5U) == 0)
		  section = SECTION_LOG;
	  else if (::strncmp(buffer, "[aprs.fi]", 9U) == 0)
		  section = SECTION_APRS_FI;
	  else if (::strncmp(buffer, "[Network]", 9U) == 0)
		  section = SECTION_NETWORK;
	  else if (::strncmp(buffer, "[YSF Network]", 13U) == 0)
		  section = SECTION_YSF_NETWORK;
	  else if (::strncmp(buffer, "[FCS Network]", 13U) == 0)
		  section = SECTION_FCS_NETWORK;
	  else if (::strncmp(buffer, "[DMR Network]", 13U) == 0)
		  section = SECTION_DMR_NETWORK;
	  else if (::strncmp(buffer, "[Mobile GPS]", 12U) == 0)
		  section = SECTION_MOBILE_GPS;	 
	  else if (::strncmp(buffer, "[Remote Commands]", 17U) == 0)
		  section = SECTION_REMOTE_COMMANDS;		   
	  else
	  	  section = SECTION_NONE;

	  continue;
    }

    char* key = ::strtok(buffer, " \t=\r\n");
    if (key == NULL)
      continue;
  
    char* value = ::strtok(NULL, "\r\n");
    if (value == NULL)
      continue;

    // Remove quotes from the value
    size_t len = ::strlen(value);
    if (len > 1U && *value == '"' && value[len - 1U] == '"') {
      value[len - 1U] = '\0';
      value++;
    }

	if (section == SECTION_GENERAL) {
		if (::strcmp(key, "Callsign") == 0) {
			// Convert the callsign to upper case
			for (unsigned int i = 0U; value[i] != 0; i++)
				value[i] = ::toupper(value[i]);
			m_callsign = value;
		} else if (::strcmp(key, "Id") == 0)
			m_id = (unsigned int)::atoi(value);
		else if (::strcmp(key, "RptAddress") == 0)
			m_rptAddress = value;
		else if (::strcmp(key, "RptPort") == 0)
			m_rptPort = (unsigned int)::atoi(value);
		else if (::strcmp(key, "LocalAddress") == 0)
			m_myAddress = value;
		else if (::strcmp(key, "LocalPort") == 0)
			m_myPort = (unsigned int)::atoi(value);
		else if (::strcmp(key, "WiresXMakeUpper") == 0)
			m_wiresXMakeUpper = ::atoi(value) == 1;
		else if (::strcmp(key, "BeaconTime") == 0)
			m_BeaconTime = (unsigned int)::atoi(value);
		else if (::strcmp(key, "SaveAMBE") == 0)
			m_SaveAMBE = ::atoi(value) == 1;		
		else if (::strcmp(key, "Daemon") == 0)
			m_daemon = ::atoi(value) == 1;
		else if (::strcmp(key, "AMBECompA") == 0)
			m_AMBECompA = (unsigned int)::atoi(value);
		else if (::strcmp(key, "AMBECompB") == 0)
			m_AMBECompB = (unsigned int)::atoi(value);	
		else if (::strcmp(key, "NewsPath") == 0)
			m_newspath = value;
		else if (::strcmp(key, "BeaconPath") == 0)
			m_beaconpath = value;				
	} else if (section == SECTION_INFO) {
		if (::strcmp(key, "TXFrequency") == 0)
			m_txFrequency = (unsigned int)::atoi(value);
		else if (::strcmp(key, "RXFrequency") == 0)
			m_rxFrequency = (unsigned int)::atoi(value);
		else if (::strcmp(key, "Power") == 0)
			m_power = (unsigned int)::atoi(value);
		else if (::strcmp(key, "Latitude") == 0)
			m_latitude = float(::atof(value));
		else if (::strcmp(key, "Longitude") == 0)
			m_longitude = float(::atof(value));
		else if (::strcmp(key, "Height") == 0)
			m_height = ::atoi(value);
		else if (::strcmp(key, "Name") == 0)
			m_name = value;
		else if (::strcmp(key, "Location") == 0)
			m_location = value;
		else if (::strcmp(key, "Description") == 0)
			m_description = value;
		else if (::strcmp(key, "URL") == 0)
			m_url = value;
	} else if (section == SECTION_LOG) {
		if (::strcmp(key, "FilePath") == 0)
			m_logFilePath = value;
		else if (::strcmp(key, "FileRoot") == 0)
			m_logFileRoot = value;
		else if (::strcmp(key, "FileLevel") == 0)
			m_logFileLevel = (unsigned int)::atoi(value);
		else if (::strcmp(key, "DisplayLevel") == 0)
			m_logDisplayLevel = (unsigned int)::atoi(value);
	} else if (section == SECTION_APRS_FI) {
		if (::strcmp(key, "AprsCallsign") == 0) {
			// Convert the callsign to upper case
			for (unsigned int i = 0U; value[i] != 0; i++)
				value[i] = ::toupper(value[i]);
			m_aprsCallsign = value;
		}
		else if (::strcmp(key, "Icon") == 0)
				m_icon = value;
		else if (::strcmp(key, "Beacon") == 0)
				m_beacon_text = value;
		else if (::strcmp(key, "BeaconTime") == 0)
				m_aprs_beacon_time = (unsigned int)::atoi(value);		
		if (::strcmp(key, "Enable") == 0)
			m_aprsEnabled = ::atoi(value) == 1;
		else if (::strcmp(key, "HotSpotFollow") == 0)
			m_aprs_follow_me = ::atoi(value) == 1;		
		else if (::strcmp(key, "Server") == 0)
			m_aprsServer = value;
		else if (::strcmp(key, "Port") == 0)
			m_aprsPort = (unsigned int)::atoi(value);
		else if (::strcmp(key, "Password") == 0)
			m_aprsPassword = value;
		else if (::strcmp(key, "APIKey") == 0)
			m_aprsAPIKey = value;
		else if (::strcmp(key, "Refresh") == 0)
			m_aprsRefresh = (unsigned int)::atoi(value);		
		else if (::strcmp(key, "Description") == 0)
			m_aprsDescription = value;
	} else if (section == SECTION_NETWORK) {
		if (::strcmp(key, "Startup") == 0)
			m_networkStartup = value;
		else if (::strcmp(key, "Type") == 0)
			m_networkTypeStartup = value;		
		else if (::strcmp(key, "InactivityTimeout") == 0)
			m_networkInactivityTimeout = (unsigned int)::atoi(value);
		else if (::strcmp(key, "Debug") == 0)
			m_networkDebug = ::atoi(value) == 1;
		else if (::strcmp(key, "ReloadTime") == 0)
			m_NetworkReloadTime = (unsigned int)::atoi(value);		
		else if (::strcmp(key, "NoChange") == 0)
			m_networkNoChange = ::atoi(value) == 1;		
		else if (::strcmp(key, "Jitter") == 0)
			m_jitter = ::atoi(value);
	} else if (section == SECTION_YSF_NETWORK) {
		if (::strcmp(key, "Enable") == 0)
			m_ysfNetworkEnabled = ::atoi(value) == 1;
		else if (::strcmp(key, "Startup") == 0)
			m_ysfStartup = (unsigned int)::atoi(value);	
		else if (::strcmp(key, "Options") == 0)
			m_ysfNetworkOptions = value;				
		else if (::strcmp(key, "StartupDGID") == 0)
			m_ysfDGID = (unsigned int)::atoi(value);					
		else if (::strcmp(key, "Port") == 0)
			m_ysfNetworkPort = (unsigned int)::atoi(value);
		else if (::strcmp(key, "Hosts") == 0)
			m_ysfNetworkHosts = value;
		else if (::strcmp(key, "ParrotAddress") == 0)
			m_ysfNetworkParrotAddress = value;
		else if (::strcmp(key, "ParrotPort") == 0)
			m_ysfNetworkParrotPort = (unsigned int)::atoi(value);
		else if (::strcmp(key, "YSF2NXDNAddress") == 0)
			m_ysfNetworkYSF2NXDNAddress = value;
		else if (::strcmp(key, "YSF2NXDNPort") == 0)
			m_ysfNetworkYSF2NXDNPort = (unsigned int)::atoi(value);
		if (::strcmp(key, "NXDNEnable") == 0)
			m_nxdnNetworkEnabled = ::atoi(value) == 1;			
		else if (::strcmp(key, "NXDNHosts") == 0)
			m_nxdnNetworkFile = value;
		else if (::strcmp(key, "NXDNStartup") == 0)
			m_nxdnStartup = (unsigned int)::atoi(value);			
		else if (::strcmp(key, "YSF2P25Address") == 0)
			m_ysfNetworkYSF2P25Address = value;
		else if (::strcmp(key, "YSF2P25Port") == 0)
			m_ysfNetworkYSF2P25Port = (unsigned int)::atoi(value);
		if (::strcmp(key, "P25Enable") == 0)
			m_p25NetworkEnabled = ::atoi(value) == 1;			
		else if (::strcmp(key, "P25Hosts") == 0)
			m_p25NetworkFile = value;
		else if (::strcmp(key, "P25Startup") == 0)
			m_p25Startup = (unsigned int)::atoi(value);			
	} else if (section == SECTION_FCS_NETWORK) {
		if (::strcmp(key, "Enable") == 0)
			m_fcsNetworkEnabled = ::atoi(value) == 1;
		else if (::strcmp(key, "Startup") == 0)
			m_fcsStartup = (unsigned int)::atoi(value);
		else if (::strcmp(key, "Options") == 0)
			m_fcsNetworkOptions = value;							
		else if (::strcmp(key, "Rooms") == 0)
			m_fcsNetworkFile = value;
		else if (::strcmp(key, "Port") == 0)
			m_fcsNetworkPort = (unsigned int)::atoi(value);
	} else if (section == SECTION_DMR_NETWORK) {
		if (::strcmp(key, "Enable") == 0)
			m_dmrNetworkEnabled = ::atoi(value) == 1;
		else if (::strcmp(key, "Startup") == 0)
			m_dmrStartup = (unsigned int)::atoi(value);		
		else if (::strcmp(key, "Hosts") == 0)
			m_dmrNetworkFile = value;
		else if (::strcmp(key, "Address") == 0)
			m_dmrNetworkAddress = value;
		else if (::strcmp(key, "Port") == 0)
			m_dmrNetworkPort = (unsigned int)::atoi(value);
		else if (::strcmp(key, "Local") == 0)
			m_dmrNetworkLocal = (unsigned int)::atoi(value);
		else if (::strcmp(key, "Password") == 0)
			m_dmrNetworkPassword = value;
		else if (::strcmp(key, "Options") == 0)
			m_dmrNetworkOptions = value;
		else if (::strcmp(key, "EnableUnlink") == 0)
			m_dmrNetworkEnableUnlink = ::atoi(value) == 1;
		else if (::strcmp(key, "TGUnlink") == 0)
			m_dmrNetworkIDUnlink = (unsigned int)::atoi(value);
		else if (::strcmp(key, "PCUnlink") == 0)
			m_dmrNetworkPCUnlink = ::atoi(value) == 1;
		else if (::strcmp(key, "File") == 0)
			m_dmrIdLookupFile = value;
		else if (::strcmp(key, "Time") == 0)
			m_dmrIdLookupTime = (unsigned int)::atoi(value);		
	} else if (section == SECTION_MOBILE_GPS) {
		if (::strcmp(key, "Enable") == 0)
			m_mobileGPSEnabled = ::atoi(value) == 1;
		else if (::strcmp(key, "Address") == 0)
			m_mobileGPSAddress = value;
		else if (::strcmp(key, "Port") == 0)
			m_mobileGPSPort = (unsigned int)::atoi(value);
	} else if (section == SECTION_REMOTE_COMMANDS) {
		if (::strcmp(key, "Enable") == 0)
			m_remoteCommandsEnabled = ::atoi(value) == 1;
		else if (::strcmp(key, "Port") == 0)
			m_remoteCommandsPort = (unsigned int)::atoi(value);
	}
  }

  ::fclose(fp);

  return true;
}

std::string CConf::getCallsign() const
{
  return m_callsign;
}

unsigned int CConf::getId() const
{
	return m_id;
}

std::string CConf::getRptAddress() const
{
	return m_rptAddress;
}

unsigned int CConf::getRptPort() const
{
	return m_rptPort;
}

std::string CConf::getMyAddress() const
{
	return m_myAddress;
}

unsigned int CConf::getMyPort() const
{
	return m_myPort;
}

bool CConf::getWiresXMakeUpper() const
{
	return m_wiresXMakeUpper;
}

bool CConf::getDaemon() const
{
	return m_daemon;
}

unsigned int CConf::getAMBECompA() const
{
	return m_AMBECompA;
}

unsigned int CConf::getAMBECompB() const
{
	return m_AMBECompB;
}

unsigned int CConf::getRxFrequency() const
{
	return m_rxFrequency;
}

unsigned int CConf::getTxFrequency() const
{
	return m_txFrequency;
}

unsigned int CConf::getPower() const
{
	return m_power;
}

float CConf::getLatitude() const
{
	return m_latitude;
}

float CConf::getLongitude() const
{
	return m_longitude;
}

int CConf::getHeight() const
{
	return m_height;
}

std::string CConf::getName() const
{
	return m_name;
}

std::string CConf::getLocation() const
{
	return m_location;
}

std::string CConf::getDescription() const
{
	return m_description;
}

std::string CConf::getURL() const
{
	return m_url;
}

unsigned int CConf::getLogDisplayLevel() const
{
	return m_logDisplayLevel;
}

unsigned int CConf::getLogFileLevel() const
{
	return m_logFileLevel;
}

std::string CConf::getLogFilePath() const
{
  return m_logFilePath;
}

std::string CConf::getLogFileRoot() const
{
  return m_logFileRoot;
}

bool CConf::getAPRSEnabled() const
{
	return m_aprsEnabled;
}

std::string CConf::getAPRSServer() const
{
	return m_aprsServer;
}

unsigned int CConf::getAPRSPort() const
{
	return m_aprsPort;
}

std::string CConf::getAPRSPassword() const
{
	return m_aprsPassword;
}

std::string CConf::getAPRSDescription() const
{
	return m_aprsDescription;
}

std::string CConf::getAPRSCallsign() const
{
	return m_aprsCallsign;
}

std::string CConf::getAPRSAPIKey() const
{
	return m_aprsAPIKey;
}

unsigned int CConf::getAPRSRefresh() const
{
	return m_aprsRefresh;
}

std::string CConf::getNetworkStartup() const
{
	return m_networkStartup;
}

unsigned int CConf::getDMRStartup() const
{
	return m_dmrStartup;
}

unsigned int CConf::getYSFStartup() const
{
	return m_ysfStartup;
}

unsigned int CConf::getNXDNStartup() const
{
	return m_nxdnStartup;
}

unsigned int CConf::getFCSStartup() const
{
	return m_fcsStartup;
}

unsigned int CConf::getP25Startup() const
{
	return m_p25Startup;
}

unsigned int CConf::getNetworkTypeStartup() const
{
	if (::strcmp(m_networkTypeStartup.c_str(),"YSF")==0) return YSF;
	else if (::strcmp(m_networkTypeStartup.c_str(),"FCS")==0) return FCS;
	else if (::strcmp(m_networkTypeStartup.c_str(),"DMR")==0) return DMR;
	else if (::strcmp(m_networkTypeStartup.c_str(),"P25")==0) return P25;
	else if (::strcmp(m_networkTypeStartup.c_str(),"NXDN")==0) return NXDN;
	else return 0U;
}


unsigned int CConf::getNetworkInactivityTimeout() const
{
	return m_networkInactivityTimeout;
}

bool CConf::getNetworkDebug() const
{
	return m_networkDebug;
}

bool CConf::getYSFNetworkEnabled() const
{
	return m_ysfNetworkEnabled;
}

bool CConf::getNXDNNetworkEnabled() const
{
	return m_nxdnNetworkEnabled;
}

bool CConf::getP25NetworkEnabled() const
{
	return m_p25NetworkEnabled;
}

bool CConf::getNetworkNoChange() const
{
	return m_networkNoChange;
}

unsigned int CConf::getYSFNetworkPort() const
{
  return m_ysfNetworkPort;
}

std::string CConf::getYSFNetworkHosts() const
{
	return m_ysfNetworkHosts;
}

unsigned int CConf::getNetworkReloadTime() const
{
	return m_NetworkReloadTime;
}

std::string CConf::getYSFNetworkParrotAddress() const
{
	return m_ysfNetworkParrotAddress;
}

unsigned int CConf::getYSFNetworkParrotPort() const
{
	return m_ysfNetworkParrotPort;
}

std::string CConf::getYSFNetworkYSF2NXDNAddress() const
{
	return m_ysfNetworkYSF2NXDNAddress;
}

unsigned int CConf::getYSFNetworkYSF2NXDNPort() const
{
	return m_ysfNetworkYSF2NXDNPort;
}

std::string CConf::getYSFNetworkYSF2P25Address() const
{
	return m_ysfNetworkYSF2P25Address;
}

unsigned int CConf::getYSFNetworkYSF2P25Port() const
{
	return m_ysfNetworkYSF2P25Port;
}

bool CConf::getFCSNetworkEnabled() const
{
	return m_fcsNetworkEnabled;
}

std::string CConf::getFCSNetworkFile() const
{
	return m_fcsNetworkFile;
}

unsigned int CConf::getFCSNetworkPort() const
{
	return m_fcsNetworkPort;
}

bool CConf::getDMRNetworkEnabled() const
{
	return m_dmrNetworkEnabled;
}

unsigned int CConf::getDMRDstId() const
{
	return m_dmrStartup;
}

std::string CConf::getDMRNetworkAddress() const
{
	return m_dmrNetworkAddress;
}

unsigned int CConf::getDMRNetworkPort() const
{
	return m_dmrNetworkPort;
}

unsigned int CConf::getDMRNetworkLocal() const
{
	return m_dmrNetworkLocal;
}

std::string CConf::getDMRNetworkPassword() const
{
	return m_dmrNetworkPassword;
}

std::string CConf::getDMRNetworkOptions() const
{
	return m_dmrNetworkOptions;
}

bool CConf::getDMRNetworkEnableUnlink() const
{
	return m_dmrNetworkEnableUnlink;
}

unsigned int CConf::getDMRNetworkIDUnlink() const
{
	return m_dmrNetworkIDUnlink;
}

bool CConf::getDMRNetworkPCUnlink() const
{
	return m_dmrNetworkPCUnlink;
}

std::string CConf::getDMRIdLookupFile() const
{
	return m_dmrIdLookupFile;
}

unsigned int CConf::getDMRIdLookupTime() const
{
	return m_dmrIdLookupTime;
}

bool CConf::getMobileGPSEnabled() const
{
	return m_mobileGPSEnabled;
}

std::string CConf::getMobileGPSAddress() const
{
	return m_mobileGPSAddress;
}

unsigned int CConf::getMobileGPSPort() const
{
	return m_mobileGPSPort;
}

std::string CConf::getAPRSIcon() const
{
  return m_icon;
}

std::string CConf::getAPRSBeaconText() const
{
  return m_beacon_text;
}


bool CConf::getAPRSFollowMe() const
{
	return m_aprs_follow_me;
}

unsigned int CConf::getAPRSBeaconTime() const
{
	return m_aprs_beacon_time;
}

unsigned int CConf::getBeaconTime() const
{
	return m_BeaconTime;
}

bool CConf::getSaveAMBE() const
{
	return m_SaveAMBE;
}

std::string CConf::getDMRNetworkFile() const
{
	return m_dmrNetworkFile;
}

std::string CConf::getNXDNNetworkFile() const
{
	return m_nxdnNetworkFile;
}

std::string CConf::getP25NetworkFile() const
{
	return m_p25NetworkFile;
}

std::string CConf::getDMRXLXFile() const
{
	return m_dmrXLXFile;
}

std::string CConf::getDMRXLXModule() const
{
	return m_dmrXLXModule;
}

unsigned int CConf::getDMRXLXReflector() const
{
	return m_dmrXLXReflector;
}

bool CConf::getRemoteCommandsEnabled() const
{
	return m_remoteCommandsEnabled;
}

unsigned int CConf::getRemoteCommandsPort() const
{
	return m_remoteCommandsPort;
}

std::string CConf::getNewsPath() const
{
	return m_newspath;
}

std::string CConf::getBeaconPath() const
{
	return m_beaconpath;
}

unsigned int CConf::getStartupDGID() const
{
	return m_ysfDGID;
}

std::string CConf::getYSFNetworkOptions() const
{
	return m_ysfNetworkOptions;
}

std::string CConf::getFCSNetworkOptions() const
{
	return m_fcsNetworkOptions;
}

unsigned int CConf::getJitter() const
{
	return m_jitter;
}
