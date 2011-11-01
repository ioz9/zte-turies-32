/*
 * Definitions for akm8973 compass chip.Created by feng.yuao@zte.com.cn
 */

#ifndef AKM8973_PRIVATE_H
#define AKM8973_PRIVATE_H

#include <linux/ioctl.h>
#define RBUFF_SIZE		4	/* Rx buffer size */
#define AKMIO                           0xA1

/*
 * IOCTLs for AKM library 
 */
#define ECS_IOCTL_INIT                  _IO(AKMIO, 0x31) 						//_IO(AKMIO, 0x01)
#define ECS_IOCTL_WRITE                 _IOW(AKMIO, 0x32, char[5])          	//_IOW(AKMIO, 0x02, char[5])
#define ECS_IOCTL_READ                  _IOWR(AKMIO, 0x33, char[5])				//_IOWR(AKMIO, 0x03, char[5])
#define ECS_IOCTL_RESET      	        _IO(AKMIO, 0x34)						//_IO(AKMIO, 0x04)
#define ECS_IOCTL_SET_MODE              _IOW(AKMIO, 0x37, short)				//_IOW(AKMIO, 0x07, short)
#define ECS_IOCTL_GETDATA               _IOR(AKMIO, 0x38, char[RBUFF_SIZE+1])	//_IOR(AKMIO, 0x08, char[RBUFF_SIZE+1])
#define ECS_IOCTL_GET_NUMFRQ            _IOR(AKMIO, 0x39, char[2])				//_IOR(AKMIO, 0x09, char[2])
#define ECS_IOCTL_SET_YPR               _IOW(AKMIO, 0x3C, short[12])			//_IOW(AKMIO, 0x0C, short[12])
#define ECS_IOCTL_GET_OPEN_STATUS       _IOR(AKMIO, 0x3D, int)					//_IOR(AKMIO, 0x0D, int)
#define ECS_IOCTL_GET_CLOSE_STATUS      _IOR(AKMIO, 0x3E, int)					//_IOR(AKMIO, 0x0E, int)
#define ECS_IOCTL_GET_DELAY             _IOR(AKMIO, 0x31, short) 				//_IOR(AKMIO, 0x30, short) 	

#endif