/*
 * drivers/media/video/msm/mt9p111_samsung.c
 *
 * Refer to drivers/media/video/msm/mt9d112.c
 * For IC MT9P111 of Module SAMSUNG: 5.0Mp, 1/4-Inch System-On-A-Chip (SOC) CMOS Digital Image Sensor
 *
 * Copyright (C) 2009-2010 ZTE Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Created by jia.jia@zte.com.cn
 */

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <media/msm_camera.h>
#include <mach/gpio.h>
#include "mt9p111.h"

/*-----------------------------------------------------------------------------------------
 *
 * MACRO DEFINITION
 *
 *----------------------------------------------------------------------------------------*/
#define MT9P111_SENSOR_PROBE_INIT

#ifdef MT9P111_SENSOR_PROBE_INIT
#define MT9P111_PROBE_WORKQUEUE
#endif /* define MT9P111_SENSOR_PROBE_INIT */

#if defined(MT9P111_PROBE_WORKQUEUE)
#include <linux/workqueue.h>
static struct platform_device *pdev_wq = NULL;
static struct workqueue_struct *mt9p111_wq = NULL;
static void mt9p111_workqueue(struct work_struct *work);
static DECLARE_WORK(mt9p111_cb_work, mt9p111_workqueue);
#endif /* defined(MT9P111_PROBE_WORKQUEUE) */

#define MT9P111_I2C_BOARD_NAME "mt9p111"

#define MT9P111_I2C_BUS_ID  (0)

#define MT9P111_SLAVE_WR_ADDR 0x7A /* replaced by "msm_i2c_devices.addr" */
#define MT9P111_SLAVE_RD_ADDR 0x7B /* replaced by "msm_i2c_devices.addr" */

#define MT9P111_EPPROM_SLAVE_ADDR  (0xA0 >> 1)

#define REG_MT9P111_MODEL_ID    0x0000
#define MT9P111_MODEL_ID        0x2880

#define REG_MT9P111_SENSOR_RESET     0x001A
#define REG_MT9P111_STANDBY_CONTROL  0x0018

#define MT9P111_CAMIO_MCLK  24000000    /* UNCONFIRMED */

#define MT9P111_GPIO_SHUTDOWN_CTL   32

#define MT9P111_GPIO_SWITCH_CTL     39

#if defined(CONFIG_MACH_RAISE)
#define MT9P111_GPIO_SWITCH_VAL     1
#elif defined(CONFIG_MACH_JOE)
#define MT9P111_GPIO_SWITCH_VAL     0
#else
#define MT9P111_GPIO_SWITCH_VAL     1
#endif /* defined(CONFIG_MACH_RAISE) */

#define MT9P111_AF_WINDOW_FULL_WIDTH            256
#define MT9P111_AF_WINDOW_FULL_HEIGHT           256
#define MT9P111_TOUCH_AF_WINDOW_DEFAULT_WIDTH   86
#define MT9P111_TOUCH_AF_WINDOW_DEFAULT_HEIGHT  86

/*-----------------------------------------------------------------------------------------
 *
 * TYPE DECLARATION
 *
 *----------------------------------------------------------------------------------------*/
struct mt9p111_work_t {
    struct work_struct work;
};

struct mt9p111_ctrl_t {
    const struct msm_camera_sensor_info *sensordata;
};

/*-----------------------------------------------------------------------------------------
 *
 * GLOBAL VARIABLE DEFINITION
 *
 *----------------------------------------------------------------------------------------*/
static struct mt9p111_work_t *mt9p111_sensorw;
static struct i2c_client *mt9p111_client;
static struct mt9p111_ctrl_t *mt9p111_ctrl;

DECLARE_MUTEX(mt9p111_sem);

static struct wake_lock mt9p111_wake_lock;

/*-----------------------------------------------------------------------------------------
 *
 * FUNCTION DECLARATION
 *
 *----------------------------------------------------------------------------------------*/
static int32_t mt9p111_i2c_add_driver(void);
static void mt9p111_i2c_del_driver(void);

extern int32_t msm_camera_power_backend(enum msm_camera_pwr_mode_t pwr_mode);
extern int msm_camera_clk_switch(const struct msm_camera_sensor_info *data,
                                         uint32_t gpio_switch,
                                         uint32_t switch_val);

#ifdef CONFIG_ZTE_PLATFORM
#ifdef CONFIG_ZTE_FTM_FLAG_SUPPORT
extern int zte_get_ftm_flag(void);
#endif
#endif

/*-----------------------------------------------------------------------------------------
 *
 * FUNCTION DEFINITION
 *
 *----------------------------------------------------------------------------------------*/
static inline void mt9p111_init_suspend(void)
{
    CDBG("%s: entry\n", __func__);
    wake_lock_init(&mt9p111_wake_lock, WAKE_LOCK_IDLE, "mt9p111");
}

static inline void mt9p111_deinit_suspend(void)
{
    CDBG("%s: entry\n", __func__);
    wake_lock_destroy(&mt9p111_wake_lock);
}

static inline void mt9p111_prevent_suspend(void)
{
    CDBG("%s: entry\n", __func__);
    wake_lock(&mt9p111_wake_lock);
}

static inline void mt9p111_allow_suspend(void)
{
    CDBG("%s: entry\n", __func__);
    wake_unlock(&mt9p111_wake_lock);
}

static int mt9p111_hard_standby(const struct msm_camera_sensor_info *dev, uint32_t on)
{
    int rc;

    CDBG("%s: entry\n", __func__);

    rc = gpio_request(dev->sensor_pwd, "mt9p111");
    if (0 == rc)
    {
        /* ignore "rc" */
        rc = gpio_direction_output(dev->sensor_pwd, on);

        mdelay(200);
    }

    gpio_free(dev->sensor_pwd);

    return rc;
}

/*
 * Hard reset: RESET_BAR pin (active LOW)
 * Hard reset has the same effect as the soft reset.
 */
static int mt9p111_hard_reset(const struct msm_camera_sensor_info *dev)
{
    int rc = 0;

    CDBG("%s: entry\n", __func__);

    rc = gpio_request(dev->sensor_reset, "mt9p111");
    if (0 == rc)
    {
        /* ignore "rc" */
        rc = gpio_direction_output(dev->sensor_reset, 1);

        mdelay(1);

        /* ignore "rc" */
        rc = gpio_direction_output(dev->sensor_reset, 0);

        udelay(3);

        /* ignore "rc" */
        rc = gpio_direction_output(dev->sensor_reset, 1);

        udelay(100);
    }

    gpio_free(dev->sensor_reset);

    return rc;
}

static int32_t mt9p111_i2c_txdata(unsigned short saddr,
                                       unsigned char *txdata,
                                       int length)
{
    struct i2c_msg msg[] = {
        {
            .addr  = saddr,
            .flags = 0,
            .len   = length,
            .buf   = txdata,
        },
    };

    if (i2c_transfer(mt9p111_client->adapter, msg, 1) < 0)
    {
        CCRT("%s: failed!\n", __func__);
        return -EIO;
    }

    return 0;
}

static int32_t mt9p111_i2c_write(unsigned short saddr,
                                      unsigned short waddr,
                                      unsigned short wdata,
                                      enum mt9p111_width_t width)
{
    int32_t rc = -EFAULT;
    unsigned char buf[4];

    memset(buf, 0, sizeof(buf));

    switch (width)
    {
        case WORD_LEN:
        {
            buf[0] = (waddr & 0xFF00) >> 8;
            buf[1] = (waddr & 0x00FF);
            buf[2] = (wdata & 0xFF00) >> 8;
            buf[3] = (wdata & 0x00FF);

            rc = mt9p111_i2c_txdata(saddr, buf, 4);
        }
        break;

        case BYTE_LEN:
        {
            buf[0] = (waddr & 0xFF00) >> 8;
            buf[1] = (waddr & 0x00FF);
            buf[2] = wdata;

            rc = mt9p111_i2c_txdata(saddr, buf, 3);
        }
        break;

        default:
        {
        }
        break;
    }

    if (rc < 0)
    {
        CCRT("%s: waddr = 0x%x, wdata = 0x%x, failed!\n", __func__, waddr, wdata);
    }

    return rc;
}

static int32_t mt9p111_i2c_write_table(struct mt9p111_i2c_reg_conf const *reg_conf_tbl,
                                             int len)
{
    uint32_t i;
    int32_t rc = 0;

    for (i = 0; i < len; i++)
    {
        rc = mt9p111_i2c_write(mt9p111_client->addr,
                               reg_conf_tbl[i].waddr,
                               reg_conf_tbl[i].wdata,
                               reg_conf_tbl[i].width);
        if (rc < 0)
        {
            break;
        }

        if (reg_conf_tbl[i].mdelay_time != 0)
        {
            mdelay(reg_conf_tbl[i].mdelay_time);
        }
    }

    return rc;
}

static int mt9p111_i2c_rxdata(unsigned short saddr,
                                   unsigned char *rxdata,
                                   int length)
{
    struct i2c_msg msgs[] = {
        {
            .addr  = saddr,
            .flags = 0,
            .len   = 2,
            .buf   = rxdata,
        },
        {
            .addr  = saddr,
            .flags = I2C_M_RD,
            .len   = length,
            .buf   = rxdata,
        },
    };

    if (i2c_transfer(mt9p111_client->adapter, msgs, 2) < 0)
    {
        CCRT("%s: failed!\n", __func__);
        return -EIO;
    }

    return 0;
}

static int32_t mt9p111_i2c_read(unsigned short saddr,
                                     unsigned short raddr,
                                     unsigned short *rdata,
                                     enum mt9p111_width_t width)
{
    int32_t rc = 0;
    unsigned char buf[4];

    if (!rdata)
    {
        CCRT("%s: rdata points to NULL!\n", __func__);
        return -EIO;
    }

    memset(buf, 0, sizeof(buf));

    switch (width)
    {
        case WORD_LEN:
        {
            buf[0] = (raddr & 0xFF00) >> 8;
            buf[1] = (raddr & 0x00FF);

            rc = mt9p111_i2c_rxdata(saddr, buf, 2);
            if (rc < 0)
            {
                CCRT("%s: failed!\n", __func__);
                return rc;
            }

            *rdata = buf[0] << 8 | buf[1];
        }
        break;

        case BYTE_LEN:
        {
            buf[0] = (raddr & 0xFF00) >> 8;
            buf[1] = (raddr & 0x00FF);

            rc = mt9p111_i2c_rxdata(saddr, buf, 2);
            if (rc < 0)
            {
                CCRT("%s: failed!\n", __func__);
                return rc;
            }

            *rdata = 0x0000 | buf[0];
        }
        break;

        default:
        {
        }
        break;
    }

    return rc;
}

static int mt9p111_eeprom_i2c_rxdata(unsigned short saddr,
                                              unsigned char *rxdata,
                                              int length)
{
    struct i2c_msg msgs[] = {
        {
            .addr  = saddr,
            .flags = 0,
            .len   = 1,
            .buf   = rxdata,
        },
        {
            .addr  = saddr,
            .flags = I2C_M_RD,
            .len   = length,
            .buf   = rxdata,
        },
    };

    if (i2c_transfer(mt9p111_client->adapter, msgs, 2) < 0)
    {
        CCRT("%s: failed!\n", __func__);
        return -EIO;
    }

    return 0;
}

static int32_t mt9p111_eeprom_i2c_read(unsigned short saddr,
                                                uint8_t raddr,
                                                unsigned short *rdata,
                                                enum mt9p111_width_t width)
{
    int32_t rc = 0;
    unsigned char buf[4];

    if (!rdata)
    {
        CCRT("%s: rdata points to NULL!\n", __func__);
        return -EIO;
    }

    memset(buf, 0, sizeof(buf));

    buf[0] = raddr;

    switch (width)
    {
        case WORD_LEN:
        {
            rc = mt9p111_eeprom_i2c_rxdata(saddr, buf, 2);
            if (rc < 0)
            {
                CCRT("%s: failed!\n", __func__);
                return rc;
            }

            *rdata = buf[0] << 8 | buf[1];
        }
        break;

        case BYTE_LEN:
        {
            rc = mt9p111_eeprom_i2c_rxdata(saddr, buf, 1);
            if (rc < 0)
            {
                CCRT("%s: failed!\n", __func__);
                return rc;
            }

            *rdata = buf[0];
        }
        break;

        default:
        {
        }
        break;
    }

    return rc;
}

static int32_t mt9p111_af_trigger(void)
{
    uint16_t af_status;
    uint32_t i;
    int32_t rc;
    
    uint16_t window_width = 0;

    CDBG("%s: entry\n", __func__);

    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0xC400, WORD_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0xC400, 0x88, BYTE_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8404, WORD_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x0990, 0x05, BYTE_LEN);
    mdelay(150);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x3002, WORD_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB002, 0x0085, WORD_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8419, WORD_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8419, 0x05, BYTE_LEN);

    rc = mt9p111_i2c_read(mt9p111_client->addr,0xB856, &window_width, BYTE_LEN);
    if(window_width != 0xBF) {       
        rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0xB854, WORD_LEN);

        rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB854, 0x20, BYTE_LEN);

        rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB855, 0x20, BYTE_LEN);

        rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB856, 0xBF, BYTE_LEN);

        rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB857, 0xBF, BYTE_LEN); 
    }

    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN);
    
    mdelay(150);

    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0xB006, WORD_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB006, 0x01, BYTE_LEN);

    mdelay(150);

    af_status = 0x0000;
    for (i = 0; i < 1500; ++i)
    {       
        rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x3000, WORD_LEN);
        if (rc < 0)
        {
            return rc;
        }

        af_status = 0x0000;
        rc = mt9p111_i2c_read(mt9p111_client->addr, 0xB000, &af_status, WORD_LEN);    
        if (rc < 0)
        {
            return rc;
        }

        if (0x0010 == af_status)
        {
            return 0;
        }

        mdelay(20);
    }

    return -EIO;
}

static int32_t mt9p111_set_lens_roll_off(void)
{
    int32_t rc = 0;
    rc = mt9p111_i2c_write_table(mt9p111_regs.rftbl, mt9p111_regs.rftbl_size);
    return rc;
}

static int32_t mt9p111_set_exposure_compensation(int8_t exposure)
{
    int32_t rc = 0;

    CCRT("%s: entry: exposure=%d\n", __func__, exposure);

    switch (exposure)
    {
        case CAMERA_EXPOSURE_0:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.exposure_tbl[0],
                                         mt9p111_regs.exposure_tbl_sz[0]);
        }
        break;

        case CAMERA_EXPOSURE_1:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.exposure_tbl[1],
                                         mt9p111_regs.exposure_tbl_sz[1]);
        }
        break;

        case CAMERA_EXPOSURE_2:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.exposure_tbl[2],
                                         mt9p111_regs.exposure_tbl_sz[2]);
        }
        break;
        
        case CAMERA_EXPOSURE_3:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.exposure_tbl[3],
                                         mt9p111_regs.exposure_tbl_sz[3]);
        }
        break; 

        case CAMERA_EXPOSURE_4:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.exposure_tbl[4],
                                         mt9p111_regs.exposure_tbl_sz[4]);
        }
        break;

        default:
        {
            CCRT("%s: parameter error!\n", __func__);
            rc = -EFAULT;
        }     
    }

    mdelay(100);

    return rc;
}  

static long mt9p111_reg_init(void)
{
    long rc;
    uint32_t num;
    uint16_t value;

    CDBG("%s: entry\n", __func__);

    rc = mt9p111_i2c_write_table(mt9p111_regs.plltbl, mt9p111_regs.plltbl_size);
    if (rc < 0)
    {
        return rc;
    }

    for(num = 0; num < mt9p111_regs.lsc_reg_addr_tbl_sz; num++)
    {    
        value = 0x0000;
        rc = mt9p111_eeprom_i2c_read(MT9P111_EPPROM_SLAVE_ADDR, mt9p111_regs.lsc_reg_addr_tbl[num].eeprom_addr, 
                                     &value, WORD_LEN);
        if (rc < 0)
        {
            CCRT("%s: read EEPROM LSC failed!, addr=0x%x\n", __func__, mt9p111_regs.lsc_reg_addr_tbl[num].eeprom_addr);
            return rc;
        }
        rc = mt9p111_i2c_write(mt9p111_client->addr, mt9p111_regs.lsc_reg_addr_tbl[num].sensor_addr, value, WORD_LEN);
        if (rc < 0)
        {
            CCRT("%s: write EEPROM LSC to sensor failed!\n", __func__);
            return rc;
        }
    }
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x3210, 0x49B8, WORD_LEN);
    if (rc < 0)
    {
        return rc;
    }

    rc = mt9p111_i2c_write_table(mt9p111_regs.prev_snap_reg_settings, mt9p111_regs.prev_snap_reg_settings_size);
    if (rc < 0)
    {
        return rc;
    }

    rc = mt9p111_i2c_write_table(mt9p111_regs.noise_reduction_reg_settings, mt9p111_regs.noise_reduction_reg_settings_size);
    if (rc < 0)
    {
        return rc;
    }

    rc = mt9p111_i2c_write_table(mt9p111_regs.stbl, mt9p111_regs.stbl_size);
    if (rc < 0)
    {
        return rc;
    }

    rc = mt9p111_set_lens_roll_off();
    if (rc < 0)
    {
        return rc;
    }

#if defined(CONFIG_SENSOR_ADAPTER)
    rc = mt9p111_i2c_write(mt9p111_client->addr, REG_MT9P111_STANDBY_CONTROL, 0x2008, WORD_LEN);
    if (rc < 0)
    {
        return rc;
    }
    mdelay(100);
#else
    // Do nothing
#endif

    return 0;
}

static long mt9p111_set_sensor_mode(int32_t mode)
{
    long rc = 0;
    uint16_t times = 0;
    uint16_t value = 0;

    CDBG("%s: entry\n", __func__);

    switch (mode)
    {
        case SENSOR_PREVIEW_MODE:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr,0x098E, 0x8419,WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr,0x8419, 0x01,BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr,0x098E, 0x8404,WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr,0x8404, 0x06,BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }
            
            mdelay(100);
            
            rc = mt9p111_i2c_write(mt9p111_client->addr,0x098E, 0xB007,WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr,0xB007, 0x00,BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }

            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x843C, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }

            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x843C, 0x01, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }

            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x01, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }

            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x0016, 0x0447, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            
            while (value != 0x03 && times != 2000)
            {
                rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8405, WORD_LEN);
                if(rc < 0)
                {
                    return rc;
                }
                value = 0;
                rc = mt9p111_i2c_read(mt9p111_client->addr, 0x8405, &value, BYTE_LEN);
                if(rc < 0)
                {
                    return rc;
                }
                
                mdelay(50);
                
                times++;
            }
        }
        break;

        case SENSOR_SNAPSHOT_MODE:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x843C, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }

            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x843C, 0xFF, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }

            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x02, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }

            while (value != 0x07 && times != 2000)
            {
                rc = mt9p111_i2c_write(mt9p111_client->addr,0x098E, 0x8405, WORD_LEN);
                if(rc < 0)
                {
                    return rc;
                }

                value = 0;
                rc = mt9p111_i2c_read(mt9p111_client->addr,0x8405, &value, BYTE_LEN);
                if(rc < 0)
                {
                    return rc;
                }
                
                mdelay(50);
                
                times++;
            }
        }
        break;

        default:
        {
            return -EFAULT;
        }
    }

    return 0;
}

static long mt9p111_set_effect(int32_t mode, int32_t effect)
{
    uint16_t __attribute__((unused)) reg_addr;
    uint16_t __attribute__((unused)) reg_val;
    long rc = 0;

    switch (mode)
    {
        case SENSOR_PREVIEW_MODE:
        {
            /* Context A Special Effects */
            /* add code here
                 e.g.
                 reg_addr = 0xXXXX;
               */
        }
        break;

        case SENSOR_SNAPSHOT_MODE:
        {
            /* Context B Special Effects */
            /* add code here
                 e.g.
                 reg_addr = 0xXXXX;
               */
        }
        break;

        default:
        {
            /* add code here
                 e.g.
                 reg_addr = 0xXXXX;
               */
        }
        break;
    }

    mdelay(50);

    switch (effect)
    {
        case CAMERA_EFFECT_OFF:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0xDC38, WORD_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xDC38, 0x00, BYTE_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xDC02, 0x306E, WORD_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN);
            if(rc < 0)
            {
                return rc;
            }
        }
        break;

        case CAMERA_EFFECT_MONO:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0xDC38, WORD_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xDC38, 0x01, BYTE_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xDC02, 0x306E, WORD_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN);
            if(rc < 0)
            {
                return rc;
            }
        }
        break;         

        case CAMERA_EFFECT_NEGATIVE:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0xDC38, WORD_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xDC38, 0x03, BYTE_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xDC02, 0x306E, WORD_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN);
            if(rc < 0)
            {
                return rc;
            }
        }
        break;          

        case CAMERA_EFFECT_SOLARIZE:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0xDC38, WORD_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xDC38, 0x04, BYTE_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xDC39, 0xC0, BYTE_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xDC02, 0x306E, WORD_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN);
            if(rc < 0)
            {
                return rc;
            }
        }
        break;   

        case CAMERA_EFFECT_SEPIA:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0xDC38, WORD_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xDC38, 0x02, BYTE_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xDC3A, 0x23, BYTE_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xDC3B, 0xB1, BYTE_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xDC02, 0x306E, WORD_LEN);
            if(rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN);
            if(rc < 0)
            {
                return rc;
            }       
        }
        break;

        default:
        {

            return -EFAULT;
        }
    }

    mdelay(50);

    return rc;
}

/*
 * Power-up Process
 */
static long mt9p111_power_up(void)
{
    CDBG("%s: not supported!\n", __func__);
    return 0;
}

/*
 * Power-down Process
 */
static long mt9p111_power_down(void)
{
    CDBG("%s: not supported!\n", __func__);
    return 0;
}

#if 0
static long mt9p111_move_focus(int32_t mode)
{
    long rc = 0;

    CDBG("%s: entry\n", __func__);

    rc = mt9p111_af_trigger();

    return rc;
}
#endif

#if defined(CONFIG_MACH_RAISE) || defined(CONFIG_MACH_JOE)
static int mt9p111_power_shutdown(uint32_t on)
#elif defined(CONFIG_MACH_MOONCAKE)
static int __attribute__((unused)) mt9p111_power_shutdown(uint32_t on)
#else
static int __attribute__((unused)) mt9p111_power_shutdown(uint32_t on)
#endif /* defined(CONFIG_MACH_RAISE) || defined(CONFIG_MACH_JOE) */
{
    int rc;

    CDBG("%s: entry\n", __func__);

    rc = gpio_request(MT9P111_GPIO_SHUTDOWN_CTL, "mt9p111");
    if (0 == rc)
    {
        rc = gpio_direction_output(MT9P111_GPIO_SHUTDOWN_CTL, on);

        mdelay(1);
    }

    gpio_free(MT9P111_GPIO_SHUTDOWN_CTL);

    return rc;
}

static int32_t mt9p111_set_wb(int8_t wb_mode)
{
    int32_t rc = 0;

    CDBG("%s: entry: wb_mode=%d\n", __func__, wb_mode);

    switch (wb_mode)
    {
        case CAMERA_WB_MODE_AWB:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.wb_auto_tbl, 
                                         mt9p111_regs.wb_auto_tbl_sz);
        }
        break;

        case CAMERA_WB_MODE_SUNLIGHT:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.wb_daylight_tbl,
                                         mt9p111_regs.wb_daylight_tbl_sz);
        }
        break;

        case CAMERA_WB_MODE_INCANDESCENT:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.wb_incandescent_tbl,
                                         mt9p111_regs.wb_incandescent_tbl_sz);
        }
        break;
        
        case CAMERA_WB_MODE_FLUORESCENT:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.wb_flourescant_tbl,
                                         mt9p111_regs.wb_flourescant_tbl_sz);
        }
        break; 

        case CAMERA_WB_MODE_CLOUDY:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.wb_cloudy_tbl,
                                         mt9p111_regs.wb_cloudy_tbl_sz);
        }
        break;

        default:
        {
            CCRT("%s: parameter error!\n", __func__);
            rc = -EFAULT;
        }     
    }

    return rc;
}    

static int32_t mt9p111_set_contrast(int8_t contrast)
{
    int32_t rc = 0;

    CCRT("%s: entry: contrast=%d\n", __func__, contrast);

    switch (contrast)
    {
        case CAMERA_CONTRAST_0:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.contrast_tbl[0],
                                         mt9p111_regs.contrast_tbl_sz[0]);
        }
        break;

        case CAMERA_CONTRAST_1:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.contrast_tbl[1],
                                         mt9p111_regs.contrast_tbl_sz[1]);
        }
        break;

        case CAMERA_CONTRAST_2:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.contrast_tbl[2],
                                         mt9p111_regs.contrast_tbl_sz[2]);
        }
        break;
        
        case CAMERA_CONTRAST_3:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.contrast_tbl[3],
                                         mt9p111_regs.contrast_tbl_sz[3]);
        }
        break; 

        case CAMERA_CONTRAST_4:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.contrast_tbl[4],
                                         mt9p111_regs.contrast_tbl_sz[4]);
        }
        break;

        default:
        {
            CCRT("%s: parameter error!\n", __func__);
            rc = -EFAULT;
        }     
    }

    mdelay(100);

    return rc;
}

static int32_t mt9p111_set_brightness(int8_t brightness)
{
    int32_t rc = 0;

    CCRT("%s: entry: brightness=%d\n", __func__, brightness);

    switch (brightness)
    {
        case CAMERA_BRIGHTNESS_0:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.brightness_tbl[0],
                                         mt9p111_regs.brightness_tbl_sz[0]);
        }
        break;

        case CAMERA_BRIGHTNESS_1:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.brightness_tbl[1],
                                         mt9p111_regs.brightness_tbl_sz[1]);
        }
        break;

        case CAMERA_BRIGHTNESS_2:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.brightness_tbl[2],
                                         mt9p111_regs.brightness_tbl_sz[2]);
        }
        break;
        
        case CAMERA_BRIGHTNESS_3:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.brightness_tbl[3],
                                         mt9p111_regs.brightness_tbl_sz[3]);
        }
        break; 

        case CAMERA_BRIGHTNESS_4:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.brightness_tbl[4],
                                         mt9p111_regs.brightness_tbl_sz[4]);
        }
        break;
        
        case CAMERA_BRIGHTNESS_5:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.brightness_tbl[5],
                                         mt9p111_regs.brightness_tbl_sz[5]);
        }
        break;
        
        case CAMERA_BRIGHTNESS_6:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.brightness_tbl[6],
                                         mt9p111_regs.brightness_tbl_sz[6]);
        }        
        break;

        default:
        {
            CCRT("%s: parameter error!\n", __func__);
            rc = -EFAULT;
        }     
    }

    return rc;
}    

static int32_t mt9p111_set_saturation(int8_t saturation)
{
    int32_t rc = 0;

    CCRT("%s: entry: saturation=%d\n", __func__, saturation);

    switch (saturation)
    {
        case CAMERA_SATURATION_0:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.saturation_tbl[0],
                                         mt9p111_regs.saturation_tbl_sz[0]);
        }
        break;

        case CAMERA_SATURATION_1:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.saturation_tbl[1],
                                         mt9p111_regs.saturation_tbl_sz[1]);
        }
        break;

        case CAMERA_SATURATION_2:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.saturation_tbl[2],
                                         mt9p111_regs.saturation_tbl_sz[2]);
        }
        break;
        
        case CAMERA_SATURATION_3:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.saturation_tbl[3],
                                         mt9p111_regs.saturation_tbl_sz[3]);
        }
        break; 

        case CAMERA_SATURATION_4:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.saturation_tbl[4],
                                         mt9p111_regs.saturation_tbl_sz[4]);
        }
        break;

        default:
        {
            CCRT("%s: parameter error!\n", __func__);
            rc = -EFAULT;
        }     
    }

    mdelay(100);

    return rc;
}    

static int32_t mt9p111_set_sharpness(int8_t sharpness)
{
    int32_t rc = 0;

    CCRT("%s: entry: sharpness=%d\n", __func__, sharpness);

    switch (sharpness)
    {
        case CAMERA_SHARPNESS_0:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.sharpness_tbl[0],
                                         mt9p111_regs.sharpness_tbl_sz[0]);
        }
        break;

        case CAMERA_SHARPNESS_1:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.sharpness_tbl[1],
                                         mt9p111_regs.sharpness_tbl_sz[1]);
        }
        break;

        case CAMERA_SHARPNESS_2:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.sharpness_tbl[2],
                                         mt9p111_regs.sharpness_tbl_sz[2]);
        }
        break;
        
        case CAMERA_SHARPNESS_3:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.sharpness_tbl[3],
                                         mt9p111_regs.sharpness_tbl_sz[3]);
        }
        break; 

        case CAMERA_SHARPNESS_4:
        {
            rc = mt9p111_i2c_write_table(mt9p111_regs.sharpness_tbl[4],
                                         mt9p111_regs.sharpness_tbl_sz[4]);
        }
        break;        

        default:
        {
            CCRT("%s: parameter error!\n", __func__);
            rc = -EFAULT;
        }     
    }

    return rc;
}    

/*
 * ISO Setting
 */
static int32_t mt9p111_set_iso(int8_t iso_val)
{
    int32_t rc = 0;

    CCRT("%s: entry: iso_val=%d\n", __func__, iso_val);

    switch (iso_val)
    {
        case CAMERA_ISO_SET_AUTO:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x281C, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA81C, 0x0060, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA81E, 0x00DC, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA820, 0x0154, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8404, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
		        rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }          
        }
        break;

        case CAMERA_ISO_SET_HJR:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x4842, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x0990, 0x0046, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x4840, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x0990, 0x01FC, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8400, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x0990, 0x0006, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }          
        }
        break;

        case CAMERA_ISO_SET_100:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x281C, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA81C, 0x0060, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA81E, 0x00DC, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA820, 0x0154, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }

            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8404, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }   
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }   
        }
        break;

        case CAMERA_ISO_SET_200:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x281C, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA81C, 0x0080, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA81E, 0x00DC, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }

            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA820, 0x0154, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
 
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8404, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }   
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }   
        }
        break;

        case CAMERA_ISO_SET_400:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x281C, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA81C, 0x00B0, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA81E, 0x00E2, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }

            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA820, 0x0164, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
 
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8404, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }   
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }   
        }
        break;

        case CAMERA_ISO_SET_800:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x281C, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA81C, 0x0100, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA81E, 0x00EC, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }

            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA820, 0x0174, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }  
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8404, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }   
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }   
        }
        break;

        default:
        {
            CCRT("%s: parameter error!\n", __func__);
            rc = -EFAULT;
        }     
    }

    mdelay(300);

    return rc;
} 

static int32_t mt9p111_set_aec_rio(aec_rio_cfg position)
{	
    int32_t rc = 0;	
    uint16_t x_ori = 0;
    uint16_t y_ori = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    int16_t  left = 0;
    int16_t  right = 0;
    int16_t  top = 0;
    int16_t  bottom = 0;
    uint16_t af_status;
    uint32_t i;
    uint16_t window_full_width;
    uint16_t window_full_height;
    uint16_t window_default_height;
    uint16_t window_default_width;
    window_full_width = MT9P111_AF_WINDOW_FULL_WIDTH;
    window_full_height = MT9P111_AF_WINDOW_FULL_HEIGHT;
    window_default_width = MT9P111_TOUCH_AF_WINDOW_DEFAULT_WIDTH;
    window_default_height = MT9P111_TOUCH_AF_WINDOW_DEFAULT_HEIGHT;

    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0xC400, WORD_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0xC400, 0x88, BYTE_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8404, WORD_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x0990, 0x05, BYTE_LEN);
    mdelay(150);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x3002, WORD_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB002, 0x0085, WORD_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8419, WORD_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8419, 0x05, BYTE_LEN);

    x_ori = position.x * window_full_width/position.preview_width;
    y_ori = position.y * window_full_height/position.preview_height;
    left = x_ori - window_default_width/2;
    right = x_ori + window_default_width/2;
    top = y_ori - window_default_height/2;
    bottom = y_ori + window_default_height/2;

    if (left < 0)
    {
        x = 0;
    }
    else if (right > (window_full_width - 1))
    {
        x = window_full_width - 1 - window_default_width;
    }
    else
    {
        x = left;
    }

    if (top < 0)
    {
        y = 0;
    }
    else if (bottom > (window_full_height - 1))
    {
        y = window_full_height - 1 - window_default_height;
    }
    else
    {
        y = top;
    }

    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0xB854, WORD_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB854, x, BYTE_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB855, y, BYTE_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB856, window_default_width, BYTE_LEN);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB857, window_default_height, BYTE_LEN); 

    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8404, WORD_LEN); 
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN);

    mdelay(150);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0xB006, WORD_LEN); 
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB006, 0x01, BYTE_LEN); 
    mdelay(150);

    af_status = 0x0000;
    for (i = 0; i < 1500; ++i)
    {       
        rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x3000, WORD_LEN);
        if (rc < 0)
        {
            return rc;
        }

        af_status = 0x0000;
        rc = mt9p111_i2c_read(mt9p111_client->addr, 0xB000, &af_status, WORD_LEN);    
        if (rc < 0)
        {
            return rc;
        }

        if (0x0010 == af_status)
        {
            return 0;
        }

        mdelay(20);
    }

    return -EIO;
}

static int32_t mt9p111_set_anti_shake(int8_t antishake)
{	
    int32_t rc = 0;	
    CCRT("%s: entry: antishake=%d\n", __func__, antishake);
    
    switch(antishake) {
        case CAMERA_ANTISHAKE_OFF:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x841E, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x841E, 0x02, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8420, 0x02, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8422, 0x00, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB85A, 0x2414, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
        }
        break;
        case CAMERA_ANTISHAKE_ON:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0xB40B, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB40B, 0x03, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB40C, 0x01FC, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB40E, 0x0180, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB41C, 0xFFFF, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xB85A, 0x1414, WORD_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x841E, 0x00, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8420, 0x00, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8422, 0x01, BYTE_LEN);
            if (rc < 0)
            {
                return rc;
            }
        }
        break;
        default:
        {
            CCRT("%s: parameter error!\n", __func__);
            rc = -EFAULT;
        }   
    }

    return rc;
   
}

/*
 * Antibanding Setting
 */
static int32_t mt9p111_set_antibanding(int8_t antibanding)
{
    int32_t rc = 0;

    CCRT("%s: entry: antibanding=%d\n", __func__, antibanding);

    switch (antibanding)
    {
        case CAMERA_ANTIBANDING_SET_OFF:
        {
            CCRT("%s: CAMERA_ANTIBANDING_SET_OFF NOT supported!\n", __func__);
        }
        break;

        case CAMERA_ANTIBANDING_SET_60HZ:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8417, WORD_LEN); // LOGICAL_ADDRESS_ACCESS [FD_MAX_NUM_AUTOCOR_FUNC_VALUES_TO_CHECK]
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8417, 0x01, BYTE_LEN); // FD_STATUS
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA004, 0x3C, BYTE_LEN); // SEQ_STATE_CFG_1_FD
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN); // SEQ_CMD
            if (rc < 0)
            {
                return rc;
            }          
        }            
        break;

        case CAMERA_ANTIBANDING_SET_50HZ:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8417, WORD_LEN);	// LOGICAL_ADDRESS_ACCESS [FD_MAX_NUM_AUTOCOR_FUNC_VALUES_TO_CHECK]
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8417, 0x01, BYTE_LEN); // FD_STATUS
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0xA004, 0x32, BYTE_LEN); // SEQ_STATE_CFG_1_FD
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN); // SEQ_CMD
            if (rc < 0)
            {
                return rc;
            }
        }
        break;

        case CAMERA_ANTIBANDING_SET_AUTO:
        {
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x8417, WORD_LEN);	// LOGICAL_ADDRESS_ACCESS [FD_MAX_NUM_AUTOCOR_FUNC_VALUES_TO_CHECK]
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8417, 0x02, BYTE_LEN); // FD_STATUS
            if (rc < 0)
            {
                return rc;
            }
            rc = mt9p111_i2c_write(mt9p111_client->addr, 0x8404, 0x06, BYTE_LEN); // SEQ_CMD
            if (rc < 0)
            {
                return rc;
            }
        }
        break;

        default:
        {
            CCRT("%s: parameter error!\n", __func__);
            rc = -EFAULT;
        }     
    }

    mdelay(100);

    return rc;
} 

/*
 * Len Shading Setting
 */
static int32_t __attribute__((unused)) mt9p111_set_lensshading(int8_t lensshading)
{
#if 0
    int32_t rc = 0;
    uint16_t brightness_lev = 0;

    CCRT("%s: entry: lensshading=%d\n", __func__, lensshading);

    if (0 == lensshading)
    {
        CCRT("%s: lens shading is disabled!\n", __func__);
        return rc;
    }

    /*
      * Set lens shading value according to brightness level
      */
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x098E, 0x3835, WORD_LEN);
    if (rc < 0)
    {
        return rc;
    }

    rc = mt9p111_i2c_read(mt9p111_client->addr, 0x0990, &brightness_lev, WORD_LEN);
    if (rc < 0)
    {
        return rc;
    }

    if (brightness_lev < 5)
    {
        rc = mt9p111_i2c_write_table(mt9p111_regs.lens_for_outdoor_tbl, mt9p111_regs.lens_for_outdoor_tbl_sz);
        if (rc < 0)
        {
            return rc;
        } 
    }
    else
    {
        rc = mt9p111_i2c_write_table(mt9p111_regs.lens_for_indoor_tbl, mt9p111_regs.lens_for_indoor_tbl_sz);
        if (rc < 0)
        {
            return rc;
        } 
    }

    return rc;
#else
    return 0;
#endif
}

#if !defined(CONFIG_SENSOR_ADAPTER)
static int mt9p111_sensor_init_probe(const struct msm_camera_sensor_info *data)
{
    uint16_t model_id = 0;
    uint32_t switch_on;
    int rc = 0;

    CDBG("%s: entry\n", __func__);

    /* Disable Lowest-Power State */
#if defined(CONFIG_MACH_RAISE) || defined(CONFIG_MACH_JOE)
    switch_on = 0;
    rc = mt9p111_power_shutdown(switch_on);
    if (rc < 0)
    {
        CCRT("enter/quit lowest-power mode failed!\n");
        goto init_probe_fail;
    }
#elif defined(CONFIG_MACH_MOONCAKE)
    /* Do nothing */
    switch_on = 0;
#else
    /* Do nothing */
    switch_on = 0;
#endif /* defined(CONFIG_MACH_RAISE) || defined(CONFIG_MACH_JOE) */

    switch_on = 0;
    rc = mt9p111_hard_standby(data, switch_on);
    if (rc < 0)
    {
        CCRT("set standby failed!\n");
        goto init_probe_fail;
    }

    rc = mt9p111_hard_reset(data);
    if (rc < 0)
    {
        CCRT("reset failed!\n");
        goto init_probe_fail;
    }

    rc = mt9p111_i2c_read(mt9p111_client->addr, REG_MT9P111_MODEL_ID, &model_id, WORD_LEN);
    if (rc < 0)
    {
        goto init_probe_fail;
    }

    CDBG("%s: model_id = 0x%x\n", __func__, model_id);

#ifdef CONFIG_SENSOR_INFO
    msm_sensorinfo_set_sensor_id(model_id);
#else
    // do nothing here
#endif
    if (model_id != MT9P111_MODEL_ID)
    {
        rc = -EFAULT;
        goto init_probe_fail;
    }

    rc = mt9p111_reg_init();
    if (rc < 0)
    {
        goto init_probe_fail;
    }

    rc = mt9p111_i2c_write(mt9p111_client->addr, REG_MT9P111_STANDBY_CONTROL, 0x2008, WORD_LEN);
    if (rc < 0)
    {
        goto init_probe_fail;
    }

    return rc;

init_probe_fail:
    CCRT("%s: rc = %d, failed!\n", __func__, rc);
    return rc;
}
#else
static int mt9p111_sensor_i2c_probe_on(void)
{
    int rc;
    struct i2c_board_info info;
    struct i2c_adapter *adapter;
    struct i2c_client *client;

    rc = mt9p111_i2c_add_driver();
    if (rc < 0)
    {
        CCRT("%s: add i2c driver failed!\n", __func__);
        return rc;
    }

    memset(&info, 0, sizeof(struct i2c_board_info));
    info.addr = MT9P111_SLAVE_WR_ADDR >> 1;
    strlcpy(info.type, MT9P111_I2C_BOARD_NAME, I2C_NAME_SIZE);

    adapter = i2c_get_adapter(MT9P111_I2C_BUS_ID);
    if (!adapter)
    {
        CCRT("%s: get i2c adapter failed!\n", __func__);
        goto i2c_probe_failed;
    }

    client = i2c_new_device(adapter, &info);
    i2c_put_adapter(adapter);
    if (!client)
    {
        CCRT("%s: add i2c device failed!\n", __func__);
        goto i2c_probe_failed;
    }

    mt9p111_client = client;

    return 0;

i2c_probe_failed:
    mt9p111_i2c_del_driver();
    return -ENODEV;
}

static void mt9p111_sensor_i2c_probe_off(void)
{
    i2c_unregister_device(mt9p111_client);
    mt9p111_i2c_del_driver();
}

static int mt9p111_sensor_dev_probe(const struct msm_camera_sensor_info *pinfo)
{
    int rc;
    uint16_t model_id;
    uint32_t switch_on;
  
    rc = msm_camera_power_backend(MSM_CAMERA_PWRUP_MODE);
    if (rc < 0)
    {
        CCRT("%s: camera_power_backend failed!\n", __func__);
        return rc;
    }

#if defined(CONFIG_MACH_R750) || defined(CONFIG_MACH_JOE)
    rc = msm_camera_clk_switch(pinfo, MT9P111_GPIO_SWITCH_CTL, MT9P111_GPIO_SWITCH_VAL);
    if (rc < 0)
    {
        CCRT("%s: camera_clk_switch failed!\n", __func__);
        return rc;;
    }
#else
    // Do nothing
#endif

    msm_camio_clk_rate_set(MT9P111_CAMIO_MCLK);
    mdelay(5);

    /* Disable Lowest-Power State */
#if defined(CONFIG_MACH_RAISE) || defined(CONFIG_MACH_JOE)
    switch_on = 0;
    rc = mt9p111_power_shutdown(switch_on);
    if (rc < 0)
    {
        CCRT("enter/quit lowest-power mode failed!\n");
        return rc;
    }
#elif defined(CONFIG_MACH_MOONCAKE)
    /* Do nothing */
    switch_on = 0;
#else
    /* Do nothing */
    switch_on = 0;
#endif /* defined(CONFIG_MACH_RAISE) || defined(CONFIG_MACH_JOE) */


    rc = mt9p111_hard_standby(pinfo, 0);
    if (rc < 0)
    {
        CCRT("set standby failed!\n");
        return rc;
    }

    rc = mt9p111_hard_reset(pinfo);
    if (rc < 0)
    {
        CCRT("hard reset failed!\n");
        return rc;
    }


    model_id = 0x0000;
    rc = mt9p111_i2c_read(mt9p111_client->addr, REG_MT9P111_MODEL_ID, &model_id, WORD_LEN);
    if (rc < 0)
    {
        return rc;
    }

    CDBG("%s: model_id = 0x%x\n", __func__, model_id);

#ifdef CONFIG_SENSOR_INFO
    msm_sensorinfo_set_sensor_id(model_id);
#else
    // do nothing here
#endif

    if(model_id != MT9P111_MODEL_ID)
    {
        return -EFAULT;        
    }
    
    return 0;
}
#endif

static int mt9p111_sensor_probe_init(const struct msm_camera_sensor_info *data)
{
    int rc = 0;

    CDBG("%s: entry\n", __func__);

    if ((!data) || strcmp(data->sensor_name, "mt9p111"))
    {
        CCRT("%s: invalid parameter!\n", __func__);
        return -ENODEV;
    }

    mt9p111_ctrl = kzalloc(sizeof(struct mt9p111_ctrl_t), GFP_KERNEL);
    if (!mt9p111_ctrl)
    {
        CCRT("%s: kzalloc failed!\n", __func__);
        rc = -ENOMEM;
        goto probe_init_fail;
    }

    mt9p111_ctrl->sensordata = data;

#if !defined(CONFIG_SENSOR_ADAPTER)
    rc = msm_camera_power_backend(MSM_CAMERA_PWRUP_MODE);
    if (rc < 0)
    {
        CCRT("%s: camera_power_backend failed!\n", __func__);
        goto probe_init_fail;
    }

#if defined(CONFIG_MACH_RAISE) || defined(CONFIG_MACH_JOE)
    rc = msm_camera_clk_switch(mt9p111_ctrl->sensordata, MT9P111_GPIO_SWITCH_CTL, MT9P111_GPIO_SWITCH_VAL);
    if (rc < 0)
    {
        CCRT("%s: camera_clk_switch failed!\n", __func__);
        goto probe_init_fail;
    }
#elif defined(CONFIG_MACH_MOONCAKE)
    /* Do nothing */
#else
    /* Do nothing */
#endif /* defined(CONFIG_MACH_RAISE) || defined(CONFIG_MACH_JOE) */

    msm_camio_clk_rate_set(MT9P111_CAMIO_MCLK);
    mdelay(5);

    rc = mt9p111_sensor_init_probe(mt9p111_ctrl->sensordata);
    if (rc < 0)
    {
        CCRT("%s: sensor_init_probe failed!\n", __func__);
        goto probe_init_fail;
    }

#else
    rc = mt9p111_sensor_dev_probe(mt9p111_ctrl->sensordata);
    if (rc < 0)
    {
        CCRT("%s: mt9p111_sensor_dev_probe failed!\n", __func__);
        goto probe_init_fail;
    }

    rc = mt9p111_reg_init();
    if (rc < 0)
    {
        CCRT("%s: mt9p111_reg_init failed!\n", __func__);
        goto probe_init_fail;
    }
#endif

    return 0;

probe_init_fail:
    msm_camera_power_backend(MSM_CAMERA_PWRDWN_MODE);
    if(mt9p111_ctrl)
    {
        kfree(mt9p111_ctrl);
    }
    return rc;
}

#ifdef MT9P111_SENSOR_PROBE_INIT
int mt9p111_sensor_init(const struct msm_camera_sensor_info *data)
{
    uint32_t switch_on; 
    int rc;

    CCRT("%s: entry\n", __func__);

    if ((NULL == data)
        || strcmp(data->sensor_name, "mt9p111")
        || strcmp(mt9p111_ctrl->sensordata->sensor_name, "mt9p111"))
    {
        CCRT("%s: data is NULL, or sensor_name is not equal to mt9p111!\n", __func__);
        rc = -ENODEV;
        goto sensor_init_fail;
    }
    
    msm_camio_clk_rate_set(MT9P111_CAMIO_MCLK);
    mdelay(5);

    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x0018, 0x4028, WORD_LEN);
    if (rc < 0)
    {
        CCRT("set soft standby failed!\n");
        goto sensor_init_fail;
    }

    msm_camio_camif_pad_reg_reset();
    mdelay(10);

    switch_on = 0;
    rc = mt9p111_hard_standby(mt9p111_ctrl->sensordata, switch_on);
    if (rc < 0)
    {
        CCRT("set standby failed!\n");
        goto sensor_init_fail;
    }
    mdelay(10);

    return 0;

sensor_init_fail:
    return rc;
}
#else
static int mt9p111_sensor_init(const struct msm_camera_sensor_info *data)
{
    int rc;

    rc = mt9p111_sensor_probe_init(data);

    return rc;
}
#endif

int mt9p111_sensor_config(void __user *argp)
{
    struct sensor_cfg_data cfg_data;
    long rc = 0;

    CDBG("%s: entry\n", __func__);

    if (copy_from_user(&cfg_data, (void *)argp, sizeof(struct sensor_cfg_data)))
    {
        CCRT("%s: copy_from_user failed!\n", __func__);
        return -EFAULT;
    }


    CDBG("%s: cfgtype = %d, mode = %d\n", __func__, cfg_data.cfgtype, cfg_data.mode);

    switch (cfg_data.cfgtype)
    {
        case CFG_SET_MODE:
        {
            rc = mt9p111_set_sensor_mode(cfg_data.mode);
        }
        break;

        case CFG_SET_EFFECT:
        {
            rc = mt9p111_set_effect(cfg_data.mode, cfg_data.cfg.effect);
        }
        break;

        case CFG_PWR_UP:
        {
            rc = mt9p111_power_up();
        }
        break;

        case CFG_PWR_DOWN:
        {
            rc = mt9p111_power_down();
        }
        break;

        case CFG_SET_WB:
        {
            rc = mt9p111_set_wb(cfg_data.cfg.wb_mode);
        }
        break;

        case CFG_SET_AF:
        {
            rc = mt9p111_af_trigger();
        }
        break;

        case CFG_SET_ISO:
        {
            rc = mt9p111_set_iso(cfg_data.cfg.iso_val);
        }
        break;

        case CFG_SET_ANTIBANDING:
        {
            rc = mt9p111_set_antibanding(cfg_data.cfg.antibanding);
        }
        break;

        case CFG_SET_BRIGHTNESS:
        {
            rc = mt9p111_set_brightness(cfg_data.cfg.brightness);
        }
        break;

        case CFG_SET_SATURATION:
        {
            rc = mt9p111_set_saturation(cfg_data.cfg.saturation);
        }
        break;

        case CFG_SET_CONTRAST:
        {
            rc = mt9p111_set_contrast(cfg_data.cfg.contrast);
        }
        break;

        case CFG_SET_SHARPNESS:
        {
            rc = mt9p111_set_sharpness(cfg_data.cfg.sharpness);
        }
        break;

        case CFG_SET_EXPOSURE_COMPENSATION:
        {
            rc = mt9p111_set_exposure_compensation(cfg_data.cfg.exposure);
        }
        break;

        case CFG_SET_LENS_SHADING:
        {
            rc = 0;
        }
        break;

        
        case CFG_SET_AEC_RIO:        
        {
            rc = mt9p111_set_aec_rio(cfg_data.cfg.aec_rio);        
        }        
        break;

        case CFG_SET_ANTI_SHAKE:        
        {
            rc = mt9p111_set_anti_shake(cfg_data.cfg.antishake);        
        }        
        break;

#if 0
        /* For Auto Focus Only */
        case CFG_MOVE_FOCUS:
        {
            pr_crit("jia.jia: %s: mt9p111_move_focus\n", __func__);
            rc = mt9p111_move_focus(cfg_data.mode);
        }
        break;
#endif

        default:
        {
            rc = -EFAULT;
        }
        break;
    }

    mt9p111_prevent_suspend();

    return rc;
}

#ifdef MT9P111_SENSOR_PROBE_INIT
static int mt9p111_sensor_release_internal(void)
{
    int rc;

    CDBG("%s: entry\n", __func__);
    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x0028, 0x0000, WORD_LEN);
    if (rc < 0)
    {
        return rc;
    }
    mdelay(1);

    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x060E, 0x00FF, WORD_LEN);
    if (rc < 0)
    {
        return rc;
    }
    mdelay(50);

    rc = mt9p111_i2c_write(mt9p111_client->addr, 0x0018, 0x4029, WORD_LEN);
    if (rc < 0)
    {
        return rc;
    }
    mdelay(200);


    return 0;
}
#else
static int mt9p111_sensor_release_internal(void)
{
    int rc;

    CDBG("%s: entry\n", __func__);

    rc = msm_camera_power_backend(MSM_CAMERA_PWRDWN_MODE);

    kfree(mt9p111_ctrl);

    return rc;
}
#endif /* mt9p111_sensor_release_internal */

int mt9p111_sensor_release(void)
{
    int rc = 0;

    CDBG("%s: entry\n", __func__);

    rc = mt9p111_sensor_release_internal();

    mt9p111_allow_suspend();


    return rc;
}

static int mt9p111_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int rc = 0;

    CDBG("%s: entry\n", __func__);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
    {
        rc = -ENOTSUPP;
        goto probe_failure;
    }

    mt9p111_sensorw = kzalloc(sizeof(struct mt9p111_work_t), GFP_KERNEL);
    if (!mt9p111_sensorw)
    {
        rc = -ENOMEM;
        goto probe_failure;
    }

    i2c_set_clientdata(client, mt9p111_sensorw);

    mt9p111_client = client;

    return 0;

probe_failure:
    kfree(mt9p111_sensorw);
    mt9p111_sensorw = NULL;
    CCRT("%s: rc = %d, failed!\n", __func__, rc);
    return rc;
}

static int __exit mt9p111_i2c_remove(struct i2c_client *client)
{
    struct mt9p111_work_t *sensorw = i2c_get_clientdata(client);

    CDBG("%s: entry\n", __func__);

    free_irq(client->irq, sensorw);   
    kfree(sensorw);

    mt9p111_client = NULL;
    mt9p111_sensorw = NULL;

    return 0;
}

static const struct i2c_device_id mt9p111_id[] = {
    { "mt9p111", 0},
    { },
};

static struct i2c_driver mt9p111_driver = {
    .id_table = mt9p111_id,
    .probe  = mt9p111_i2c_probe,
    .remove = __exit_p(mt9p111_i2c_remove),
    .driver = {
        .name = MT9P111_I2C_BOARD_NAME,
    },
};

static int32_t mt9p111_i2c_add_driver(void)
{
    int32_t rc = 0;

    rc = i2c_add_driver(&mt9p111_driver);
    if (IS_ERR_VALUE(rc))
    {
        goto init_failure;
    }

    return rc;

init_failure:
    CCRT("%s: rc = %d, failed!\n", __func__, rc);
    return rc;
}

static void mt9p111_i2c_del_driver(void)
{
    i2c_del_driver(&mt9p111_driver);
}

void mt9p111_exit(void)
{
    CDBG("%s: entry\n", __func__);
    mt9p111_i2c_del_driver();
}

int mt9p111_sensor_probe(const struct msm_camera_sensor_info *info,
                                 struct msm_sensor_ctrl *s)
{
    int rc;

    CDBG("%s: entry\n", __func__);

#if !defined(CONFIG_SENSOR_ADAPTER)
    rc = mt9p111_i2c_add_driver();
    if (rc < 0)
    {
        goto probe_failed;
    }
#else
    // Do nothing
#endif

#ifdef MT9P111_SENSOR_PROBE_INIT
    rc = mt9p111_sensor_probe_init(info);
    if (rc < 0)
    {
        CCRT("%s: mt9p111_sensor_probe_init failed!\n", __func__);
        goto probe_failed;
    }
    mdelay(100);
    rc = mt9p111_sensor_release_internal();
    if (rc < 0)
    {
        CCRT("%s: mt9p111_sensor_release failed!\n", __func__);
        goto probe_failed;
    }
#endif /* define MT9P111_SENSOR_PROBE_INIT */

    mt9p111_init_suspend();

    s->s_init       = mt9p111_sensor_init;
    s->s_release    = mt9p111_sensor_release;
    s->s_config     = mt9p111_sensor_config;

    return 0;

probe_failed:
    CCRT("%s: rc = %d, failed!\n", __func__, rc);

#if !defined(CONFIG_SENSOR_ADAPTER)
    mt9p111_i2c_del_driver();
#else
    // Do nothing
#endif

    return rc;
}

#if defined(MT9P111_PROBE_WORKQUEUE)
static void mt9p111_workqueue(struct work_struct *work)
{
    int32_t rc;

    CDBG("%s: entry!!\n", __func__);

#ifdef CONFIG_ZTE_PLATFORM
#ifdef CONFIG_ZTE_FTM_FLAG_SUPPORT
    if(zte_get_ftm_flag())
    {
        return;
    }
#endif
#endif

    if (!pdev_wq)
    {
        CCRT("%s: pdev_wq is NULL!\n", __func__);
        return;
    }

#if !defined(CONFIG_SENSOR_ADAPTER)
    rc = msm_camera_drv_start(pdev_wq, mt9p111_sensor_probe);
#else
    rc = msm_camera_dev_start(pdev_wq,
                              mt9p111_sensor_i2c_probe_on,
                              mt9p111_sensor_i2c_probe_off,
                              mt9p111_sensor_dev_probe);
    if (rc < 0)
    {
        CCRT("%s: msm_camera_dev_start failed!\n", __func__);
        goto probe_failed;
    }

    rc = msm_camera_drv_start(pdev_wq, mt9p111_sensor_probe);
    if (rc < 0)
    {
        goto probe_failed;
    }

    return;

probe_failed:
    CCRT("%s: rc = %d, failed!\n", __func__, rc);
    msm_camera_power_backend(MSM_CAMERA_PWRDWN_MODE);
    return;
#endif
}

static int32_t mt9p111_probe_workqueue(void)
{
    int32_t rc;

    mt9p111_wq = create_singlethread_workqueue("mt9p111_wq");

    if (!mt9p111_wq)
    {
        CCRT("%s: mt9p111_wq is NULL!\n", __func__);
        return -EFAULT;
    }

    rc = queue_work(mt9p111_wq, &mt9p111_cb_work);

    return 0;
}

static int __mt9p111_probe(struct platform_device *pdev)
{
    int32_t rc;

    pdev_wq = pdev;

    rc = mt9p111_probe_workqueue();

    return rc;
}
#else
static int __mt9p111_probe(struct platform_device *pdev)
{
#ifdef CONFIG_ZTE_PLATFORM
#ifdef CONFIG_ZTE_FTM_FLAG_SUPPORT
    if(zte_get_ftm_flag())
    {
        return 0;
    }
#endif
#endif

    return msm_camera_drv_start(pdev, mt9p111_sensor_probe);
}
#endif /* defined(MT9P111_PROBE_WORKQUEUE) */

static struct platform_driver msm_camera_driver = {
    .probe = __mt9p111_probe,
    .driver = {
        .name = "msm_camera_mt9p111",
        .owner = THIS_MODULE,
    },
};

static int __init mt9p111_init(void)
{
    return platform_driver_register(&msm_camera_driver);
}

module_init(mt9p111_init);

