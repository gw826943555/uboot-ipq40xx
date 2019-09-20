/*
 * Command for accessing SPI flash.
 *
 * Copyright (C) 2008 Atmel Corporation
 * Licensed under the GPL-2 or later.
 */

#include <common.h>
#include <malloc.h>
#include <spi_flash.h>

#include <asm/io.h>

#ifndef CONFIG_SF_DEFAULT_SPEED
# define CONFIG_SF_DEFAULT_SPEED	1000000
#endif
#ifndef CONFIG_SF_DEFAULT_MODE
# define CONFIG_SF_DEFAULT_MODE		SPI_MODE_3
#endif
#ifndef CONFIG_SF_DEFAULT_CS
# define CONFIG_SF_DEFAULT_CS		0
#endif
#ifndef CONFIG_SF_DEFAULT_BUS
# define CONFIG_SF_DEFAULT_BUS		0
#endif

static struct spi_flash *flash;


/*
 * This function computes the length argument for the erase command.
 * The length on which the command is to operate can be given in two forms:
 * 1. <cmd> offset len  - operate on <'offset',  'len')
 * 2. <cmd> offset +len - operate on <'offset',  'round_up(len)')
 * If the second form is used and the length doesn't fall on the
 * sector boundary, than it will be adjusted to the next sector boundary.
 * If it isn't in the flash, the function will fail (return -1).
 * Input:
 *    arg: length specification (i.e. both command arguments)
 * Output:
 *    len: computed length for operation
 * Return:
 *    1: success
 *   -1: failure (bad format, bad address).
 */
static int sf_parse_len_arg(char *arg, ulong *len)
{
	char *ep;
	char round_up_len; /* indicates if the "+length" form used */
	ulong len_arg;

	round_up_len = 0;
	if (*arg == '+') {
		round_up_len = 1;
		++arg;
	}

	len_arg = simple_strtoul(arg, &ep, 16);
	if (ep == arg || *ep != '\0')
		return -1;

	if (round_up_len && flash->sector_size > 0)
		*len = ROUND(len_arg, flash->sector_size);
	else
		*len = len_arg;

	return 1;
}

static int do_spi_flash_probe(int argc, char * const argv[])
{
	unsigned int bus = CONFIG_SF_DEFAULT_BUS;
	unsigned int cs = CONFIG_SF_DEFAULT_CS;
	unsigned int speed = CONFIG_SF_DEFAULT_SPEED;
	unsigned int mode = CONFIG_SF_DEFAULT_MODE;
	char *endp;
	struct spi_flash *new;

	if (argc >= 2) {
		cs = simple_strtoul(argv[1], &endp, 0);
		if (*argv[1] == 0 || (*endp != 0 && *endp != ':'))
			return -1;
		if (*endp == ':') {
			if (endp[1] == 0)
				return -1;

			bus = cs;
			cs = simple_strtoul(endp + 1, &endp, 0);
			if (*endp != 0)
				return -1;
		}
	}

	if (argc >= 3) {
		speed = simple_strtoul(argv[2], &endp, 0);
		if (*argv[2] == 0 || *endp != 0)
			return -1;
	}
	if (argc >= 4) {
		mode = simple_strtoul(argv[3], &endp, 16);
		if (*argv[3] == 0 || *endp != 0)
			return -1;
	}

	new = spi_flash_probe(bus, cs, speed, mode);
	if (!new) {
		printf("Failed to initialize SPI flash at %u:%u\n", bus, cs);
		return 1;
	}

	if (flash)
		spi_flash_free(flash);
	flash = new;

	return 0;
}

/**
 * Write a block of data to SPI flash, first checking if it is different from
 * what is already there.
 *
 * If the data being written is the same, then *skipped is incremented by len.
 *
 * @param flash		flash context pointer
 * @param offset	flash offset to write
 * @param len		number of bytes to write
 * @param buf		buffer to write from
 * @param cmp_buf	read buffer to use to compare data
 * @param skipped	Count of skipped data (incremented by this function)
 * @return NULL if OK, else a string containing the stage which failed
 */
static const char *spi_flash_update_block(struct spi_flash *flash, u32 offset,
		size_t len, const char *buf, char *cmp_buf, size_t *skipped)
{
	debug("offset=%#x, sector_size=%#x, len=%#zx\n",
		offset, flash->sector_size, len);
	if (spi_flash_read(flash, offset, len, cmp_buf))
		return "read";
	if (memcmp(cmp_buf, buf, len) == 0) {
		debug("Skip region %x size %zx: no change\n",
			offset, len);
		*skipped += len;
		return NULL;
	}
	if (spi_flash_erase(flash, offset, len))
		return "erase";
	if (spi_flash_write(flash, offset, len, buf))
		return "write";
	return NULL;
}

/**
 * Update an area of SPI flash by erasing and writing any blocks which need
 * to change. Existing blocks with the correct data are left unchanged.
 *
 * @param flash		flash context pointer
 * @param offset	flash offset to write
 * @param len		number of bytes to write
 * @param buf		buffer to write from
 * @return 0 if ok, 1 on error
 */
static int spi_flash_update(struct spi_flash *flash, u32 offset,
		size_t len, const char *buf)
{
	const char *err_oper = NULL;
	char *cmp_buf;
	const char *end = buf + len;
	size_t todo;		/* number of bytes to do in this pass */
	size_t skipped = 0;	/* statistics */

	cmp_buf = malloc(flash->sector_size);
	if (cmp_buf) {
		for (; buf < end && !err_oper; buf += todo, offset += todo) {
			todo = min(end - buf, flash->sector_size);
			err_oper = spi_flash_update_block(flash, offset, todo,
					buf, cmp_buf, &skipped);
		}
	} else {
		err_oper = "malloc";
	}
	free(cmp_buf);
	if (err_oper) {
		printf("SPI flash failed in %s step\n", err_oper);
		return 1;
	}
	printf("%zu bytes written, %zu bytes skipped\n", len - skipped,
	       skipped);

	return 0;
}

static int do_spi_flash_read_write(int argc, char * const argv[])
{
	unsigned long addr;
	unsigned long offset;
	unsigned long len;
	void *buf;
	char *endp;
	int ret;

	if (argc < 4)
		return -1;

	addr = simple_strtoul(argv[1], &endp, 16);
	if (*argv[1] == 0 || *endp != 0)
		return -1;
	offset = simple_strtoul(argv[2], &endp, 16);
	if (*argv[2] == 0 || *endp != 0)
		return -1;
	len = simple_strtoul(argv[3], &endp, 16);
	if (*argv[3] == 0 || *endp != 0)
		return -1;

	buf = map_physmem(addr, len, MAP_WRBACK);
	if (!buf) {
		puts("Failed to map physical memory\n");
		return 1;
	}

	if (strcmp(argv[0], "update") == 0)
		ret = spi_flash_update(flash, offset, len, buf);
	else if (strcmp(argv[0], "read") == 0)
		ret = spi_flash_read(flash, offset, len, buf);
	else
		ret = spi_flash_write(flash, offset, len, buf);

	unmap_physmem(buf, len);

	if (ret) {
		printf("SPI flash %s failed\n", argv[0]);
		return 1;
	}

	return 0;
}

static int do_spi_flash_erase(int argc, char * const argv[])
{
	unsigned long offset;
	unsigned long len;
	char *endp;
	int ret;

	if (argc < 3)
		return -1;

	offset = simple_strtoul(argv[1], &endp, 16);
	if (*argv[1] == 0 || *endp != 0)
		return -1;

	ret = sf_parse_len_arg(argv[2], &len);
	if (ret != 1)
		return -1;

	ret = spi_flash_erase(flash, offset, len);
	if (ret) {
		printf("SPI flash %s failed\n", argv[0]);
		return 1;
	}

	return 0;
}

static int do_spi_flash_berase(int argc, char * const argv[])
{
	switch (spi_flash_berase(flash)) {
	case 0:
		return 0;
	case -ENOTSUPP:
		printf("SPI flash %s not supported\n", argv[0]);
		return 1;
	default:
		printf("SPI flash %s failed\n", argv[0]);
		return 1;
	}

	return 1;
}

static int do_spi_flash(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	const char *cmd;
	int ret;

	/* need at least two arguments */
	if (argc < 2)
		goto usage;

	cmd = argv[1];
	--argc;
	++argv;

	if (strcmp(cmd, "probe") == 0) {
		ret = do_spi_flash_probe(argc, argv);
		goto done;
	}

	/* The remaining commands require a selected device */
	if (!flash) {
		puts("No SPI flash selected. Please run `sf probe'\n");
		return 1;
	}

	if (strcmp(cmd, "read") == 0 || strcmp(cmd, "write") == 0 ||
	    strcmp(cmd, "update") == 0)
		ret = do_spi_flash_read_write(argc, argv);
	else if (strcmp(cmd, "erase") == 0)
		ret = do_spi_flash_erase(argc, argv);
	else if (strcmp(cmd, "bulkerase") == 0)
		ret = do_spi_flash_berase(argc, argv);
	else
		ret = -1;

done:
	if (ret != -1)
		return ret;

usage:
	return CMD_RET_USAGE;
}

U_BOOT_CMD(
	sf,	5,	1,	do_spi_flash,
	"SPI flash sub-system",
	"probe [[bus:]cs] [hz] [mode]	- init flash device on given SPI bus\n"
	"				  and chip select\n"
	"sf read addr offset len 	- read `len' bytes starting at\n"
	"				  `offset' to memory at `addr'\n"
	"sf write addr offset len	- write `len' bytes from memory\n"
	"				  at `addr' to flash at `offset'\n"
	"sf erase offset [+]len		- erase `len' bytes from `offset'\n"
	"				  `+len' round up `len' to block size\n"
	"sf bulkerase			- Erase entire flash chip\n"
	"				  (Not supported on all devices)\n"
	"sf update addr offset len	- erase and write `len' bytes from memory\n"
	"				  at `addr' to flash at `offset'"
);

/**
*0 burning qsdk firmware. 1 burning lede firmware
*/
int do_checkout_firmware(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	volatile unsigned char *s = (volatile unsigned char *)0x84000084;
	volatile unsigned char *c = (volatile unsigned char *)0x84000085;
	volatile unsigned char *r = (volatile unsigned char *)0x84000086;
	volatile unsigned char *i = (volatile unsigned char *)0x84000087;
	volatile unsigned char *p = (volatile unsigned char *)0x84000088;
	volatile unsigned char *t = (volatile unsigned char *)0x84000089;

	if (*s==0x73 && *c==0x63 && *r==0x72 && *i==0x69 && *p==0x70 && *t==0x74 ) {
		return 0;
	}
	
	return 1;
}

int do_burning_qsdk(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	char cmd[128] = {0};
	
	printf("do_burning_qsdk\n");
	sprintf(cmd, "imgaddr=0x84000000 && source $imgaddr:script");

	return run_command(cmd, 0);
}

int do_burning_lede(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	char cmd[128] = {0};
	
	printf("do_burning_lede\n");
	sprintf(cmd, "sf probe && sf erase 0x%x 0x%x && sf write 0x84000000 0x%x $filesize",
		CONFIG_FIRMWARE_START, CONFIG_FIRMWARE_SIZE, CONFIG_FIRMWARE_START);

	return run_command(cmd, 0);
}

void change_ethernet_mac(void)
{
	int i;
	volatile unsigned char *addr = (volatile unsigned char *)0x84000100;
	volatile unsigned char *src = (volatile unsigned char *)0x84000000;

	for (i=0; i<64; i++) {
		*addr++ = *src++;
	}
}

void change_wifi_mac(void)
{
	int i;
	int checksum_2g = 0;
	int checksum_5g = 0;
	volatile unsigned char *wifi_2g = NULL;
	volatile unsigned short *wifi_2g_checksum = NULL;
	volatile unsigned char *wifi_5g = NULL;
	volatile unsigned short *wifi_5g_checksum = NULL;
	volatile unsigned char *src = (volatile unsigned char *)0x84000070;
	volatile unsigned short *wifi_2g_art = NULL;
	volatile unsigned short *wifi_5g_art = NULL;

	wifi_2g = (volatile unsigned char *)0x84001106;
	wifi_2g_checksum = (volatile unsigned short *)0x84001102;
	wifi_5g = (volatile unsigned char *)0x84005106;
	wifi_5g_checksum = (volatile unsigned short *)0x84005102;
	wifi_2g_art = (volatile unsigned short *)0x84001100;
	wifi_5g_art = (volatile unsigned short *)0x84005100;

	for (i=0; i<12; i++) {
		if (i<6)
		 	*wifi_2g++ = *src++;
		else
			*wifi_5g++ = *src++;
	}

	printf("befour wifi_2g_checksum = %04x, wifi_5g_checksum = %04x\n", *wifi_2g_checksum, *wifi_5g_checksum);
	*wifi_2g_checksum = *wifi_5g_checksum = -1;
	printf("after wifi_2g_checksum = %04x, wifi_5g_checksum = %04x\n", *wifi_2g_checksum, *wifi_5g_checksum);
	

	for (i=0; i<12064; i+=2) {
	   checksum_2g ^= *wifi_2g_art;
	   checksum_5g ^= *wifi_5g_art;
	   wifi_2g_art++;
	   wifi_5g_art++;
	}
	*wifi_2g_checksum = checksum_2g;
	*wifi_5g_checksum = checksum_5g;
	printf("after wifi_2g_checksum = %04x\n", *wifi_2g_checksum);
	printf("after wifi_5g_checksum = %04x\n", *wifi_5g_checksum);
}

int do_update_config(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	char cmd[128];
	sprintf(cmd, "sf probe && sf read 0x84000100 0x%x 0x%x", CONFIG_ART_START, CONFIG_ART_SIZE);
	run_command(cmd, 0);
	
	change_ethernet_mac();
	change_wifi_mac();
	
	sprintf(cmd, "sf erase 0x%x 0x%x && sf write 0x84000100 0x%x 0x%x", 
		CONFIG_ART_START, CONFIG_ART_SIZE, CONFIG_ART_START, CONFIG_ART_SIZE);
	
	return run_command(cmd, 0);
}

U_BOOT_CMD(
	checkfw,	CONFIG_SYS_MAXARGS,	0,	do_checkout_firmware,
	"check is qsdk or lede firmware",
	"[args..]"
);

U_BOOT_CMD(
	burning_qsdk,	CONFIG_SYS_MAXARGS,	0,	do_burning_qsdk,
	"burning qsdk tool",
	"[args..]"
);

U_BOOT_CMD(
	burning_lede,	CONFIG_SYS_MAXARGS,	0,	do_burning_lede,
	"burning lede tool",
	"[args..]"
);

U_BOOT_CMD(
	updateconfig,	CONFIG_SYS_MAXARGS,	0,	do_update_config,
	"update config",
	"[args..]"
);


