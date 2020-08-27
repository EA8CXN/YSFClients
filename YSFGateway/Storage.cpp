/*
*   Copyright (C) 2016,2017 by Jonathan Naylor G4KLX
*   Copyright (C) 2019,2020 by Manuel Sanchez EA7EE
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
#include "unistd.h"
#include "Storage.h"
#include "WiresX.h"
#include "YSFPayload.h"
#include "Reflectors.h"
#include "YSFFICH.h"
#include "Utils.h"
#include "Sync.h"
#include "CRC.h"
#include "Log.h"

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cctype>	

#define MAX_PATH_NAME 100U
						
CWiresXStorage::CWiresXStorage(std::string path_news) :
m_callsign(),
m_number(1)
{
	m_picture_file = NULL;
	m_reg_picture = NULL;
	if (path_news.empty()) m_newspath = std::string("/tmp/news");
	else m_newspath = path_news;
}

CWiresXStorage::~CWiresXStorage()
{

}

void CWiresXStorage::UpdateReflectorType(unsigned int type) {
	switch (type) {
		case YSF:
			strcpy(m_reflector_type,"YSF");
			break;
		case DMR:
			strcpy(m_reflector_type,"DMR");
			break;
		case FCS:
			strcpy(m_reflector_type,"FCS");
			break;
		case P25:
			strcpy(m_reflector_type,"P25");
			break;
		case NXDN:
			strcpy(m_reflector_type,"NXD");
			break;			
		default:
			strcpy(m_reflector_type,"YSF");
			break;								
	}
}

void CWiresXStorage::UpdateIndex(wiresx_record* reg)
{
	FILE *file;
	char record[180U];  // records are 83 bytes long
	char index_str[MAX_PATH_NAME];
	char tmp[6];
	char destino[6];
	struct stat buffer;
	unsigned int number;
	int status;
	
	number=0;
	if (stat (m_newspath.c_str(), &buffer) != 0) {
		status = mkdir(m_newspath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (status != 0) {
				::LogMessage("Cannot create News Directory.");
				return;		
		}
	}
	
	::memcpy(destino,reg->to,5);
	destino[5]=0;
	::strcpy(m_source,destino);
	::sprintf(index_str,"%s/%s%s",m_newspath.c_str(),m_reflector_type,destino);
	
	if (stat (index_str, &buffer) != 0) {
		status = mkdir(index_str, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (status != 0) { 
				::LogMessage("Cannot create news directory for destination: %s%s.",m_reflector_type,destino);
				return;		
		}
	}
	
	::strcat(index_str,"/INDEX.DAT");
	if (stat (index_str, &buffer) == 0) {
		::LogMessage("Found index file for %s%s.",m_reflector_type,destino);
		file = fopen(index_str,"ab");
	}
	else {
		::LogMessage("Created index file for %s%s.",m_reflector_type,destino);		
		file = fopen(index_str,"wb");
		number=1U;
	}	
	if (!file) {
		LogMessage("Error reading index file: %s",index_str);
		return;
	}
	if (number<1) number=(buffer.st_size/83U)+1U;
	m_number=number;
	
	LogMessage("Updating record n:%05u, type %c.",number, reg->type[0]);
	::memset(tmp,0,6U);
	::memcpy(record,reg->gps_pos,18U);
	::memcpy(record+18U,reg->token,6U);
	::memcpy(record+24U,reg->time_recv,12U);	
	::sprintf(tmp,"%05u",number%100000);
	::memcpy(record+36U,tmp,5U);
	::memcpy(record+41U,reg->type,3U);
	::memcpy(record+44U,reg->time_send,12U);
	::memcpy(record+56U,reg->callsign,10U);
	::memcpy(record+66U,reg->subject,16U);
	record[82U]=0x0D;
	fwrite(record,1,83U,file);
	fclose(file);
	reg->number=number;
	
	if (reg->type[0]=='T') {
		::sprintf(index_str,"%s/%s%s/%05u.DAT",m_newspath.c_str(),m_reflector_type,destino,number);	
		file = fopen(index_str,"wb");
		if (!file) {
			LogMessage("Error writing message file: %s",index_str);
			return;
		}
		::memcpy(record,reg->callsign,10U);
		::memcpy(record+10U,reg->time_send,12);	
		::memcpy(record+22U,reg->gps_pos,18);
		::memcpy(record+40U,reg->text,80);	
		record[120U]=0x0D;	
		fwrite(record,1,121U,file);
		fclose(file);
	}
}

void CWiresXStorage::StorePicture(unsigned const char* data, unsigned const char* source, unsigned int gps)
{
	char index_str[MAX_PATH_NAME];
	char destino[6];
	struct stat buffer;
	unsigned int number,off;
	int status;
	
	picture_final_size=0;
	number=1;
	if (stat (m_newspath.c_str(), &buffer) != 0) {
		status = mkdir(m_newspath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (status != 0) {
				::LogMessage("Cannot create News Directory.");
				return;		
		}
	}
	
	if (m_reg_picture) delete m_reg_picture;
	m_reg_picture = new wiresx_record;
	
	if (gps) {
		::memcpy(m_reg_picture->gps_pos,data,18U);
		off=18U;
	}
	else {
		::memset(m_reg_picture->gps_pos,0,18U);
		off=0;
	}
	
	::memcpy(destino,data+off+30U,5U);
	destino[5]=0;	
	::sprintf(index_str,"%s/%s%s",m_newspath.c_str(),m_reflector_type,destino);
	
	if (stat (index_str, &buffer) != 0) {
		status = mkdir(index_str, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (status != 0) { 
			::LogMessage("Cannot create news directory for destination: %s%s.",m_reflector_type,destino);
			return;		
		}
	}
	
	::strcat(index_str,"/INDEX.DAT");
	if (stat (index_str, &buffer) == 0) number=(buffer.st_size/83U)+1U;
	
	::sprintf(m_reg_picture->callsign,"%10.10s",source);

	::memcpy(m_reg_picture->time_recv,data+off+6U,12U);
	::memcpy(m_reg_picture->time_send,data+off+18U,12U);
	::memcpy(m_reg_picture->to,data+off+30U,5U);

	::memcpy(m_reg_picture->subject,data+off+45U,16U);
	::memcpy(m_reg_picture->token,data+off,6U);

	::sprintf(index_str,"%s/%s%s/%05u.JPG",m_newspath.c_str(),m_reflector_type,destino,number);
	std::string file_name(index_str);
	m_picture_name = file_name;
	m_picture_file = fopen(index_str,"wb");
	if (!m_picture_file) {
		LogMessage("Error writing jpg file: %s",index_str);
		m_picture_file = NULL;
		return;
	}	
}

void CWiresXStorage::AddPictureData(const unsigned char *data, unsigned int size, unsigned char *source)
{	
	if (m_picture_file) {	
		if (size>771U) {
			fwrite(data,1,250U,m_picture_file);			
			fwrite(data+251U,1,259U,m_picture_file);	
			fwrite(data+511U,1,259U,m_picture_file);
			fwrite(data+771U,1,size-771U,m_picture_file);
		}	else if (size>511U) {
			fwrite(data,1,250U,m_picture_file);			
			fwrite(data+251U,1,259U,m_picture_file);	
			fwrite(data+511U,1,size-511U,m_picture_file);
		} 	else if (size>251U) {
			fwrite(data,1,250U,m_picture_file);			
			fwrite(data+251U,1,size-251U,m_picture_file);		
		} 	else fwrite(data,1,size,m_picture_file);
	}
	if ((size<1027U) && m_picture_file) {
		picture_final_size=ftell(m_picture_file);
		fclose(m_picture_file);
		m_picture_file = NULL;		
	}
}

unsigned int CWiresXStorage::GetList(unsigned char *data, unsigned int type, unsigned char *source, unsigned int start)
{ 
	FILE *file;
	char index_str[MAX_PATH_NAME];	
	char record[83U];  // records are 83 bytes long
	char f_type[3U];
	unsigned int items,n,offset,count;

	items=0;count=0;
	offset=15U;
	
	::sprintf((char*)data, "%02u", items+1U);
	::memcpy((char*)(data+2U),source,5U);	
	::sprintf((char*)(data+7U), "     %02u",items);
	*(data+14U) = 0x0DU;
	
	char tmp[6];
	::memcpy(tmp,source,5U);
	tmp[5]=0;
	::sprintf(index_str,"%s/%s%s/INDEX.DAT",m_newspath.c_str(),m_reflector_type,tmp);	
	file = fopen(index_str,"rb");
	if (!file) {
		LogMessage("Error getting index file: %s",index_str);
		return offset;
	}

	do {		
		n = fread(record,1,83U,file);
		if (n<83U) break;
		// get type
		::memcpy(f_type,record+41U,3U);
		if (((f_type[0]=='T') && (type=='1')) ||
		    ((f_type[0]=='P') && (type=='2')) ||
	        ((f_type[0]=='V') && (type=='3')) ||
			((f_type[0]=='E') && (type=='4'))) {
				if ((items>=start) && (count<20U)) {					
					::memcpy(data+offset,record+36U,47U);				
					offset+=47U;
					count++;
				}
				items++;
		}
	} while (n==83U);
	fclose(file);
		
	::sprintf((char*)(data), "%02u", count+1U);
	::memcpy((char*)(data+2U),source,5U);
	::sprintf((char*)(data + 7U), "     %02u",count);
	*(data+14U) = 0x0DU;	
	
	return offset;
	}	

void CWiresXStorage::StoreTextMessage(unsigned const char* data, unsigned const char* source, unsigned int gps)
{
	wiresx_record *reg;
	unsigned int off;

	reg = new wiresx_record;
	
	if (gps) {
		::memcpy(reg->gps_pos,data,18U);
		off=18U;
	}
	else {
		::memset(reg->gps_pos,0,18U);
		off=0;
	}
	::sprintf(reg->callsign,"%10.10s",source);

	::memcpy(reg->time_recv,data+off+6U,12U);
	::memcpy(reg->time_send,data+off+18U,12U);
	::memcpy(reg->to,data+off+30U,5U);

	::sprintf(reg->type,"T01");
	::memcpy(reg->text,data+off+45U,80U);
	::sprintf(reg->subject,"                ");	

	UpdateIndex(reg);
	delete reg;
}

unsigned int CWiresXStorage::GetMessage(unsigned char *data,unsigned int number, unsigned char *source) {
	FILE *file;
	char file_name[MAX_PATH_NAME];
	char index_str[MAX_PATH_NAME];
	char record[83U];  // records are 83 bytes long	
	char tmp[20U];
	struct stat buffer;
	unsigned int n,offset;
	
	::memcpy(tmp,source,5U);
	tmp[5]=0;
	::sprintf(file_name,"%s/%s%s/%05u.JPG",m_newspath.c_str(),m_reflector_type,tmp,number);
	if (stat (file_name, &buffer) < 0) {
		::sprintf(file_name,"%s/%s%s/%05u.AMB",m_newspath.c_str(),m_reflector_type,tmp,number);
		if (stat (file_name, &buffer) < 0) {		
			data[0]='T';
			::sprintf(file_name,"%s/%s%s/%05u.DAT",m_newspath.c_str(),m_reflector_type,tmp,number);
			if ((file=fopen(file_name,"rb"))==0) {
				LogMessage("Error getting data file: %s",file_name);
				return 0U;
			}
			::sprintf((char*)(data+5U), "%02u", 1U);
			::memcpy((char*)(data+7U),source,5U);
			::sprintf((char*)(data+12U),"     %05u",number);
			n=fread(data+22U,1,121U,file);
			if (n!=121U) {
				LogMessage("Error getting data file: %s",file_name);
				fclose(file);
				return 0U;		
			}
			fclose(file);
			return 138U;
			} 
		else {
			data[0]='V';
			strcpy((char *)(data+1),file_name);

			::sprintf(index_str,"%s/%s%s/INDEX.DAT",m_newspath.c_str(),m_reflector_type,tmp);	
			file = fopen(index_str,"rb");
			if (!file) {
				LogMessage("Error getting index file: %s",index_str);
				return 0U;
			}
			fseek(file,83U*(number-1),SEEK_SET);
			n = fread(record,1,83U,file);
			if (n<83U) {
				fclose(file);
				return 0U;
			}
			fclose(file);
			// callsign
			offset = 138U;
			memcpy(data+offset,record+56U,10U);
			offset+=10U;
			// date
			memcpy(data+offset,record+24U,12U);
			offset+=12U;
			// GPS
			memcpy(data+offset,record,18U);
			offset+=18U;
			// Subject
			memcpy(data+offset,record+66U,16U);

			return 94U;
		}			
	} else {
		data[0]='P';
		
		m_size=buffer.st_size;

		::sprintf(index_str,"%s/%s%s/INDEX.DAT",m_newspath.c_str(),m_reflector_type,tmp);	
		file = fopen(index_str,"rb");
		if (!file) {
			LogMessage("Error getting index file: %s",index_str);
			return 0U;
		}
		fseek(file,83U*(number-1),SEEK_SET);
		n = fread(record,1,83U,file);
		if (n<83U) {
				fclose(file);
				return 0U;
			}
		fclose(file);
		
		m_seq=0;
        	char tmp1[3];
		strcpy(tmp1,"01");		
		
		::memcpy(data+5U,tmp1,2U);
		//01
		::memcpy(data+7U,tmp,5U);
		//ID
		::memset(data+12U,0x20,5U);
		//NUMBER
		::sprintf(tmp,"%05u",number);
		::memcpy(data+17U,tmp,5U);
		//CALL
		::memcpy(data+22U,record+56U,10U);
		//TIME
		::memcpy(data+32U,record+44U,12U);
		//GPS
		::memcpy(data+44U,record,18U); 		
		//SUBJECT
		::memcpy(data+62U,record+66U,16U);
	    	data[78U]=0x0DU;
		if ((m_picture_file=fopen(file_name,"rb"))==0) {
			LogMessage("Error getting data file: %s",file_name);
			return 0U;
		}
		//return 79U;
		return 74U;
	}
	return 0U;
}

unsigned int CWiresXStorage::GetPictureHeader(unsigned char *data,unsigned int number, unsigned char *source) 
{
	FILE *file;
	char index_str[MAX_PATH_NAME];
	char record[83U];  // records are 83 bytes long	
	unsigned int n;
	char tmp[6];
	char cab[7]={0x50,0x00,0x01,0x30,0x00,0x00,0x00};

	m_sum_check=0;
	
	::memcpy(tmp,source,5U);
	tmp[5]=0;
	::sprintf(index_str,"%s/%s%s/INDEX.DAT",m_newspath.c_str(),m_reflector_type,tmp);	
	file = fopen(index_str,"rb");
	if (!file) {
		LogMessage("Error getting index file: %s",index_str);
		return 0U;
	}
	fseek(file,83U*(number-1),SEEK_SET);
	n = fread(record,1,83U,file);
	if (n<83U) return 0U;
	fclose(file);
	
	// Copy GPS
	::memcpy(data+5U,record,18U);
	cab[2]=m_seq+1;
	m_seq++;
	::memcpy(data+23U,cab,7U);
	data[30U]=(m_size>>8)&0xFF;
	data[31U]=m_size&0xFF;
	::sprintf((char *)(data+32U),"20");
	// TIME 1
	::memcpy(data+34U,record+24U,12U);
	// TALKY KEY
	char key[7]="HE5Gbv";

	::memcpy(data+46U,key,6U);	
	// file name
	::sprintf((char *)(data+52U),"%06u.jpg",number); 
	// gps
	::memcpy(data+62U,record,18U);
	//SUBJECT
	::memcpy(data+80U,record+66U,16U);
	
	return 91U;
}


unsigned int CWiresXStorage::GetPictureData(unsigned char *data,unsigned int offset) 
{
	unsigned int tam,n,i;
	char tmp[5]={0x50,0x00,0x01,0x00,0x00};

	tmp[2]=m_seq+1;
	m_seq++;	
	
	if (offset>(m_size-1024U))  {
		tam=m_size-offset;
		tmp[3]=(tam>>8)&0xFF;
		tmp[4]=tam&0xFF;
	}
	else tam=1024U;
	
	::memcpy(data,tmp,5U);
	n = fread(data+5U,1,tam,m_picture_file);
	
	for (i=0;i<n;i++)
		m_sum_check+=data[i+5U];
	
	return n;
}


unsigned char CWiresXStorage::GetPictureSeq(){
	return m_seq;
}

unsigned int CWiresXStorage::GetSumCheck(){
	return m_sum_check;
}

char tmp_gps[] = {0x53,0x37,0x51,0x52,0x58,0x50,0x7D,0x5B,0x70,0x6C,0x20,0x1C,0x5B,0x2F,0x20,0x20,0x20,0x20};

std::string CWiresXStorage::StoreVoice(unsigned const char *data, const char *source, unsigned int news_channel, unsigned int gps)
{
	char destino[6];
	char index_str[MAX_PATH_NAME];
	char temp[13];
	struct stat buffer;	
	unsigned int number;
//	unsigned int off;
	struct tm tm;
	time_t t;
	int status;	
	static std::string file_name;

	number=1;
	if (stat (m_newspath.c_str(), &buffer) != 0) {
		status = mkdir(m_newspath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (status != 0) {
				::LogMessage("Cannot create News Directory.");
				return std::string("");		
		}
	}
	
	if (m_reg_voice) delete m_reg_voice;
	m_reg_voice = new wiresx_record;
	
	// if (gps) {
	// 	::memcpy(m_reg_voice->gps_pos,tmp_gps,18U);
	// 	off=18U;
	// }
	// else {
	// 	::memset(m_reg_voice->gps_pos,0,18U);
	// 	off=0;
	// }
	
//	::memcpy(destino,data+off,5U);
//	destino[5]=0;	
	::memcpy(m_reg_voice->gps_pos,tmp_gps,18U);	
	snprintf(destino,6U,"%05d",news_channel);
	::sprintf(index_str,"%s/%s%s",m_newspath.c_str(),m_reflector_type,destino);
	
	if (stat (index_str, &buffer) != 0) {
		status = mkdir(index_str, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (status != 0) { 
			::LogMessage("Cannot create news directory for destination: %s%s.",m_reflector_type,destino);
			return std::string("");		
		}
	}
	
	::strcat(index_str,"/INDEX.DAT");
	if (stat (index_str, &buffer) == 0) number=(buffer.st_size/83U)+1U;
	
	::sprintf(m_reg_voice->callsign,"%10.10s",source);
	m_reg_voice->number=number;
	t = time(NULL);
	tm = *localtime(&t);
	sprintf(temp,"%02hu%02hu%02hu%02hu%02hu%02hu", (tm.tm_year + 1900)%100, 
		tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

	::memcpy(m_reg_voice->time_recv,temp,13U);
	::memcpy(m_reg_voice->time_send,temp,13U);
	::memcpy(m_reg_voice->to,destino,6U);
	::sprintf(m_reg_voice->subject,"Uploaded voice  ");
	
	//::memcpy(reg->subject,data+58U,10U);	
	//::memcpy(reg->text,data+off+45U,80U);	

	::sprintf(index_str,"%s/%s%s/%05u.AMB",m_newspath.c_str(),m_reflector_type,destino,number);
	file_name =std::string(index_str);
	
	return file_name;
}

void CWiresXStorage::VoiceEnd(unsigned int size) {
	if (m_reg_voice) {
		::LogMessage("Voice uploaded sucessfully.");		
		::sprintf(m_reg_voice->type,"V%02u",((size/1000U)+1)%100);
		UpdateIndex(m_reg_voice);
		delete m_reg_voice;
		m_reg_voice = NULL;
	}
}


void CWiresXStorage::PictureEnd(bool error) {
		
	if (!error) {
		::LogMessage("Picture uploaded sucessfully");
		::sprintf(m_reg_picture->type,"P%02u",((picture_final_size/1000U)+1)%100);
		UpdateIndex(m_reg_picture);
	}
	else {
		::LogMessage("Picture uploaded unsucessfully %s.",m_picture_name.c_str());		
		if (m_picture_file) fclose(m_picture_file);
		int ret = unlink(m_picture_name.c_str());
		::LogMessage("Unlink returns %d.",ret);	
	}
	
	if (m_reg_picture) delete m_reg_picture;
	m_reg_picture = NULL;	
}



