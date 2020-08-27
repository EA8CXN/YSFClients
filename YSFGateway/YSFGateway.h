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
#include "FCSNetwork.h"
#include "WiresX.h"
#include "Timer.h"
#include "Conf.h"
#include "Sync.h"
#include "Storage.h"
#include "Streamer.h"

#include <string>

class CYSFGateway
{
public:
	CYSFGateway(const std::string& configFile);
	~CYSFGateway();

	int run();

private:
//	CWiresXStorage*  m_storage;
	CConf           m_conf;
	CReflectors*    m_ysfReflectors;
	CReflectors*    m_fcsReflectors;
	CReflectors*    m_dmrReflectors;
	CReflectors*    m_nxdnReflectors;		
	CReflectors*    m_p25Reflectors;
	CReflectors*	m_actual_ref;
	CWiresX*        m_wiresX;
	bool			m_ptt_pc;
	unsigned int    m_ptt_dstid;
	CYSFNetwork*    m_ysfNetwork;
	CFCSNetwork*    m_fcsNetwork;
	CDMRNetwork*    m_dmrNetwork;	
	std::string     m_current;
	std::string     m_startup;
	CTimer *        m_inactivityTimer;
	CTimer          m_lostTimer;
	bool            m_fcsNetworkEnabled;
	bool  			m_dmrNetworkEnabled;
	unsigned int     m_idUnlink;
	FLCO             m_flcoUnlink;
    bool			 m_NoChange;	
	enum TG_TYPE     m_tg_type;
	unsigned int     m_last_DMR_TG;
	unsigned int	 m_last_YSF_TG;
	unsigned int	 m_last_FCS_TG;
	unsigned int	 m_last_NXDN_TG;
	unsigned int	 m_last_P25_TG;
	bool             m_enableUnlink;
	in_addr      	 m_parrotAddress;
	unsigned int     m_parrotPort;
	in_addr      	 m_ysf2nxdnAddress;
	unsigned int     m_ysf2nxdnPort;
	in_addr      	 m_ysf2p25Address;
	unsigned int     m_ysf2p25Port;
	bool 			 m_tgConnected;
	CStopWatch 		 m_TGChange;
	CStopWatch 		 m_stopWatch;
	TG_STATUS 		 m_TG_connect_state;	
	std::string      m_xlxmodule;
	bool             m_xlxConnected;
	CReflectors*     m_xlxReflectors;
	unsigned int     m_xlxrefl;
	CUDPSocket*      m_remoteSocket;
	CStreamer*		 m_Streamer;
	unsigned int	 m_DGID;
	bool			 m_dmr_closed;
	bool			 m_fcs_closed;	
	bool			 m_ysf_closed;
	unsigned int 	 m_original;
	unsigned int     m_current_num;

	bool startupLinking();
	void startupReLinking();
	std::string calculateLocator();
	void createWiresX(CYSFNetwork* rptNetwork, bool makeUpper, std::string callsign);
	bool TG_Connect(unsigned int dstID);
	bool createDMRNetwork(std::string callsign);
	void SendDummyDMR(unsigned int srcid, unsigned int dstid, FLCO dmr_flco);	
	int  getTg(int m_srcHS);
	void writeXLXLink(unsigned int srcId, unsigned int dstId, CDMRNetwork* network);
	void DMR_reconect_logic(void);
	void processRemoteCommands();
};

#endif
