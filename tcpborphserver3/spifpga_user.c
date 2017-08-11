/*
 * Functions to issue CASPER-style
 * (as arbitrarily defined by Jack)
 * commands over SPI.
 * Uses the spidev driver. See spidev_test.c
 * for examples.
 */

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include "spifpga_user.h"

static uint32_t speed = MAX_SPEED;
static uint8_t bits = BITS;
static uint8_t mode;
static uint16_t delay = DELAY;

/* Write a single word to the FPGA */
int write_word(int fd, uint32_t addr, uint32_t val)
{
    //printf("trying to write %u to address %u of %d\n", val, addr, fd);

    struct fpga_spi_cmd *fcmd;
    struct fpga_spi_cmd *fresp;
    int spidev_ret, fpga_ret;

    fcmd = malloc(sizeof(struct fpga_spi_cmd));
    if (!fcmd)
    {
        printf("Failed to allocate fcmd\n");
        return -1;
    }

    fresp = malloc(sizeof(struct fpga_spi_cmd));
    if (!fresp)
    {
        printf("Failed to allocate fresp\n");
        return -1;
    }

	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long) fcmd,
		.rx_buf = (unsigned long) fresp,
		.len = sizeof(struct fpga_spi_cmd),
		.delay_usecs = delay,
		.speed_hz = speed,
		.bits_per_word = bits,
	};


    fcmd->cmd = 0x8F; //write command, all byte enables=1
    fcmd->addr = addr;
    fcmd->din = val;
    fcmd->dout = 0; //Dummy bytes while slave sends back data
    fcmd->resp = 0; //Dummy bytes while slave sends back data

    //printf("Sending SPI message\n");
	spidev_ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (spidev_ret < 1)
    {
		printf("can't send spi message\n");
        return spidev_ret;
    }

    //printf("FPGA return value: %u\n", fresp->resp);
    fpga_ret = fresp->resp;
    free(fcmd);
    free(fresp);
    if (fpga_ret == 143) {
        return 4;
    } else {
        return 0;
    }
}

/* Read multiple words from the FPGA */
int bulk_read(int fd, uint32_t start_addr, unsigned int n_bytes, uint32_t *buf)
{

    struct fpga_spi_cmd *fcmd, *fcmd_loop, *fresp, *fresp_loop;
    struct spi_ioc_transfer *tr, *tr_loop;
    int spidev_ret = 0;
    int fpga_ret = 0;
    int read_bytes = 0;
    unsigned int *buf_loop;

    int n_transfers = (n_bytes + BYTES_PER_WORD - 1) / BYTES_PER_WORD;
    int n_bursts = (n_transfers + MAX_BURST_SIZE - 1) / MAX_BURST_SIZE;

    int n_trans_per_buf;
    int n, m, tx_word_cnt=0, rx_word_cnt=0;

    if (n_bursts > 1)
    {
        n_trans_per_buf = MAX_BURST_SIZE;
    } else {
        n_trans_per_buf = n_transfers;
    }


    fcmd = calloc(n_trans_per_buf, sizeof(struct fpga_spi_cmd));
    if (!fcmd)
    {
        printf("Failed to allocate fcmd\n");
        return -1;
    }

    fresp = calloc(n_trans_per_buf, sizeof(struct fpga_spi_cmd));
    if (!fresp)
    {
        printf("Failed to allocate fresp\n");
        return -1;
    }
    tr = calloc(n_trans_per_buf, sizeof(struct spi_ioc_transfer));
    if (!tr)
    {
        printf("Failed to allocate tr\n");
        return -1;
    }

    int cs_change = 0;
    for (n=0; n<n_bursts; n++)
    {
        for (m=0, fcmd_loop=fcmd, fresp_loop=fresp, tr_loop=tr; m<n_trans_per_buf; m++, fcmd_loop++, fresp_loop++, tr_loop++)
        {
            if (tx_word_cnt == n_transfers)
            {
                break;
            }

            if (tx_word_cnt == n_transfers - 1)
            {
                cs_change = 0;
            }
            else if (m == n_trans_per_buf - 1)
            {
                cs_change = 0;
            }
            else
            {
                cs_change = 1;
            }

            fcmd_loop->cmd = 0x0F; //read command, all byte enables =1
            fcmd_loop->addr = start_addr + (tx_word_cnt * BYTES_PER_WORD);
            //din, dout and resp are already 0

            // Copy the messages to the transfer command buffer
            tr_loop->len = sizeof(struct fpga_spi_cmd);
            tr_loop->tx_buf = (unsigned long) fcmd_loop;
            tr_loop->rx_buf = (unsigned long) fresp_loop;
            tr_loop->delay_usecs = delay;
            tr_loop->speed_hz = speed;
            tr_loop->bits_per_word = bits;
            tr_loop->cs_change = cs_change;

            tx_word_cnt++;

        }

        //printf("Sending SPI message burst %d n_messages: %d\n", n, m);
	    spidev_ret = ioctl(fd, SPI_IOC_MESSAGE(m), tr);
    	if (spidev_ret < 1)
        {
		    printf("can't send spi message! (error %d)\n", spidev_ret);
            return spidev_ret;
        }
        else
        {
        }

        for (m=0, fresp_loop=fresp, buf_loop=buf; m<n_trans_per_buf; m++, fresp_loop++, buf_loop++)
        {
            //memcpy(buf + (rx_word_cnt++), &fresp_loop->dout, sizeof(unsigned int));
            //printf("readback value fresp_loop->dout: %u\n", fresp_loop->dout);

            *(buf + rx_word_cnt) = fresp_loop->dout;
            fpga_ret = fpga_ret | fresp_loop->resp;
            if (fresp_loop->resp == 143)
            {
                read_bytes += 4;
            }
            if (++rx_word_cnt == n_transfers)
            {
                break;
            }
        }
    }
    free(fcmd);
    free(fresp);
    free(tr);
    return read_bytes;
}

/* Write multiple words from the FPGA */
int bulk_write(int fd, uint32_t start_addr, unsigned int n_bytes, uint32_t *buf)
{

    struct fpga_spi_cmd *fcmd, *fcmd_loop, *fresp, *fresp_loop;
    struct spi_ioc_transfer *tr, *tr_loop;
    int spidev_ret = 0;
    int fpga_ret = 0;
    int written_bytes = 0;
    unsigned int *buf_loop;

    int n_transfers = (n_bytes + BYTES_PER_WORD - 1) / BYTES_PER_WORD;
    int n_bursts = (n_transfers + MAX_BURST_SIZE - 1) / MAX_BURST_SIZE;

    int n_trans_per_buf;
    int n, m, tx_word_cnt=0, rx_word_cnt=0;

    if (n_bursts > 1)
    {
        n_trans_per_buf = MAX_BURST_SIZE;
    } else {
        n_trans_per_buf = n_transfers;
    }


    fcmd = calloc(n_trans_per_buf, sizeof(struct fpga_spi_cmd));
    if (!fcmd)
    {
        printf("Failed to allocate fcmd\n");
        return -1;
    }

    fresp = calloc(n_trans_per_buf, sizeof(struct fpga_spi_cmd));
    if (!fresp)
    {
        printf("Failed to allocate fresp\n");
        return -1;
    }
    tr = calloc(n_trans_per_buf, sizeof(struct spi_ioc_transfer));
    if (!tr)
    {
        printf("Failed to allocate tr\n");
        return -1;
    }

    int cs_change = 0;
    for (n=0; n<n_bursts; n++)
    {
        for (m=0, buf_loop=buf, fcmd_loop=fcmd, fresp_loop=fresp, tr_loop=tr; m<n_trans_per_buf; m++, buf_loop++, fcmd_loop++, fresp_loop++, tr_loop++)
        {
            if (tx_word_cnt == n_transfers)
            {
                break;
            }

            if (tx_word_cnt == n_transfers - 1)
            {
                cs_change = 0;
            }
            else if (m == n_trans_per_buf - 1)
            {
                cs_change = 0;
            }
            else
            {
                cs_change = 1;
            }

            fcmd_loop->cmd = 0x8F; //write command, all byte enables =1
            fcmd_loop->addr = start_addr + (tx_word_cnt * BYTES_PER_WORD);
            fcmd_loop->din = *(buf + tx_word_cnt);
            //dout and resp are already 0

            // Copy the messages to the transfer command buffer
            tr_loop->len = sizeof(struct fpga_spi_cmd);
            tr_loop->tx_buf = (unsigned long) fcmd_loop;
            tr_loop->rx_buf = (unsigned long) fresp_loop;
            tr_loop->delay_usecs = delay;
            tr_loop->speed_hz = speed;
            tr_loop->bits_per_word = bits;
            tr_loop->cs_change = cs_change;

            tx_word_cnt++;

        }

        //printf("Sending SPI message burst %d n_messages: %d\n", n, m);
	    spidev_ret = ioctl(fd, SPI_IOC_MESSAGE(m), tr);
    	if (spidev_ret < 1)
        {
		    printf("can't send spi message! (error %d)\n", spidev_ret);
            return spidev_ret;
        }

        for (m=0, fresp_loop=fresp; m<n_trans_per_buf; m++, fresp_loop++)
        {
            if (fresp_loop->resp == 143)
            {
                written_bytes += 4;
            }
            fpga_ret = fpga_ret | fresp_loop->resp;
            if (++rx_word_cnt == n_transfers)
            {
                break;
            }
        }
    }
    free(fcmd);
    free(fresp);
    free(tr);
    return written_bytes;
}

/* Read a single word to the FPGA */
int read_word(int fd, uint32_t addr, uint32_t *val)
{

    struct fpga_spi_cmd *fcmd;
    struct fpga_spi_cmd *fresp;
    int spidev_ret = 0;
    int fpga_ret = 0;

    fcmd = malloc(sizeof(struct fpga_spi_cmd));
    if (!fcmd)
    {
        printf("Failed to allocate fcmd\n");
        return -1;
    }

    fresp = malloc(sizeof(struct fpga_spi_cmd));
    if (!fresp)
    {
        printf("Failed to allocate fresp\n");
        return -1;
    }

	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long) fcmd,
		.rx_buf = (unsigned long) fresp,
		.len = sizeof(struct fpga_spi_cmd),
		.delay_usecs = delay,
		.speed_hz = speed,
		.bits_per_word = bits,
	};


    fcmd->cmd = 0x0F; //read command, all byte enables=1
    fcmd->addr = addr;
    fcmd->din = 0;
    fcmd->dout = 0; //Dummy bytes while slave sends back data
    fcmd->resp = 0; //Dummy bytes while slave sends back data


    //printf("Sending SPI message\n");
	spidev_ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (spidev_ret < 1)
    {
		printf("can't send spi message");
        return spidev_ret;
    }

    memcpy(val, &fresp->dout, sizeof(unsigned int));
    fpga_ret = fresp->resp;
    free(fcmd);
    free(fresp);

    if (fpga_ret == 143) {
        return 4;
    } else {
        return 0;
    }
}

int config_spi()
{
	int fd;
    int ret;

	fd = open(DEVICE, O_RDWR);
	if (fd < 0)
    {
		printf("can't open device\n");
        return fd;
    }

	/*
	 * spi mode
	 */
	ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
	if (ret == -1)
    {
		printf("can't set spi mode\n");
        return ret;
    }

	ret = ioctl(fd, SPI_IOC_RD_MODE, &mode);
	if (ret == -1)
    {
		printf("can't set spi mode\n");
        return ret;
    }

	/*
	 * bits per word
	 */
	ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
	if (ret == -1)
    {
		printf("can't set bits per word\n");
        return ret;
    }

	ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
	if (ret == -1)
    {
		printf("can't get bits per word\n");
        return ret;
    }

	/*
	 * max speed hz
	 */
	ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
	if (ret == -1)
    {
		printf("can't set max speed hz\n");
        return ret;
    }

	ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
	if (ret == -1)
    {
		printf("can't get max speed hz\n");
        return ret;
    }

	printf("spi mode: %d\n", mode);
	printf("bits per word: %d\n", bits);
	printf("max speed: %d Hz (%d KHz)\n", speed, speed/1000);

	return fd;
}




