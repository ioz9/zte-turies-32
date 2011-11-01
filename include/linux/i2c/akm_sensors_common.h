/*
 * Definitions for akm8962 && akm8973 compass chips.
 */
 /* ==========================================================================
when         	who        	what, where, why                                        				comment tag
--------       ----    	   -------------------------------------    		----------------------------------
2011-04-15     feng.yuao    Merge head files for two compass chips.             ZTE_AKM_SENSORS_001 

#ifndef AKM_SENSORS_COMMON_H
#define AKM_SENSORS_COMMON_H

#include <linux/ioctl.h>

/*
 * Compass device dependent definition 
 */
#define AKECS_MODE_MEASURE		0x00	/* Starts measurement. */
#define AKECS_MODE_E2P_READ		0x02	/* E2P access mode (read). */
#define AKECS_MODE_POWERDOWN	0x03	/* Power down mode */

#define RBUFF_SIZE		4	/* Rx buffer size */

/* AK8973 register address */
#define AKECS_REG_ST			0xC0
#define AKECS_REG_TMPS			0xC1
#define AKECS_REG_H1X			0xC2
#define AKECS_REG_H1Y			0xC3
#define AKECS_REG_H1Z			0xC4

#define AKECS_REG_MS1			0xE0
#define AKECS_REG_HXDA			0xE1
#define AKECS_REG_HYDA			0xE2
#define AKECS_REG_HZDA			0xE3
#define AKECS_REG_HXGA			0xE4
#define AKECS_REG_HYGA			0xE5
#define AKECS_REG_HZGA			0xE6
/* End by fengyuao ZTE_AKM_SENSORS_001 AKM8973--------------- */

/* Defined by fengyuao ZTE_AKM_SENSORS_001 AKM8962 ++++++++++ */
/* name AK8962 operation mode anchor AK8962_Mode. Defines an operation mode of the AK8962.*/
#define AK8962_MODE_SNG_MEASURE	0x01
#define	AK8962_MODE_SELF_TEST	0x08
#define	AK8962_MODE_FUSE_ACCESS	0x0F
#define	AK8962_MODE_POWERDOWN	0x00

#define SENSOR_DATA_SIZE		8	/* Rx buffer size, i.e from ST1 to ST2 */
#define RWBUF_SIZE				16	/* Read/Write buffer size.*/

/* AK8962 register address anchor AK8962_REG.Defines a register address of the AK8962.*/
#define AK8962_REG_WIA		0x00
#define AK8962_REG_INFO		0x01
#define AK8962_REG_ST1		0x02
#define AK8962_REG_HXL		0x03
#define AK8962_REG_HXH		0x04
#define AK8962_REG_HYL		0x05
#define AK8962_REG_HYH		0x06
#define AK8962_REG_HZL		0x07
#define AK8962_REG_HZH		0x08
#define AK8962_REG_ST2		0x09
#define AK8962_REG_CNTL		0x0A
#define AK8962_REG_RSV		0x0B
#define AK8962_REG_ASTC		0x0C
#define AK8962_REG_TS1		0x0D
#define AK8962_REG_TS2		0x0E
#define AK8962_REG_I2CDIS	0x0F

/* AK8962 fuse-rom address anchor AK8962_FUSE.Defines a read-only address of the fuse ROM of the AK8962.*/
#define AK8962_FUSE_ASAX	0x10
#define AK8962_FUSE_ASAY	0x11
#define AK8962_FUSE_ASAZ	0x12
/* End by fengyuao ZTE_AKM_SENSORS_001 AKM8962 ----------- */

/********************************** ALL USED $$$$$$$**********************************/
#define AKMIO                   0xA1

/* IOCTLs for APPs */
#define ECS_IOCTL_APP_SET_MODE		_IOW(AKMIO, 0x10, short)
#define ECS_IOCTL_APP_SET_MFLAG		_IOW(AKMIO, 0x11, short)
#define ECS_IOCTL_APP_GET_MFLAG		_IOW(AKMIO, 0x12, short)
#define ECS_IOCTL_APP_SET_AFLAG		_IOW(AKMIO, 0x13, short)
#define ECS_IOCTL_APP_GET_AFLAG		_IOR(AKMIO, 0x14, short)
#define ECS_IOCTL_APP_SET_TFLAG		_IOR(AKMIO, 0x15, short)/* NOT use */
#define ECS_IOCTL_APP_GET_TFLAG		_IOR(AKMIO, 0x16, short)/* NOT use */
#define ECS_IOCTL_APP_RESET_PEDOMETER   _IO(AKMIO, 0x17)	/* NOT use */
#define ECS_IOCTL_APP_SET_DELAY		_IOW(AKMIO, 0x18, short)
#define ECS_IOCTL_APP_GET_DELAY		ECS_IOCTL_GET_DELAY
#define ECS_IOCTL_APP_SET_MVFLAG	_IOW(AKMIO, 0x19, short)
#define ECS_IOCTL_APP_GET_MVFLAG	_IOR(AKMIO, 0x1A, short)
/********************************** ALL USED @@@@@@@*********************************/

/*Defined by fengyuao ZTE_AKM_SENSORS_001 AKM8973 +++++++++++++++ */
/* IOCTLs for pedometer */
#define ECS_IOCTL_SET_STEP_CNT          _IOW(AKMIO, 0x20, short)

/* Default GPIO setting */
#if defined(CONFIG_MACH_JOE)
#define ECS_RST		38	/*reset gpio*/
#else
#define ECS_RST		89	/*MISC4, bit2 */
#endif
#define ECS_CLK_ON	107	/*MISC5, bit3 */
#define ECS_INTR	32 	/*INT2, bit1 */

struct akm8973_platform_data {
	int reset;
	int clk_on;
	int intr;
};

extern char *get_akm_cal_ram(void);
/* End by fengyuao ZTE_AKM_SENSORS_001 AKM8973 ---------------- */

/* Defined by fengyuao ZTE_AKM_SENSORS_001 AKM8973 +++++++++++++++ */
struct akm8962_platform_data {
	char layouts[3][3];
	char project_name[64];
	int gpio_DRDY;
};
/* End by fengyuao ZTE_AKM_SENSORS_001 AKM8973 ------------- */

