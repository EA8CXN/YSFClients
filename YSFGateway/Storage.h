/*
*   Copyright (C) 2016,2017 by Jonathan Naylor G4KLX
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

#if !defined(STORAGE_H)
#define	STORAGE_H

#include "ModeConv.h"

#include <string>


typedef struct {
	char type[4]; // record type: '1,T01' is message, '2,P04' is picture, '3,V01' is voice, '4,E' is voice emergency 
	unsigned int number;  // message number
	char text[81];  // message text ASCII null terminated
	char subject[17];  // subject of message
	char to[6];
	char callsign[11];  // off: 0x1E callsign (0xFF fill)
	char token[7];
	char time_send[13];   // off: 0x34  
	char time_recv[13];   // off: 0x3A
	char gps_pos[19];
} wiresx_record;

class CWiresXStorage {
public:
	CWiresXStorage(std::string path_news);
	~CWiresXStorage();
	
	unsigned int GetList(unsigned char *, unsigned int , unsigned char * , unsigned int);
	unsigned int GetMessage(unsigned char *,unsigned int,unsigned char *);
	void StoreTextMessage(const unsigned char*, const unsigned char*, unsigned int);
	void StorePicture(const unsigned char*, const unsigned char*, unsigned int);	
	std::string StoreVoice(unsigned const char*, const char*, unsigned int news_channel, unsigned int);
	void AddPictureData(const unsigned char *, unsigned int, unsigned char *);
	unsigned int GetPictureHeader(unsigned char *,unsigned int, unsigned char *);
	unsigned int GetPictureData(unsigned char *,unsigned int);
	unsigned char GetPictureSeq();
	unsigned int GetSumCheck();
	void PictureEnd(bool);
    void VoiceEnd(unsigned int);
	void UpdateReflectorType(unsigned int type);
	
private:
	std::string 	m_callsign;
	std::string 	m_index_file;
	unsigned int    m_number;
	unsigned int 	m_size;
	unsigned char   m_seq;
	FILE*		    m_picture_file;
	unsigned int    m_sum_check;
	char 		    m_source[6];
	wiresx_record   *m_reg_picture;
	wiresx_record   *m_reg_voice;
	std::string 	m_picture_name;
	unsigned int    picture_final_size;
	std::string		m_newspath;
	char			m_reflector_type[4];
	
	void UpdateIndex(wiresx_record *);

};

#endif
