#ifndef RPJTAG_IO_H
#define RPJTAG_IO_H

#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define PAGE_SIZE (4*1024)
#define BLOCK_SIZE (4*1024)

#define BCM2708_PERI_BASE        0xFE000000 // RPI 2-3 0x3F000000 // or for RPI 1 B+ 0x20000000
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */

// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x) or SET_GPIO_ALT(x,y)
#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))

#define GPIO_SET(g) *(gpio+7) = 1<<(g) // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR(g) *(gpio+10) = 1<<(g) // clears bits which are 1 ignores bits which are 0
#define GPIO_READ(g) (*(gpio+13) >> (g)) & 0x00000001
#define GPIO_READRAW *(gpio+13)

//Perspective is from Device connected, so TDO is output from device to input into rpi
#define JTAG_TMS 27 //PI ---> JTAG
#define JTAG_TDI 22 //PI ---> JTAG
#define JTAG_TDO 23 //PI <--- JTAG
#define JTAG_TCK 24 //PI ---> JTAG

//-D DEBUG when compiling, will make all sleeps last 0.5 second, this can be used to test with LED on ports, or pushbuttons
//Else sleeps can be reduced to increase speed
#ifdef DEBUG
#define WAIT 10000000 //aprox 0.5s
#else
#define WAIT 1000 //aprox 0.5us
#endif

int setup_io();
void close_io();
void reset_clk();
int read_jtag_tdo();
void send_cmd_no_tms(int iTDI);
void send_cmd(int iTDI,int iTMS);
void send_cmdWord_msb_first(unsigned int cmd, int lastBit, int bitoffset);
void send_cmdWord_msb_last(unsigned int cmd, int lastBit, int bitoffset);
void send_byte(unsigned char byte, int lastbyte);
void send_byte_no_tms(unsigned char byte);
void nop_sleep(long x);
void jtag_read_data(char* data,int iSize);

#endif
