/*
 * SPI testing utility (using spidev driver)
 *
 * Copyright (c) 2007  MontaVista Software, Inc.
 * Copyright (c) 2007  Anton Vorontsov 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * Cross-compile with cross-gcc -I/path/to/cross-kernel/include
 *
 */
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

//#define DEBUG	1

#ifdef DEBUG
#define DBG_MSG(fmt, arg...) fprintf(stdout, fmt, ##arg)
#else
#define DBG_MSG(fmt, arg...)
#endif
#define INF_MSG(fmt, arg...) fprintf(stdout, fmt, ##arg)
#define ERR_MSG(fmt, arg...) fprintf(stderr, fmt, ##arg)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define CODE_RESET	1
#define CODE_APDU	2

#define pabort(fmt, arg...) do {                \
        fprintf(stderr, fmt, ##arg);            \
        fflush(stderr);                         \
        fflush(stdout);                         \
        exit(-1);                               \
    } while(0)

#define GPIO_INT	"/sys/class/gpio/pesam_int/value"
		
static const char *device = "/dev/spidev2.0";
/** 
    - CPOL indicates the initial clock polarity.  CPOL=0 means the
    clock starts low, so the first (leading) edge is rising, and
    the second (trailing) edge is falling.  CPOL=1 means the clock
    starts high, so the first (leading) edge is falling.

    - CPHA indicates the clock phase used to sample data; CPHA=0 says
    sample on the leading edge, CPHA=1 means the trailing edge.

    Since the signal needs to stablize before it's sampled, CPHA=0
    implies that its data is written half a clock before the first
    clock edge.  The chipselect may have made it become available.
    *
    * MODE   CPOL    CPHA
    * ===================
    *   0      0      0
    *   1      0      1
    *   2      1      0
    *   3      1      1
    */
 
static uint8_t mode = SPI_MODE_2;
static uint8_t bits = 16;
static uint32_t speed = 1000000;
static uint16_t delay = 0;
static int fd_spi = -1;

/* ===================== Function Implementations ======================= */
static void print_hex(const char *pdata, int len)
{
    int i;
    for (i=0;i<len;i++) {
        INF_MSG("0x%02x ", pdata[i]);
        if ((i+1)%16 == 0)
            INF_MSG("\n");
    }
    INF_MSG("\n");
}

static int read_quence_adc(int ch)
{
    struct spi_ioc_transfer tr[2];
    uint16_t tx, rx = 0;
    int ret, idx, i;

    bzero(&tr, sizeof(tr));

    tx = ((1<<11)|(1<<10)|((ch&3)<<6)|(0x3<<4)|(1<<3)/*|(1<<1)*/|1)<<4;
    tr[0].tx_buf = (unsigned long)&tx;
    tr[0].len = sizeof(tx);
    tr[0].cs_change = 1;
    tr[0].delay_usecs = 10;

    ret = ioctl(fd_spi, SPI_IOC_MESSAGE(1), &tr[0]);
    if (ret < 0)
        ERR_MSG("%s: ret=%d\n", __func__, ret);
    
    while (1) {
        for (i = 0; i <= ch; ++i) {
            tr[1].rx_buf = (unsigned long)&rx;
            tr[1].len = sizeof(rx);
            tr[1].cs_change = 1;
            tr[1].delay_usecs = 10;

            ret = ioctl(fd_spi, SPI_IOC_MESSAGE(1), &tr[1]);
            if (ret < 0)
                ERR_MSG("%s: ret=%d\n", __func__, ret);

            ret = (rx >> 4) & 0xFF;
            idx = (rx >> 12) & 0x3;
            printf("ch%d: %6.1fv (%#x)\n", idx,  (ret * 1.0  / 256) * 5.0, ret);
        
            fflush(stdout);
            rx = 0;
        }
        sleep(1);
        printf("\n");
    }
}

static uint16_t read_adc(int ch)
{
    struct spi_ioc_transfer tr[2];
    uint16_t tx, rx=0;
    int ret,i;
    float fval = 0.0;

    bzero(&tr, sizeof(tr));

    tx = ((1<<11)|((ch&3)<<6)|(0x3<<4)/*|(1<<1)*/|1)<<4;
	
    tr[0].tx_buf = (unsigned long)&tx;
    tr[0].len = sizeof(tx);
    tr[0].cs_change = 1;
    tr[0].delay_usecs = 10;
	
    tr[1].rx_buf = (unsigned long)&rx;
    //tr[1].tx_buf = (unsigned long)&tx;
    tr[1].len = sizeof(rx);
    tr[1].cs_change = 1;
    tr[1].delay_usecs = 10;

    ret = ioctl(fd_spi, SPI_IOC_MESSAGE(1), &tr[0]);
    if (ret < 0)
        ERR_MSG("%s: ret=%d\n", __func__, ret);

    while(1) {
        ret = ioctl(fd_spi, SPI_IOC_MESSAGE(1), &tr[1]);
        if (ret < 0)
            ERR_MSG("%s: ret=%d\n", __func__, ret);

        ret = (rx>>4)&0xFF;
        fval = (float)ret;
        printf("ch%d: %6.1fv(0x%02X)\r", rx>>12, fval*5.0/256.0, ret);
        fflush(stdout);
    }
	
    return rx;
}


int main(int argc, char *argv[])
{
    int ret = 0;
    int ch = -1;
    char dummy[256];

    if (argc != 3 ) {
        ERR_MSG("\n Usage: %s <ch> <speed-khz>\n\n", argv[0]);
        exit(-1);
    }
    ch = atoi(argv[1]);
    speed = atoi(argv[2])*1000;
	
    fd_spi = open(device, O_RDWR);
    if (fd_spi < 0)
        pabort("can't open device\n");

    /*
     * spi mode
     */
    ret = ioctl(fd_spi, SPI_IOC_WR_MODE, &mode);
    if (ret == -1)
        pabort("can't set spi mode\n");

    ret = ioctl(fd_spi, SPI_IOC_RD_MODE, &mode);
    if (ret == -1)
        pabort("can't get spi mode\n");

    /*
     * bits per word
     */
    ret = ioctl(fd_spi, SPI_IOC_WR_BITS_PER_WORD, &bits);
    if (ret == -1)
        pabort("can't set bits per word\n");

    ret = ioctl(fd_spi, SPI_IOC_RD_BITS_PER_WORD, &bits);
    if (ret == -1)
        pabort("can't get bits per word\n");

    /*
     * max speed hz
     */
    ret = ioctl(fd_spi, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    if (ret == -1)
        pabort("can't set max speed hz\n");

    ret = ioctl(fd_spi, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
    if (ret == -1)
        pabort("can't get max speed hz\n");

    DBG_MSG("spi mode: %d\n", mode);
    DBG_MSG("bits per word: %d\n", bits);
    DBG_MSG("max speed: %d Hz (%d KHz)\n", speed, speed/1000);
    DBG_MSG("sizeof frame_t = %d, sizeof data_t = %d\n",
            sizeof(frame_t), sizeof(data_t));

//	while (1) {
//    read_adc(ch);
//		usleep(100*1000);
//	}

    read_quence_adc(ch);
    
    close(fd_spi);

    return ret;
}

