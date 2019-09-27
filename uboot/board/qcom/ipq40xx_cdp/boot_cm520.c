#include <configs/ipq40xx_cdp.h>
#include <common.h>
#include <image.h>
#include "ipq40xx_cdp.h"

extern board_ipq40xx_params_t *gboard_param;

/*
 * Set the root device and bootargs for mounting root filesystem.
 */
static int set_fs_bootargs(void)
{
	char *bootargs;

#define nand_rootfs	"ubi.mtd=" QCA_ROOT_FS_PART_NAME " root=mtd:ubi_rootfs rootfstype=squashfs"

    bootargs = nand_rootfs;
    if (getenv("fsbootargs") == NULL)
        setenv("fsbootargs", bootargs);

	return run_command("setenv bootargs ${bootargs} ${fsbootargs} rootwait", 0);
}

static int config_select(unsigned int addr, const char **config, char *rcmd, int rcmd_size)
{
	/* Selecting a config name from the list of available
	 * config names by passing them to the fit_conf_get_node()
	 * function which is used to get the node_offset with the
	 * config name passed. Based on the return value index based
	 * or board name based config is used.
	 */

	int i;
	for (i = 0; i < MAX_CONF_NAME && config[i]; i++) {
		if (fit_conf_get_node((void *)addr, config[i]) >= 0) {
			snprintf(rcmd, rcmd_size, "bootm 0x%x#%s\n",
				addr, config[i]);
			return 0;
		}
	}
	printf("Config not availabale\n");
	return -1;
}

static int do_cm520_boot(cmd_tbl_t * cmdtp, int flag, int argc, char * const argv[])
{
    int ret;
    char runcmd[256];

    if ((ret = set_fs_bootargs()))
		return ret;
    
    snprintf(runcmd, sizeof(runcmd),
			"set mtdids nand0=nand0 && "
			"set mtdparts mtdparts=nand0:0x%x@0x%x(fs),${msmparts} && "
			"ubi part fs && "
			"ubi read 0x%x kernel && ",
			CONFIG_FIRMWARE_SIZE, CONFIG_FIRMWARE_START,
			CONFIG_SYS_LOAD_ADDR);
    
    if (run_command(runcmd, 0) != CMD_RET_SUCCESS) {
		return CMD_RET_FAILURE;
	}

	dcache_enable();

	ret = genimg_get_format((void *)CONFIG_SYS_LOAD_ADDR);
	if (ret == IMAGE_FORMAT_FIT) {
		ret = config_select(CONFIG_SYS_LOAD_ADDR, gboard_param->dtb_config_name,
				runcmd, sizeof(runcmd));
	} else if (ret == IMAGE_FORMAT_LEGACY) {
		snprintf(runcmd, sizeof(runcmd),
			"bootm 0x%x\n", CONFIG_SYS_LOAD_ADDR);
	} else {
			dcache_disable();
			return CMD_RET_FAILURE;
	}

	if (ret < 0 || run_command(runcmd, 0) != CMD_RET_SUCCESS) {
		dcache_disable();
		return CMD_RET_FAILURE;
	}
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
    boot_cm520, 1, 0, do_cm520_boot,
    "boot cm520 board",
    NULL
);