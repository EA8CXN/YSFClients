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

#if !defined(CONF_H)
#define	CONF_H

#include <string>
#include <vector>

class CConf
{
public:
  CConf(const std::string& file);
  ~CConf();

  bool read();

  // The General section
  std::string  getCallsign() const;
  std::string  getSuffix() const;
  unsigned int getId() const;
  std::string  getRptAddress() const;
  unsigned int getRptPort() const;
  std::string  getMyAddress() const;
  unsigned int getMyPort() const;
  bool         getWiresXMakeUpper() const;
  bool         getDaemon() const;
  unsigned int getBeaconTime() const;
  bool 		   getSaveAMBE() const; 
  unsigned int getAMBECompA() const;
  unsigned int getAMBECompB() const;   

  // The Info section
  unsigned int getRxFrequency() const;
  unsigned int getTxFrequency() const;
  unsigned int getPower() const;
  float        getLatitude() const;
  float        getLongitude() const;
  int          getHeight() const;
  std::string  getName() const;
  std::string  getLocation() const;
  std::string  getDescription() const;
  std::string  getURL() const;

  // The Log section
  unsigned int getLogDisplayLevel() const;
  unsigned int getLogFileLevel() const;
  std::string  getLogFilePath() const;
  std::string  getLogFileRoot() const;

  // The aprs.fi section
  bool         getAPRSEnabled() const;
  std::string  getAPRSServer() const;
  unsigned int getAPRSPort() const;
  std::string  getAPRSPassword() const;
  std::string  getAPRSCallsign() const;
  std::string  getAPRSAPIKey() const;
  unsigned int getAPRSRefresh() const;
  std::string  getAPRSDescription() const;
  std::string  getAPRSIcon() const;
  std::string  getAPRSBeaconText() const;
  unsigned int getAPRSBeaconTime() const;
  bool         getAPRSFollowMe() const;

  // The Network section
  std::string  getNetworkStartup() const;
  unsigned int getNetworkTypeStartup() const;  
  unsigned int getNetworkInactivityTimeout() const;
  bool         getNetworkRevert() const;
  bool         getNetworkDebug() const;
  bool 		     getNetworkNoChange() const;
  unsigned int getNetworkReloadTime() const;  

  // The YSF Network section
  bool         getYSFNetworkEnabled() const;
  unsigned int getYSFStartup() const;  
  std::string  getYSFNetworkOptions() const;
  unsigned int getYSFNetworkPort() const;
  std::string  getYSFNetworkHosts() const;
  std::string  getYSFNetworkParrotAddress() const;
  unsigned int getYSFNetworkParrotPort() const;
  std::string  getYSFNetworkYSF2DMRAddress() const;
  unsigned int getYSFNetworkYSF2DMRPort() const;
  std::string  getYSFNetworkYSF2NXDNAddress() const;
  unsigned int getYSFNetworkYSF2NXDNPort() const;
  bool         getNXDNNetworkEnabled() const;
  std::string  getNXDNNetworkFile() const;
  unsigned int getNXDNStartup() const;
  std::string  getYSFNetworkYSF2P25Address() const;
  unsigned int getYSFNetworkYSF2P25Port() const;
  bool         getP25NetworkEnabled() const;  
  std::string  getP25NetworkFile() const;
  unsigned int getP25Startup() const;  
  

  // The FCS Network section
  bool         getFCSNetworkEnabled() const;
  unsigned int getFCSStartup() const;  
  std::string  getFCSNetworkOptions() const;
  std::string  getFCSNetworkFile() const;
  unsigned int getFCSNetworkPort() const;
  
  // The DMR Network section
  bool         getDMRNetworkEnabled() const;
  unsigned int getDMRStartup() const;
  unsigned int getDMRDstId() const;
  std::string  getDMRNetworkFile() const;
  std::string  getDMRNetworkAddress() const;
  unsigned int getDMRNetworkPort() const;
  unsigned int getDMRNetworkLocal() const;
  std::string  getDMRNetworkPassword() const;
  std::string  getDMRNetworkOptions() const;
  bool         getDMRNetworkEnableUnlink() const;
  unsigned int getDMRNetworkIDUnlink() const;
  bool         getDMRNetworkPCUnlink() const;
  std::string  getDMRIdLookupFile() const;
  unsigned int getDMRIdLookupTime() const;
  std::string  getDMRXLXFile() const;
  std::string  getDMRXLXModule() const;
  unsigned int getDMRXLXReflector() const;  
  
  // The Mobile GPS section
  bool         getMobileGPSEnabled() const;
  std::string  getMobileGPSAddress() const;
  unsigned int getMobileGPSPort() const;

  // The Remote Commands section
  bool         getRemoteCommandsEnabled() const;
  unsigned int getRemoteCommandsPort() const;
  std::string  getNewsPath() const;  
  std::string  getBeaconPath() const;
  unsigned int getStartupDGID() const;

private:
  std::string  m_file;
  std::string  m_callsign;
  unsigned int m_id;
  std::string  m_rptAddress;
  unsigned int m_rptPort;
  std::string  m_myAddress;
  unsigned int m_myPort;
  bool         m_wiresXMakeUpper;
  bool         m_daemon;
  unsigned int m_BeaconTime;
  bool 		   m_SaveAMBE;
  unsigned int m_AMBECompA;
  unsigned int m_AMBECompB;   

  unsigned int m_rxFrequency;
  unsigned int m_txFrequency;
  unsigned int m_power;
  float        m_latitude;
  float        m_longitude;
  int          m_height;
  std::string  m_name;
  std::string  m_location;
  std::string  m_description;
  std::string  m_url;

  unsigned int m_logDisplayLevel;
  unsigned int m_logFileLevel;
  std::string  m_logFilePath;
  std::string  m_logFileRoot;

  bool         m_aprsEnabled;
  std::string  m_aprsServer;
  unsigned int m_aprsPort;
  std::string  m_aprsPassword;
  std::string  m_aprsCallsign;
  std::string  m_aprsAPIKey;
  unsigned int m_aprsRefresh;  
  std::string  m_aprsDescription;
  std::string  m_icon;
  std::string  m_beacon_text;
  unsigned int m_aprs_beacon_time;
  bool		   m_aprs_follow_me;


  std::string  m_networkStartup;
  std::string  m_networkTypeStartup;  
  unsigned int m_networkInactivityTimeout;
  bool         m_networkDebug;
  bool		   m_networkNoChange;
  unsigned int m_NetworkReloadTime;

  bool         m_ysfNetworkEnabled;
  unsigned int m_ysfStartup; 
  std::string  m_ysfNetworkOptions;
  unsigned int m_ysfNetworkPort;
  std::string  m_ysfNetworkHosts;
  std::string  m_ysfNetworkParrotAddress;
  unsigned int m_ysfNetworkParrotPort;
  std::string  m_ysfNetworkYSF2NXDNAddress;
  unsigned int m_ysfNetworkYSF2NXDNPort;
  bool         m_nxdnNetworkEnabled;
  std::string  m_nxdnNetworkFile;
  unsigned int m_nxdnStartup;  
  std::string  m_ysfNetworkYSF2P25Address;
  unsigned int m_ysfNetworkYSF2P25Port;
  bool         m_p25NetworkEnabled;
  std::string  m_p25NetworkFile;
  unsigned int m_p25Startup;
  
  bool         m_fcsNetworkEnabled;
  unsigned int m_fcsStartup;

  std::string  m_fcsNetworkFile;
  unsigned int m_fcsNetworkPort;
  std::string  m_fcsNetworkOptions;
  bool         m_dmrNetworkEnabled;  
  unsigned int m_dmrStartup;
  std::string  m_dmrNetworkFile;
  std::string  m_dmrNetworkAddress;
  unsigned int m_dmrNetworkPort;
  unsigned int m_dmrNetworkLocal;
  std::string  m_dmrNetworkPassword;
  std::string  m_dmrNetworkOptions;
  bool         m_dmrNetworkEnableUnlink;
  unsigned int m_dmrNetworkIDUnlink;
  bool         m_dmrNetworkPCUnlink;   
  std::string  m_dmrIdLookupFile;  
  unsigned int m_dmrIdLookupTime;
  std::string  m_dmrXLXFile;
  std::string  m_dmrXLXModule;
  unsigned int m_dmrXLXReflector;  

  bool         m_mobileGPSEnabled;
  std::string  m_mobileGPSAddress;
  unsigned int m_mobileGPSPort;

  bool         m_remoteCommandsEnabled;
  unsigned int m_remoteCommandsPort;
  std::string  m_newspath;
  std::string  m_beaconpath; 
  unsigned int m_ysfDGID;
};

#endif
