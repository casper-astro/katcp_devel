#ifndef RPJTAG_BIT_READER_H
#define RPJTAG_BIT_READER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include "rpjtag_stateMachine.h"
#include "rpjtag_io.h"

// JTAG command codes (xilinx)
#define JPROGRAM_CMD 0x0b
#define CFG_IN_CMD 0x05
#define CFG_OUT_CMD 0x04

// Binary file size
// 6692572 for Kintex7 160t
#define FPGA_BIN_SIZE 6692572

extern struct Device
{
	char* DeviceName; //Device name
	int idcode:32; //Device Id Code
	int dIRLen; //Device IR Length
	int dBSRLen; //Device Boundary Scan Length, currently on v0.3 not used
	FILE* filePtr; //Pointer to our BDSL, in case we need it
} device_data[32]; //32 Devices

extern struct bitFileInfo
{
	int BitFile_Type;
	char* DesignName;
	char* DeviceName;
	int Bitstream_Length;
} bitfileinfo;

void checkStatusReg();
int read_status();
int ProgramDevice(int iregs, unsigned char * buffer, int n_bytes);
void checkUserId(int x);
int GetSegmentLength(int segment, int segmentCheck, FILE *f);

#endif
