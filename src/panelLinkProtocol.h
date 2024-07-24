/**
 * panel-link_protocol.h
 *
 * panel-Link Protocol Implement
 *  - Weidong Zhou
 *
 * This is a panel-Link protocol implementation for USB Graphics project.
 *
 * Features
 *  - Implements panel-Link package per protocol spec.
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
#ifndef _PANEL_LINK_PROTOCOL_H
#define _PANEL_LINK_PROTOCOL_H

#ifdef __cplusplus
extern "C"
{
#endif

    int fillPLStart(unsigned char * data,
                                     unsigned int * len,
                                     const char * fmt);

    int fillPLEnd(unsigned char * data,
                                   unsigned int * len);

    int fillPLReset(unsigned char * data,
                                      unsigned int * len);

#ifdef __cplusplus
}  // extern C
#endif

#endif //_PANEL_LINK_PROTOCOL_H