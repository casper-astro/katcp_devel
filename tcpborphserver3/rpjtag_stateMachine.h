#ifndef RPJTAG_STATEMACHINE_H
#define RPJTAG_STATEMACHINE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "rpjtag_io.h"

void syncJTAGs();
void SelectShiftDR();
void SelectShiftIR();
void ExitShift();

#endif
