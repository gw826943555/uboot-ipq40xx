/*
 *	Copyright 1994, 1995, 2000 Neil Russell.
 *	(See License)
 *	Copyright 2000, 2001 DENX Software Engineering, Wolfgang Denk, wd@denx.de
 */

#include <common.h>
#include <command.h>
#include <net.h>
#include <asm/byteorder.h>

#if defined(CONFIG_CMD_HTTPD)
#include "httpd.h"

#include "../httpd/uipopt.h"
#include "../httpd/uip.h"
#include "../httpd/uip_arp.h"
#include "gl_config.h"

#include <asm/arch-qcom-common/gpio.h>


static int arptimer = 0;
extern void NetSendHttpd(void);
void HttpdHandler(void){
	int i;

	for(i = 0; i < UIP_CONNS; i++){
		uip_periodic(i);

		if(uip_len > 0){
			uip_arp_out();
			NetSendHttpd();
		}
	}

	if(++arptimer == 20){
		uip_arp_timer();
		arptimer = 0;
	}
}

// start http daemon
void HttpdStart(void){
	uip_init();
	httpd_init();
}

extern int do_checkout_firmware(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[]);
int do_http_upgrade( const ulong size, const int upgrade_type )
{
	char cmd[128] = {0};

	if ( upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_UBOOT ) {
		printf( "\n\n****************************\n*     U-BOOT UPGRADING     *\n* DO NOT POWER OFF DEVICE! *\n****************************\n\n" );

		sprintf(cmd, "nand erase 0x%x 0x%x && nand write 0x%x 0x%x 0x%x && nand erase 0x%x 0x%x && nand write 0x%x 0x%x 0x%x", 
			CONFIG_UBOOT1_START, CONFIG_UBOOT1_SIZE, CONFIG_LOADADDR, CONFIG_UBOOT1_START, CONFIG_UBOOT1_SIZE,
			CONFIG_UBOOT2_START, CONFIG_UBOOT2_SIZE, CONFIG_LOADADDR, CONFIG_UBOOT2_START, CONFIG_UBOOT2_SIZE);
		return run_command(cmd, 0);

	} else if ( upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE ) {

		printf( "\n\n****************************\n*    FIRMWARE UPGRADING    *\n* DO NOT POWER OFF DEVICE! *\n****************************\n\n" );

		if ( do_checkout_firmware(NULL, 0, 0, NULL) ) {
			sprintf(cmd, "nand erase 0x%x 0x%x && nand write 0x%x 0x%x 0x%x",
				CONFIG_FIRMWARE_START, CONFIG_FIRMWARE_SIZE, CONFIG_LOADADDR, CONFIG_FIRMWARE_START, CONFIG_FIRMWARE_SIZE);
		} else {
			sprintf(cmd, "sf probe && imgaddr=0x84000000 && source $imgaddr:script");
		}
		printf("cmd:%s\r\n", cmd);
		return run_command(cmd, 0);
		
	} else if ( upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_ART ) {

		printf( "\n\n****************************\n*      ART  UPGRADING      *\n* DO NOT POWER OFF DEVICE! *\n****************************\n\n" );
		sprintf(cmd, "nand erase 0x%x 0x%x && nand write 0x%x 0x%x 0x%x",
			CONFIG_ART_START, CONFIG_ART_SIZE, CONFIG_LOADADDR, CONFIG_ART_START, CONFIG_ART_SIZE);

		return run_command(cmd, 0);

	} else {
		return(-1);
	}
	return(-1);
}

// info about current progress of failsafe mode
int do_http_progress(const int state){
	/* toggle LED's here */
	switch(state){
		case WEBFAILSAFE_PROGRESS_START:

			// blink LED fast 10 times
			/*for(i = 0; i < 10; ++i){
				all_led_on();
				udelay(25000);
				all_led_off();
				udelay(25000);
			}*/

			printf("HTTP server is ready!\n\n");
			break;

		case WEBFAILSAFE_PROGRESS_TIMEOUT:
			//printf("Waiting for request...\n");
			break;

		case WEBFAILSAFE_PROGRESS_UPLOAD_READY:
			printf("HTTP upload is done! Upgrading...\n");
			char buf[20];
			printf("Bytes transferred = %ld (%lx hex)\n",NetBootFileXferSize,NetBootFileXferSize);
			sprintf(buf, "%lX", NetBootFileXferSize);
			setenv("filesize", buf);
			sprintf(buf, "%lX", (unsigned long)load_addr);
			setenv("fileaddr", buf);
			break;

		case WEBFAILSAFE_PROGRESS_UPGRADE_READY:
			printf("HTTP ugrade is done! Rebooting...\n\n");
			break;

		case WEBFAILSAFE_PROGRESS_UPGRADE_FAILED:
			printf("HTTP ugrade failed!\n\n");

			// blink LED fast for 4 sec
			/*for(i = 0; i < 80; ++i){
				all_led_on();
				udelay(25000);
				all_led_off();
				udelay(25000);
			}*/

			// wait 1 sec
			udelay(1000000);

			break;
	}

	return(0);
}
#endif /* CONFIG_CMD_HTTPD */
