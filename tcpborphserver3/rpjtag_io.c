#include "rpjtag_io.h"
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
//Credit for GPIO setup goes to http://elinux.org/RPi_Low-level_peripherals
//
// Set up a memory regions to access GPIO
//
int  mem_fd;
void *gpio_map;

// I/O access
volatile unsigned *gpio;

void tick_clk()
{
    GPIO_SET(JTAG_TCK);
    //nop_sleep(WAIT);
    GPIO_CLR(JTAG_TCK);
}

void close_io()
{
    munmap(gpio_map, BLOCK_SIZE);
}

int setup_io()
{
    /* open /dev/mem */
    if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0)
    {
        printf("can't open /dev/mem \n");
        return -1;
    }
    
    /* mmap GPIO */
    gpio_map = mmap(
        NULL,                //Any adddress in our space will do
        BLOCK_SIZE,          //Map length
        PROT_READ|PROT_WRITE,// Enable reading & writting to mapped memory
        MAP_SHARED,          //Shared with other processes
        mem_fd,              //File to map
        GPIO_BASE);

    close(mem_fd); //No need to keep mem_fd open after mmap
    
    if (gpio_map == MAP_FAILED)
    {
        printf("mmap error %d\n", (int)gpio_map);
        return -2;
    }
    
    // Always use volatile pointer!
    gpio = (volatile unsigned *)gpio_map;
    
    INP_GPIO(JTAG_TCK);
    INP_GPIO(JTAG_TMS);
    INP_GPIO(JTAG_TDI);
    INP_GPIO(JTAG_TDO); //Receive output from Device to rpi

    nop_sleep(WAIT);

    OUT_GPIO(JTAG_TDI); //Send data from rpi to Device
    OUT_GPIO(JTAG_TMS);
    OUT_GPIO(JTAG_TCK);
    nop_sleep(WAIT);

    return 0;
}

int read_jtag_tdo()
{
    return ( GPIO_READ(JTAG_TDO) ) ? 1 : 0;
}

int tdi=-1;

void send_cmd_no_tms(int iTDI)
{
    if(iTDI == 0)
    {
      if (tdi != 0)
      {
          GPIO_CLR(JTAG_TDI);
          tdi = 0;
      }
    }
    else
    {
        if (tdi != 1)
        {
            GPIO_SET(JTAG_TDI);
            tdi = 1;
        }
    }

    //nop_sleep(WAIT);
    GPIO_SET(JTAG_TCK);
    //nop_sleep(WAIT);
    GPIO_CLR(JTAG_TCK);
    //nop_sleep(WAIT);
}

//void set_pin(int pin, int val)
//{
//    if (val == 0)
//    {
//        GPIO_CLR(pin);
//    }
//    else
//    {
//        GPIO_SET(pin);
//    }
//}

void send_cmd(int iTDI,int iTMS)
{
    if(iTDI == 1)
    {
        GPIO_SET(JTAG_TDI);
        tdi = 1;
    }
    else
    {
        GPIO_CLR(JTAG_TDI);
        tdi = 0;
    }

    if(iTMS == 1)
    {
        GPIO_SET(JTAG_TMS);
    }
    else
        GPIO_CLR(JTAG_TMS);

    //nop_sleep(WAIT);
    tick_clk();
    //nop_sleep(WAIT);
}


void reset_clk()
{
    fprintf(stderr, "reseting clock at pin %d\n", JTAG_TCK);
    GPIO_CLR(JTAG_TCK);
    fprintf(stderr, "done\n");
}

//Mainly used for command words (CFG_IN)
void send_cmdWord_msb_first(unsigned int cmd, int lastBit, int bitoffset) //Send data, example 0xFFFF,0,20 would send 20 1's, with not TMS
{
    while(bitoffset--)
    {
        int x = ( cmd >> bitoffset) & 0x01;
        send_cmd(x,(lastBit==1 && bitoffset==0));
    }
}

//Mainly used for IR Register codes
void send_cmdWord_msb_last(unsigned int cmd, int lastBit, int bitoffset) //Send data, example 0xFFFF,0,20 would send 20 1's, with not TMS
{
    int i;
    for(i=0;i<bitoffset;i++)
    {
        int x = ( cmd >> i ) & 0x01;
        send_cmd(x,(lastBit==1 && bitoffset==i+1));
    }
}

void send_byte(unsigned char byte, int lastbyte) //Send single byte, example from fgetc
{
    int x;
        for(x=7;x>=0;x--)
        {
            send_cmd(byte>>x&0x01,( x==0) && (lastbyte==1));
        }
}

void send_byte_no_tms(unsigned char byte)
{
    //int x;
    //    for(x=7;x>=0;x--)
    //    {
    //        send_cmd_no_tms(byte>>x&0x01);
    //    }
    send_cmd_no_tms(byte&0x80);
    send_cmd_no_tms(byte&0x40);
    send_cmd_no_tms(byte&0x20);
    send_cmd_no_tms(byte&0x10);
    send_cmd_no_tms(byte&0x08);
    send_cmd_no_tms(byte&0x04);
    send_cmd_no_tms(byte&0x02);
    send_cmd_no_tms(byte&0x01);
}

//Does a NOP call in BCM2708, and is meant to be run @ 750 MHz
void nop_sleep(long x)
{
    while (x--) {
        asm("nop");
    }
}

void jtag_read_data(char* data,int iSize)
{
    if(iSize==0) return;
    int i, temp;
    memset(data,0,(iSize+7)/8);

    for(i=iSize-1; i>0; i--)
    {
        temp = read_jtag_tdo();
        send_cmd(0,0);
        data[i/8] |= (temp << (i & 7));
    }

    temp = read_jtag_tdo(); //Read last bit, while also going to EXIT
    send_cmd(0,1);
    data[0] |= temp;
}
