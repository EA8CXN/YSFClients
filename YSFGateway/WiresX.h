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

#if !defined(WIRESX_H)
#define	WIRESX_H

#include "Storage.h"
#include "Reflectors.h"
#include "YSFNetwork.h"
#include "Timer.h"
#include "StopWatch.h"
#include "RingBuffer.h"

#include <vector>
#include <string>

enum WX_STATUS {
	WXS_NONE,
	WXS_CONNECT,
	WXS_DISCONNECT,
	WXS_DX,
	WXS_ALL,
	WXS_NEWS,
	WXS_FAIL,
	WXS_LIST,
	WXS_GET_MESSAGE,
	WXS_UPLOAD,
	WXS_VOICE,
	WXS_PICTURE,
	WXS_PLAY	
};

enum WXSI_STATUS {
	WXSI_NONE,
	WXSI_DX,
	WXSI_CONNECT,
	WXSI_DISCONNECT,
	WXSI_ALL,
	WXSI_LNEWS,
	WXSI_NEWS,	
	WXSI_SEARCH,
	WXSI_CATEGORY,
	WXSI_LIST,
	WXSI_GET_MESSAGE,
	WXSI_UPLOAD_PIC,
	WXSI_UPLOAD_TXT,
	WXSI_SEND_RCONNECT,
	WXSI_SEND_PREPLY,
	WXSI_SEND_VREPLY,
	WXSI_PLAY_AMBE
};

enum WXPIC_STATUS {
	WXPIC_NONE,
	WXPIC_BEGIN,
	WXPIC_DATA,
	WXPIC_END
};

class CWiresX {
public:
	CWiresX(CWiresXStorage* storage, const std::string& callsign, std::string& location, CYSFNetwork* network, bool makeUpper, CModeConv *mconv);
	~CWiresX();
	
	WX_STATUS process(const unsigned char* data, const unsigned char* source, unsigned char fi, unsigned char dt, unsigned char fn, unsigned char ft, unsigned char bn, unsigned char bt);	
	WX_STATUS processVDMODE1(std::string& callsign, const unsigned char* data, unsigned char fi, unsigned char dt, unsigned char fn, unsigned char ft, unsigned char bn, unsigned char bt);	
	unsigned int getDstID();
	unsigned int getTgCount();
	unsigned int getOpt(unsigned int id);

	void setInfo(const std::string& name, unsigned int txFrequency, unsigned int rxFrequency);

	bool start();
	bool isBusy() const;

	CReflector* getReflector() const;
	void setReflectors(CReflectors* reflectors);
	void setReflector(CReflector* reflector, int dstID);
	void SendCReply(void);
	void SendDReply(void);
	void SendPReply(CYSFNetwork* ysfNetwork);
	void SendRConnect(CYSFNetwork* ysfNetwork);
	bool sendNetwork(void);
	void getMode1DCH(unsigned char *dch,unsigned int fn);
	std::string getCallsign();
	
	//void processConnect(CReflector* reflector);
	void processDisconnect(const unsigned char* source = NULL);

	void clock(unsigned int ms);
	void sendUploadVoiceReply();
	bool EndPicture();

private:
	CWiresXStorage*		m_storage;
	std::string     m_callsign;
	std::string     m_location;	
	std::string     m_node;
	CYSFNetwork*    m_network;
	CReflectors*    m_reflectors;
	CReflector*     m_reflector;
	std::string     m_id;
	std::string     m_name;
	unsigned char*  m_command;
	unsigned int    m_txFrequency;
	unsigned int    m_rxFrequency;
	unsigned int    m_dstID;
	//unsigned int    m_fulldstID;	
	unsigned int    m_count;	
	CTimer          m_timer;
	CTimer          m_ptimer;
	CTimer		    m_timeout;
	unsigned char   m_seqNo;
	unsigned char*  m_header;
	unsigned char*  m_csd1;
	unsigned char*  m_csd2;
	unsigned char*  m_csd3;
	WXSI_STATUS     m_status;
	unsigned int    m_start;
	std::vector<CReflector*> m_category;
	bool                 m_makeUpper;	
	std::string     m_search;
	bool            m_busy;
	CTimer          m_busyTimer;
	CStopWatch      m_txWatch;
	CRingBuffer<unsigned char> m_bufferTX;
	unsigned char 		 m_type;
	unsigned int         m_number;
	unsigned char        m_news_source[5];
	std::string          m_source;
	unsigned char		 m_serial[6];
	unsigned char 		 m_talky_key[5];
	WXPIC_STATUS		 m_picture_state;
	unsigned int 		m_offset;
	unsigned int 	     m_pcount;
	bool			m_end_picture;
	bool			error_upload;
	bool			m_enable;
//	bool            m_noChange;
	CYSFNetwork*    m_ysfNetwork;
	bool 			m_no_store_picture;
	bool 		    m_sendNetwork;
	unsigned int 	m_last_news;
	FILE *	        m_ambefile;
	CStopWatch       m_ysfWatch;
	CModeConv *     m_conv;

	WX_STATUS processConnect(const unsigned char* source, const unsigned char* data);
	void processDX(const unsigned char* source);
	void processAll(const unsigned char* source, const unsigned char* data);
	
	void processCategory(const unsigned char* source, const unsigned char* data);
	bool processListDown(const unsigned char* source, const unsigned char* data);
	WX_STATUS processGetMessage(const unsigned char* source, const unsigned char* data);
	WX_STATUS processUploadMessage(const unsigned char* source, const unsigned char* data, unsigned int gps);
	WX_STATUS processUploadPicture(const unsigned char* source, const unsigned char* data, unsigned int gps);
	void processPictureACK(const unsigned char* source, const unsigned char* data);
	void processDataPicture(const unsigned char* data, unsigned int size);
	bool processNews(const unsigned char* source, const unsigned char* data);
	void processVoiceACK();
	void sendDXReply();
	void sendAllReply();
	void sendLocalNewsReply();
	void sendNewsReply();	
	void sendSearchReply();
	void sendSearchNotFoundReply();
	void sendCategoryReply();
	void sendListReply();
	void sendGetMessageReply();
	void sendUploadReply(bool);
	void sendPictureBegin();
	void sendPictureData();
	void sendPictureEnd();
	void sendConnectReply();
	void sendDisconnectReply();	
	void sendAMBEMode1();

	void createReply(const unsigned char* data, unsigned int length, const char* dst_callsign);
	void writeData(const unsigned char* data);
	unsigned char calculateFT(unsigned int length, unsigned int offset) const;
	void makeConnect();	
	void makePacket(CYSFNetwork* ysfNetwork, unsigned char *data, unsigned int length);
	void makeEndPicture(void);
	bool NewsForMe(const unsigned char* data, const unsigned int offset);
};

#endif
