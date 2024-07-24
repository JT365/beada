/**
 * status-link_protocol.h
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

#ifndef _STATUS_LINK_PROTOCOL_H
#define _STATUS_LINK_PROTOCOL_H

#define MIN_Buffer_Size 512

#pragma pack(push) //�������״̬
#pragma pack(1)   // 1 bytes����

typedef struct _STATUSLINK_INFO {
	unsigned short firmware_version;
	unsigned char panellink_version;
	unsigned char statuslink_version;
	unsigned char hardware_platform;
	unsigned char os_version;
	unsigned char sn[64];
	unsigned short screen_resolution_x;
	unsigned short screen_resolution_y;
	unsigned int storage_size;
	unsigned char max_brightness;
	unsigned char current_brightness;
}  STATUSLINK_INFO;

#pragma pack(pop)//�ָ�����״̬

#ifdef __cplusplus
extern "C"
{
#endif

// - fillSLReset -
//   len 
//       in -  length of buffer
//       out - length of SL payload
//   value
//       in - value of backlight level.
//
int fillSLReset(unsigned char * data, unsigned int * len);

// - fillSLSetBL -
//   len 
//       in -  length of buffer
//       out - length of SL payload
//   value
//       in - value of backlight level.
//
int fillSLSetBL(unsigned char * data, unsigned int * len, unsigned char value);

// - fillSLSetTime -
//   len 
//       in -  length of buffer
//       out - length of SL payload
//   value
//       in - value of backlight level.
//
int fillSLSetTime(unsigned char * data, unsigned int * len, SYSTEMTIME * value);

// - fillSLGetInfo -
//   len 
//       in -  length of buffer
//       out - length of SL payload
//   value
//       in - value of backlight level.
//
int fillSLGetInfo(unsigned char * data, unsigned int * len);

// - retrivSLGetInfo -
//   len 
//       in -  length of buffer
//       out - length of SL payload
//   value
//       in - value of backlight level.
//
int retrivSLGetInfo(unsigned char * data, unsigned int len, STATUSLINK_INFO * info);

int fillSLResetPL(unsigned char * data, unsigned int * len);
int fillSLResetAN(unsigned char * data, unsigned int * len);

#ifdef __cplusplus
}  // extern C
#endif

#endif // _STATUS_LINK_PROTOCOL_H
