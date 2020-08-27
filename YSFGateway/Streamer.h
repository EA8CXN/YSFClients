/*
*   Copyright (C) 2016,2017 by Jonathan Naylor G4KLX
*   Copyright (C) 2018 by Manuel Sanchez EA7EE
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

#ifndef	Streamer_H
#define	Streamer_H



#include "Reflectors.h"
#include "Conf.h"
#include "APRSWriter.h"
#include "APRSReader.h"
#include "GPS.h"
#include "WiresX.h"
#include "DTMF.h"
#include "Timer.h"
#include "ModeConv.h"
#include "YSFNetwork.h"
#include "Reflectors.h"
#include "DMRNetwork.h"
#include "DMREmbeddedData.h"
#include "RingBuffer.h"
#include "DMRLC.h"
#include "DMRFullLC.h"
#include "DMREMB.h"
#include "DMRLookup.h"
#include "YSFFICH.h"
#include "YSFDefines.h"
#include "YSFPayload.h"
#include "FCSNetwork.h"

#include <string>

#define DMR_FRAME_PER       55U
#define YSF_FRAME_PER       90U
#define BEACON_PER			55U

#define XLX_SLOT            2U
#define XLX_COLOR_CODE      3U

#define TIME_MIN			60000U
#define TIME_SEC			1000U

enum BE_STATUS {
	BE_OFF,
	BE_INIT,
	BE_DATA,
	BE_EOT
};

enum TG_STATUS {
    TG_DISABLE,
	TG_NONE,
	WAITING_SEND_UNLINK,
	WAITING_UNLINK,
	SEND_REPLY,
	SEND_PTT
};

class CStreamer {
public:
	CStreamer(CConf *conf);
	~CStreamer();

	void createGPS(std::string callsign);
    void setBeacon(std::string file, CTimer *inactivityTimer, CTimer *lost_timer, bool NoChange, unsigned int dgid);
    void AMBE_write(unsigned char* buffer, unsigned char fi, unsigned char dt, unsigned char fn, unsigned char ft, unsigned char bn, unsigned char bt);
    void Init(CYSFNetwork *modemNetwork, CYSFNetwork* ysfNetwork, CFCSNetwork* fcsNetwork,CDMRNetwork *dmrNetwork,CReflectors*m_dmrReflectors);
    void clock(TG_STATUS m_TG_connect_state, unsigned int ms);
    bool not_busy(void);
    WX_STATUS change_TG(void);
    std::string getNetDst(void);
    std::string get_ysfcallsign(void);
    void putNetDst(std::string);
    unsigned int get_srcid(void);
    unsigned int get_dstid(void);
    void put_dmrflco(FLCO newflco);
    FLCO get_dmrflco();    
    bool unkinkReceived(void);
    void put_dstid(unsigned int new_dstid);
    void put_tgType(enum TG_TYPE tg_type);
    void SendDummyDMR(unsigned int srcid,unsigned int dstid, FLCO dmr_flco);
    void SendDummyYSF(CYSFNetwork *ysf_network, unsigned int dg_id);
    CWiresX* createWiresX(CYSFNetwork* rptNetwork, bool makeUpper, std::string callsign, std::string location);

private:

    CConf*           m_conf;
	CAPRSWriter*     m_writer;
	CGPS*            m_gps;
    CAPRSReader*     m_APRS;
	FILE* 			 m_file_out;
	bool			 m_beacon;
    std::string 	 m_beacon_name;        
	unsigned int 	 m_beacon_time;
	BE_STATUS        m_beacon_status;
    CStopWatch       m_beacon_Watch;
    CStopWatch       m_bea_voice_Watch;
    CStopWatch       m_ysfWatch;	
    CStopWatch       m_dmrWatch;
    bool             m_not_busy;
    bool             m_open_channel;
	CModeConv        m_conv;
	std::string      m_rcv_callsign;
	unsigned char    m_gid;
	bool  			 m_data_ready;
    bool             m_dmrNetworkEnabled;
    bool             m_fcsNetworkEnabled;
	bool  			 m_ysfNetworkEnabled;
    CYSFNetwork*     m_modemNetwork;
    CYSFNetwork*     m_ysfNetwork;
    CFCSNetwork*     m_fcsNetwork;    
    CDMRNetwork*     m_dmrNetwork;
    bool             m_saveAMBE;
	std::string		 m_ysf_callsign;
	std::string      m_netDst;
	CDMRLookup*      m_lookup;	
    CWiresXStorage*  m_storage;
	CWiresX*         m_wiresX;    
	CDTMF*           m_dtmf;
    unsigned int     m_colorcode;
	unsigned int     m_srcid;
	unsigned int     m_defsrcid;
	unsigned int     m_dstid;
    CDMREmbeddedData m_EmbeddedLC;
	unsigned char*   m_ysfFrame;
	unsigned char*   m_dmrFrame;	
	unsigned int     m_dmrFrames;
	unsigned int     m_ysfFrames;
	CRingBuffer<unsigned char> m_rpt_buffer;       
    enum TG_TYPE     m_tg_type;
    CTimer *         m_inactivityTimer;
    CTimer *         m_inacBeaconTimer;
    CTimer *         m_lostTimer;
    CTimer           m_networkWatchdog;
	unsigned int	 m_ysf_cnt;
	unsigned char 	 m_dmr_cnt;
	bool             m_dmrinfo;
	bool             m_firstSync;
	unsigned char    m_dmrLastDT;    
	FLCO             m_dmrflco;
	bool			 m_unlinkReceived;
    CReflectors*     m_dmrReflectors;      
    WX_STATUS        m_wx_returned;
    bool             m_NoChange;
    unsigned int     m_DGID;
    std::string      m_callsign;

    void BeaconLogic(void);
    bool containsOnlyASCII(const std::string& filePath);
    void YSFPlayback(CYSFNetwork *rptNetwork);
    void GetFromNetwork(unsigned char *buffer, CYSFNetwork* rtpNetwork);
    char *get_radio(char c);
    void GetFromModem(CYSFNetwork* rptNetwork, TG_STATUS m_TG_connect_state);
    void DMR_get_Network(CDMRData tx_dmrdata, unsigned int ms);
    void DMR_send_Network(void);
    std::string getSrcYSF_fromData(const unsigned char* buffer);
    std::string getSrcYSF_fromHeader(const unsigned char* buffer);
    unsigned int findYSFID(std::string cs, bool showdst);
    void processDTMF(unsigned char* buffer, unsigned char dt);
    void processWiresX(const unsigned char* buffer, unsigned char fi, unsigned char dt, unsigned char fn, unsigned char ft, unsigned char bn, unsigned char bt);


};

#endif
