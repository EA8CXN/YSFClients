/*
 *   Copyright (C) 2015,2016,2017 by Jonathan Naylor G4KLX
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

#if !defined(Defines_H)
#define	Defines_H

const unsigned char MODE_IDLE    = 0U;
const unsigned char MODE_DSTAR   = 1U;
const unsigned char MODE_DMR     = 2U;
const unsigned char MODE_YSF     = 3U;
const unsigned char MODE_P25     = 4U;
const unsigned char MODE_CW      = 98U;
const unsigned char MODE_LOCKOUT = 99U;
const unsigned char MODE_ERROR   = 100U;

const unsigned char TAG_HEADER   = 0x00U;
const unsigned char TAG_DATA     = 0x01U;
const unsigned char TAG_LOST     = 0x02U;
const unsigned char TAG_EOT      = 0x03U;
const unsigned char TAG_NODATA   = 0x04U;
const unsigned char TAG_HEADERV1 = 0x05U;
const unsigned char TAG_DATAV1   = 0x06U;
const unsigned char TAG_EOTV1    = 0x07U;
const unsigned char TAG_DCH      = 0x08U;
const unsigned char TAG_BULK     = 0x09U;

enum HW_TYPE {
	HWT_MMDVM,
	HWT_DVMEGA,
	HWT_MMDVM_ZUMSPOT,
	HWT_MMDVM_HS_HAT,
	HWT_MMDVM_HS,
	HWT_UNKNOWN
};

enum RPT_RF_STATE {
	RS_RF_LISTENING,
	RS_RF_LATE_ENTRY,
	RS_RF_AUDIO,
	RS_RF_DATA,
	RS_RF_REJECTED,
	RS_RF_INVALID
};

enum RPT_NET_STATE {
	RS_NET_IDLE,
	RS_NET_AUDIO,
	RS_NET_DATA
};

enum B_STATUS {
	BS_NO_DATA,
	BS_DATA,
	BS_MISSING
};

#endif
