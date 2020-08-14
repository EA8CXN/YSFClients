/*
*   Copyright (C) 2016,2017,2018 by Jonathan Naylor G4KLX
*   Copyright (C) 2018 by Andy Uribe CA6JAU
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

#if !defined(YSFGateway_H)
#define	YSFGateway_H

#include "YSFNetwork.h"
#include "Reflectors.h"
#include "DMRNetwork.h"
#include "DMREmbeddedData.h"
#include "DMRLC.h"
#include "DMRFullLC.h"
#include "DMREMB.h"
#include "DMRLookup.h"
#include "YSFFICH.h"
#include "YSFDefines.h"
#include "YSFPayload.h"
#include "FCSNetwork.h"
#include "APRSWriter.h"
#include "WiresX.h"
#include "Timer.h"
#include "Conf.h"
#include "DTMF.h"
#include "GPS.h"
#include "Sync.h"
#include "RingBuffer.h"
#include "APRSReader.h"
#include "Storage.h"
#include "ModeConv.h"

#include <string>

enum TG_STATUS {
	TG_NONE,
	WAITING_UNLINK,
	SEND_REPLY,
	SEND_PTT
};

enum BE_STATUS {
	BE_OFF,
	BE_INIT,
	BE_DATA,
	BE_EOT
};

class CYSFGateway
{
public:
	CYSFGateway(const std::string& configFile);
	~CYSFGateway();

	int run();

private:
	CWiresXStorage*  m_storage;
	std::string     m_callsign;
	CConf           m_conf;
	CAPRSWriter*    m_writer;
	CGPS*           m_gps;
	CReflectors*    m_ysfReflectors;
	CReflectors*    m_fcsReflectors;
	CReflectors*    m_dmrReflectors;
	CReflectors*    m_nxdnReflectors;		
	CReflectors*    m_p25Reflectors;
	CReflectors*	m_actual_ref;
	CDMRLookup*     m_lookup;	
	CWiresX*        m_wiresX;
	CDTMF*          m_dtmf;
	CAPRSReader*    m_APRS;
	CModeConv       m_conv;
	unsigned int     m_colorcode;
	unsigned int     m_srcHS;
	unsigned int     m_srcid;
	unsigned int     m_defsrcid;
	unsigned int     m_dstid;
	unsigned int     m_ptt_dstid;
	bool             m_ptt_pc;
	bool             m_dmrpc;	
	CYSFNetwork*    m_ysfNetwork;
	CFCSNetwork*    m_fcsNetwork;
	CDMRNetwork*    m_dmrNetwork;	
	//LINK_TYPE       m_linkType;
	std::string     m_current;
	std::string     m_startup;
	bool            m_exclude;
	CTimer *        m_inactivityTimer;
	CTimer          m_lostTimer;
	CTimer          m_networkWatchdog;
	bool            m_fcsNetworkEnabled;
	bool  			m_dmrNetworkEnabled;
	unsigned char*   m_ysfFrame;
	unsigned char*   m_dmrFrame;	
	unsigned int     m_dmrFrames;
	unsigned int     m_ysfFrames;
	CDMREmbeddedData m_EmbeddedLC;
	std::string      m_TGList;
	FLCO             m_dmrflco;
	bool             m_dmrinfo;
	unsigned int     m_idUnlink;
	FLCO             m_flcoUnlink;
	//std::string 	 m_netSrc;
	bool             m_saveAMBE;
    bool			 m_NoChange;	
	enum TG_TYPE     m_tg_type;
	unsigned int     m_last_DMR_TG;
	unsigned int	 m_last_YSF_TG;
	unsigned int	 m_last_FCS_TG;
	unsigned int	 m_last_NXDN_TG;
	unsigned int	 m_last_P25_TG;
	std::string		 m_ysf_callsign;
	std::string      m_netDst;
	bool             m_enableUnlink;
	
	in_addr      	 m_parrotAddress;
	unsigned int     m_parrotPort;
	in_addr      	 m_ysf2nxdnAddress;
	unsigned int     m_ysf2nxdnPort;
	in_addr      	 m_ysf2p25Address;
	unsigned int     m_ysf2p25Port;
	unsigned int	 m_ysf_cnt;
	unsigned char 	 m_dmr_cnt;	
	std::string      m_rcv_callsign;
	unsigned char    m_gid;
	bool  			 m_data_ready;
	bool 			 m_tgConnected;
	bool             m_firstSync;
	unsigned char    m_dmrLastDT;
	unsigned int 	 m_beacon_time;
	unsigned int     m_hangTime;
	bool			 m_unlinkReceived;
	
	CStopWatch m_TGChange;
	CStopWatch m_stopWatch;
	CStopWatch m_ysfWatch;	
	CStopWatch m_bea_voice_Watch;
	CStopWatch m_beacon_Watch;
	CStopWatch m_dmrWatch;
	
	BE_STATUS m_beacon_status;	
	bool m_gps_ready;
	bool m_not_busy;	
	TG_STATUS m_TG_connect_state;	
	
	CRingBuffer<unsigned char> m_rpt_buffer;

	std::string      m_xlxmodule;
	bool             m_xlxConnected;
	CReflectors*     m_xlxReflectors;
	unsigned int     m_xlxrefl;
	FILE* 			 m_file_out;
	bool			 m_beacon;

	bool startupLinking();
	std::string calculateLocator();
	void processWiresX(const unsigned char* buffer, unsigned char fi, unsigned char dt, unsigned char fn, unsigned char ft, unsigned char bn, unsigned char bt);
	void processDTMF(unsigned char* buffer, unsigned char dt);
	void createWiresX(CYSFNetwork* rptNetwork, bool makeUpper);
	void createGPS();
	bool TG_Connect(unsigned int dstID);
	void AMBE_write(unsigned char* buffer, unsigned char fi, unsigned char dt, unsigned char fn, unsigned char ft, unsigned char bn, unsigned char bt);
	void GetFromModem(CYSFNetwork* rptNetwork);
	void BeaconLogic(void);
	void GetFromNetwork(unsigned char *buffer, CYSFNetwork *rtpNetwork);
	void YSFPlayback(CYSFNetwork *rptNetwork);
	bool createDMRNetwork();
	void SendDummyDMR(unsigned int srcid, unsigned int dstid, FLCO dmr_flco);
	unsigned int findYSFID(std::string cs, bool showdst);
	std::string getSrcYSF(const unsigned char* source);
	int  getTg(int m_srcHS);
	void writeXLXLink(unsigned int srcId, unsigned int dstId, CDMRNetwork* network);
	void DMR_reconect_logic(void);
	void DMR_send_Network(void);
	void DMR_get_Modem(unsigned int ms);
	
};

#endif
