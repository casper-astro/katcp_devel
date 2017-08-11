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

#include "rpjtag_stateMachine.h"
#include "rpjtag_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

int i;

void syncJTAGs()
{
    fprintf(stderr, "resetting clk\n");
    reset_clk(); //in case clock is currently high
    fprintf(stderr, "done\n");
    for(i=0;i<5;i++) // reset JTAG chain
    {
        fprintf(stderr, "sending command\n");
        send_cmd(0,1);
    }
    send_cmd(0,0);
}


// Use this function only when the current state is IDLE
void SelectShiftDR()
{
    send_cmd(0,1);
    send_cmd(0,0);
    send_cmd(0,0);
}

// Use this function only when the current state is IDLE
void SelectShiftIR()
{
    send_cmd(0,1);
    send_cmd(0,1);
    send_cmd(0,0);
    send_cmd(0,0);
}

// Use this function only from the Exit1-DR/IR states
void ExitShift()
{
    send_cmd(0,1);
    send_cmd(0,0);
}
