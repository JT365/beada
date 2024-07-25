/**
 * statusLinkProtocol.c
 *
 * Status-Link Protocol Implement
 *  - Weidong Zhou
 *
 * This is a Status-Link protocol implementation for USB Graphics project.
 *
 * Features
 *  - Implements Status-Link package per protocol spec.
 *  - No dynamic allocations.
 *
 * This library is coded in the spirit of the stb libraries and mostly follows
 * the stb guidelines.
 *
 * It is written in C99. And depends on the C standard library.
 * Works with C++11
 *
 *
 */

#include <linux/module.h>
#include "statusLinkProtocol.h"

#define LOGW(fmt, ...) do {;} while(0)

/* Define for pack type, per StatusLink protocol. */
#define TYPE_GET_PANEL_INFO      1
#define TYPE_PANELLINK_RESET     2
#define TYPE_SET_BACKLIGHT         3
#define TYPE_PUSH_STORAGE        4
#define TYPE_GET_TIME                   5
#define TYPE_SET_TIME                   6
#define TYPE_PL_RESET     7
#define TYPE_ANIMATION_RESET     8

#define SL_VERSION                        1

static const char protocol_str[] = "STATUS-LINK";

#pragma pack(push) //�������״̬
#pragma pack(1)   // 1 bytes����

typedef struct _STATUSLINK_TAG {
	unsigned char protocol_name[11];
	unsigned char version;
	unsigned char type;
	unsigned char reserved1;
	unsigned short sequence_number;
	unsigned short length;
	unsigned short checksum16;
} STATUSLINK_TAG;

typedef struct _STATUSLINK_INFO_PACK {
	STATUSLINK_TAG header;
	STATUSLINK_INFO value;
}  STATUSLINK_INFO_PACK;

typedef struct _STATUSLINK_BL_PACK {
	STATUSLINK_TAG header;
	unsigned char value;
}  STATUSLINK_BL_PACK;

typedef struct _STATUSLINK_TEMP_PACK {
	STATUSLINK_TAG header;
	unsigned char value[256];
}  STATUSLINK_TEMP_PACK;

#pragma pack(pop)//�ָ�����״̬



static unsigned short checksum16(unsigned short *buf, int nword)
{
	unsigned long sum;

	for (sum = 0; nword > 0; nword--)
		sum += *buf++;
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);

	return (unsigned short)~sum;
}

static inline int packageDummySL(unsigned char * data,
                              unsigned char value)
{     	
    STATUSLINK_TAG * pTemp = (STATUSLINK_TAG *)data;
	
    pTemp->type = value;
    pTemp->version = SL_VERSION;
    memcpy(&pTemp->protocol_name, protocol_str, strlen(protocol_str));
    
    // add checksum
    pTemp->checksum16 = checksum16((unsigned short *)data, (sizeof(STATUSLINK_TAG) - 2) / 2);
    return 0;
}

// - packageResetAN -
//   len 
//       in -  length of buffer
//       out - length of SL payload
//
int fillSLResetAN(unsigned char * data,
	unsigned int * len)
{
	// check length of data       	
	if (*len < sizeof(STATUSLINK_TAG))
		return -1;

	STATUSLINK_TAG * pTemp = (STATUSLINK_TAG *)data;
	pTemp->length = sizeof(STATUSLINK_TAG);

	*len = sizeof(STATUSLINK_TAG);
	return (packageDummySL(data, TYPE_ANIMATION_RESET));

}

// - packageResetPLSL -
//   len 
//       in -  length of buffer
//       out - length of SL payload
//
int fillSLResetPL(unsigned char * data,
	unsigned int * len)
{
	// check length of data       	
	if (*len < sizeof(STATUSLINK_TAG))
		return -1;

	STATUSLINK_TAG * pTemp = (STATUSLINK_TAG *)data;
	pTemp->length = sizeof(STATUSLINK_TAG);

	*len = sizeof(STATUSLINK_TAG);
	return (packageDummySL(data, TYPE_PL_RESET));
}

// - packageResetSL -
//   len 
//       in -  length of buffer
//       out - length of SL payload
//
int fillSLReset(unsigned char * data,
	unsigned int * len)
{
	// check length of data       	
	if (*len < sizeof(STATUSLINK_TAG))
		return -1;

	STATUSLINK_TAG * pTemp = (STATUSLINK_TAG *)data;
	pTemp->length = sizeof(STATUSLINK_TAG);

	*len = sizeof(STATUSLINK_TAG);
	return (packageDummySL(data, TYPE_PANELLINK_RESET));
}

// - packageGetInfoSL -
//   len 
//       in -  length of buffer
//       out - length of SL payload
//
int fillSLGetInfo(unsigned char * data,
	unsigned int * len)
{
	// check length of data       	
	if (*len < sizeof(STATUSLINK_TAG))
		return -1;

	STATUSLINK_TAG * pTemp = (STATUSLINK_TAG *)data;
	pTemp->length = sizeof(STATUSLINK_TAG);

	*len = sizeof(STATUSLINK_TAG);
	return (packageDummySL(data, TYPE_GET_PANEL_INFO));
}

// - depackGetInfoSL -
//   len 
//       in -  length of buffer
//       out - length of SL payload
//   value
//       in - value of backlight level.
//
int retrivSLGetInfo(unsigned char * data,
	unsigned int len,
	STATUSLINK_INFO * value)
{
	// check length of data       	
	if (len < sizeof(STATUSLINK_INFO_PACK))
		return -1;

	STATUSLINK_INFO_PACK * pTemp = (STATUSLINK_INFO_PACK *)data;
	*value = pTemp->value;
	return 0;
}

// - packageSetBLSL -
//   len 
//       in -  length of buffer
//       out - length of SL payload
//   value
//       in - value of backlight level.
//
int fillSLSetBL(unsigned char * data,
                   unsigned int * len,
                   unsigned char value)
{
    // check length of data       	
    if (*len < sizeof(STATUSLINK_BL_PACK))
    	return -1;
    	
    STATUSLINK_BL_PACK * pTemp = (STATUSLINK_BL_PACK *)data;	
    pTemp->header.length = sizeof(STATUSLINK_BL_PACK);
    pTemp->value = value;    

    *len = sizeof(STATUSLINK_BL_PACK);
    return (packageDummySL(data, TYPE_SET_BACKLIGHT));
}
