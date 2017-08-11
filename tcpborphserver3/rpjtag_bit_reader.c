/*
 * Raspberry Pi JTAG Programmer using GPIO connector
 * Version 0.3 Copyright 2013 Rune 'Krelian' Joergensen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "rpjtag_bit_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include "rpjtag_stateMachine.h"
#include "rpjtag_io.h"

int read_status(int iregs, char* buffer)
{
    // Configuration packets for get status
    // see Xilinx UG470
    // Note: commands go into shift IR msb last, and shift DR msf *first*
    // 0xAA995566
    // 0x20000000
    // 0x2800E001
    // 0x20000000
    // 0x20000000
    SelectShiftIR();
    send_cmdWord_msb_last(CFG_IN_CMD,1,iregs);
    ExitShift();
    SelectShiftDR();
    send_cmdWord_msb_first(0xAA995566,0,32);
    send_cmdWord_msb_first(0x20000000,0,32);
    send_cmdWord_msb_first(0x2800E001,0,32);
    send_cmdWord_msb_first(0x20000000,0,32);
    send_cmdWord_msb_first(0x20000000,1,32);
    ExitShift();
    SelectShiftIR();
    send_cmdWord_msb_last(CFG_OUT_CMD,1,iregs);
    ExitShift();
    SelectShiftDR();
    jtag_read_data(buffer, 32);
    ExitShift();
    fprintf(stderr, "buffer contents %d %d %d %d\n",buffer[0], buffer[1], buffer[2], buffer[3]);
    return 0;
}


//remember last device is always the one to send IR bits to first
int ProgramDevice(int iregs, unsigned char * buffer, int n_bytes)
{
	syncJTAGs();


    // Deprogram FPGA
	SelectShiftIR();
    send_cmdWord_msb_last(JPROGRAM_CMD,1,iregs);
    ExitShift();

    char dout[4];
    read_status(iregs, dout);
    fprintf(stderr, "Dout[1] reads %d\n", dout[1]);
    while ((dout[1] & (1<<3)) == 0) //check for init_complete
    {
        fprintf(stderr, "Dout[1] reads %d\n", dout[1]);
        read_status(iregs, dout);
    }

    // Issue Program command FPGA
	SelectShiftIR();
    send_cmdWord_msb_last(CFG_IN_CMD,1,iregs);
    ExitShift();

    // Prepare to load data
    SelectShiftDR();

	int n, m;

    int offset = 0;
    // The first word is a dummy...
	for(n = 0+offset; n < 4+offset; n++)
    {
        if (buffer[n] != 0xFF)
		{
			fprintf(stderr, "Error in bitfile, no Dummy Word at start\n");
            for(m=0; m<128; m++)
            {
                fprintf(stderr, "Value %d: %d\n", m, buffer[m]);
            }
			exit(1);

		}
    }

    // Now send all the data except the last byte. Separate this, because
    // the intermediate bytes don't need a tms strobe at their end
	for(n = 4+offset; n < n_bytes-1; n++) //send all but the last byte
	{
	    send_byte_no_tms(buffer[n]);
		if((n % 1048576) == 0)
        {
            fprintf(stdout,"%.2f complete\n",(100.*n)/n_bytes);
        }
	}

    // send last byte with a tms strobe on the final bit
    send_byte(buffer[n++],1);
    ExitShift();

	//Last bit has TMS 1, so we are in EXIT-DR
	fprintf(stderr, "\nProgrammed %d bytes\n", n);

    for(n=0; n<32; n++)
    {
        read_status(iregs, dout);
        if((dout[0] & 1) != 0) { //crc fail
            fprintf(stderr, "CRC failed! returning -2, %d,%d\n", dout[0], dout[0]&1);
            return -2; 
        }
        if((dout[1] & (1<<6)) != 0) { // done
            return 0;
            fprintf(stderr, "returning 0, %d, %d\n", dout[1], dout[1]&(1<<6));
        }
        fprintf(stderr, "No CRC fail, but done not yet asserted. Checking again %d, %d\n", dout[1], dout[1]&(1<<6));
    }
    fprintf(stderr, "No CRC fail, but done not asserted after 32 checks. Giving up %d, %d\n", dout[1], dout[1]&(1<<6));

    return -1;
}


void checkUserId(int x)
{
	syncJTAGs();
	SelectShiftIR();

	send_cmdWord_msb_last(0x3FC8,1,14); //11111111 001000

	send_cmd(0,1);
	send_cmd(0,1);
	send_cmd(0,0);
	send_cmd(0,0);

	int data;
	jtag_read_data((char *)&data,32);
	if(!x)
		fprintf(stderr,"\nUserID Before programming: %08X",data);
	else
		fprintf(stderr,"\nUserID After programming: %08X",data);
	send_cmd(0,1);
	send_cmd(0,0);
	send_cmd(0,0); //Run-Test/Idle
}

int GetSegmentLength(int segment, int segmentCheck, FILE *f)
{
	//Get Next Segment
	segment = fgetc(f);
	if(segment != segmentCheck)
	{
		fprintf(stderr, "Error in header segment: %d, should be %d\n",segment,segmentCheck);
		exit(1);
	}

	return ((fgetc(f) << 8) + fgetc(f)); //Lenght of segment
}
