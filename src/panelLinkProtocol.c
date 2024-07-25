/**
 * panel-link_protocol.c
 *
 * Panel-Link Protocol Implement
 *  - Weidong Zhou
 *
 * This is a Panel-Link protocol implementation for BeadaPanel USB Graphics project.
 *
 * Features
 *  - Implements Panel-Link package per protocol spec.
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
#include "panelLinkProtocol.h"

#define LOGW(fmt, ...) do {;} while(0)

/* Define for pack type, per PanelLink protocol. */
#define PL_TYPE_START            1
#define PL_TYPE_END               2
#define PL_TYPE_RESET            3

#define PL_VERSION                 1
#define PL_FMT_STR_LEN         256

#pragma pack(push) //�������״̬
#pragma pack(1)   // 1 bytes����

static const char protocol_str[] = "PANEL-LINK";
const char raw_video_str[] = "video/x-raw, format=BGR16, height=480, width=800, framerate=0/1";

typedef struct _PANELLINK_STREAM_TAG {
	unsigned char protocol_name[10];
	unsigned char version;
	unsigned char type;
	unsigned char fmtstr[PL_FMT_STR_LEN];
	unsigned short checksum16;
} PANELLINK_STREAM_TAG;

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

static inline int packageDummyPL(unsigned char * data,
                                 unsigned char value)
{   	
    PANELLINK_STREAM_TAG * pTemp = (PANELLINK_STREAM_TAG *)data;
	
    pTemp->type = value;
    pTemp->version = PL_VERSION;
    memcpy(pTemp->protocol_name, protocol_str, strlen(protocol_str));

    // add checksum
    pTemp->checksum16 = checksum16((unsigned short *)data, (sizeof(PANELLINK_STREAM_TAG) - 2) / 2);
    return 0;
}

// - fillPLStart -
//   len 
//       in -  length of buffer
//       out - length of PL payload
//   fmt
//       in - format string for PL start package, NULL if no need of a format steam.
//
int fillPLStart(unsigned char * data,
                   unsigned int * len,
                   const char* fmt)
{    	
	size_t length;

    // check length of data       	
    if (*len < sizeof(PANELLINK_STREAM_TAG))
    	return -1;
    	
    PANELLINK_STREAM_TAG * pTemp = (PANELLINK_STREAM_TAG *)data;

    memset(pTemp->fmtstr, 0, PL_FMT_STR_LEN);    
    if (fmt != NULL) {
        // length of format string should less than PL_FMT_STR_LEN
        if ((length = strlen((const char*)fmt)) < PL_FMT_STR_LEN) {
            memcpy(pTemp->fmtstr, fmt, strlen((const char*)fmt));
        }
        else 
            return -2;
    }

	*len = sizeof(PANELLINK_STREAM_TAG);
    return (packageDummyPL(data, PL_TYPE_START));
}

// - fillPLEnd -
//   len 
//       in -  length of buffer
//       out - length of PL payload
//
int fillPLEnd(unsigned char * data,
                 unsigned int * len)
{
    // check length of data       	
    if (*len < sizeof(PANELLINK_STREAM_TAG))
    	return -1;

    *len =  sizeof(PANELLINK_STREAM_TAG);   	
    return (packageDummyPL(data, PL_TYPE_END));
}

// - fillPLReset -
//   len 
//       in -  length of buffer
//       out - length of PL payload
//
int fillPLReset(unsigned char * data,
                   unsigned int * len)
{ 
    // check length of data       	
    if (*len < sizeof(PANELLINK_STREAM_TAG))
    	return -1;

    *len =  sizeof(PANELLINK_STREAM_TAG);   	    	
    return (packageDummyPL(data, PL_TYPE_RESET));    
}
