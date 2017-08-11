#ifndef SPIFPGA_USER_H_
#define SPIFPGA_USER_H_

#define DEVICE "/dev/spidev0.0"
#define MAX_SPEED 4000000
#define DELAY 1
#define BITS 8
#define MAX_BURST_SIZE 256
#define BYTES_PER_WORD 4

struct fpga_spi_cmd {
    unsigned char cmd;
    unsigned int addr;
    unsigned int din;
    unsigned int dout;
    unsigned char resp;
} __attribute__((packed));

int config_spi();
int write_word(int fd, uint32_t addr, uint32_t val);
int read_word(int fd, uint32_t addr, uint32_t *val);
int bulk_read(int fd, uint32_t start_addr, unsigned int n_bytes, uint32_t *buf);
int bulk_write(int fd, uint32_t start_addr, unsigned int n_bytes, uint32_t *buf);

#endif
