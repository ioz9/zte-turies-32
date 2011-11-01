/*
 * leds-msm-pmic-status.c - MSM PMIC LEDs driver.
 *
 * Copyright (c) 2009, ZTE, Corporation. All rights reserved.
 *
  */
#ifdef CONFIG_MACH_IBOX//WF633 
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/sched.h>

#include <mach/pmic.h>
#include <mach/gpio.h>

#define ZYF_BL_TAG "[ZYF@pmic-leds]"

#define MAX_PMIC_BL_LEVEL	16
#define BLINK_LED_NUM   4

struct BLINK_LED_data{
       int blink_flag;
	int blink_led_flag;  // 0: off, 1:0n
	int blink_on_time;  //ms
	int blink_off_time;  //ms
	struct timer_list timer;
       struct work_struct work_led_on;
       struct work_struct work_led_off;
	struct led_classdev led;
};

struct STATUS_LED_data {
	spinlock_t data_lock;
	struct BLINK_LED_data blink_led[4];  /*green, red ,data_green,data_red*/
};

struct STATUS_LED_data *STATUS_LED;

#define DATA_RED_LED_GPIO      6
#define DATA_GREEN_LED_GPIO  7
static unsigned data_leds_gpio[] = {
	GPIO_CFG(DATA_RED_LED_GPIO, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),	/*data_red */
	GPIO_CFG(DATA_GREEN_LED_GPIO, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),	/*data_green */
};

#define LED_DATA_RED     2
#define LED_DATA_GREEN 3

int pmic_set_data_led_intensity(enum ledtype type, int level)
{
      if(type==LED_DATA_RED)
      	{
      	    if(level==LED_OFF)
      	    {
              if((gpio_direction_output(DATA_RED_LED_GPIO, 0))<0)  //turn  off red
		{
			printk(KERN_ERR "gpio_direction_output: %d failed!\n", DATA_RED_LED_GPIO);
		}
      	    }
	    else
	   {
              if((gpio_direction_output(DATA_RED_LED_GPIO, 1))<0)//turn on red
		{
			printk(KERN_ERR "gpio_direction_output: %d failed!\n", DATA_RED_LED_GPIO);
		}
	   }
      	}

	 else
	 {
      	    if(level==LED_OFF)
      	    {
              if((gpio_direction_output(DATA_GREEN_LED_GPIO, 0))<0)  //turn  off green
		{
			printk(KERN_ERR "gpio_direction_output: %d failed!\n", DATA_GREEN_LED_GPIO);
		}
      	    }
	    else
	   {
              if((gpio_direction_output(DATA_GREEN_LED_GPIO, 1))<0)//turn on green
		{
			printk(KERN_ERR "gpio_direction_output: %d failed!\n", DATA_GREEN_LED_GPIO);
		}
	   }
      	}

	 return true;
}

static void pmic_data_red_led_on(struct work_struct *work)
{
       struct BLINK_LED_data *b_led = container_of(work, struct BLINK_LED_data, work_led_on);
	pmic_set_data_led_intensity(LED_DATA_RED, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
}

static void pmic_data_red_led_off(struct work_struct *work)
{
	pmic_set_data_led_intensity(LED_DATA_RED, LED_OFF);
}

static void pmic_data_green_led_on(struct work_struct *work)
{
       struct BLINK_LED_data *b_led = container_of(work, struct BLINK_LED_data, work_led_on);
	pmic_set_data_led_intensity(LED_DATA_GREEN, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
}

static void pmic_data_green_led_off(struct work_struct *work)
{
	pmic_set_data_led_intensity(LED_DATA_GREEN, LED_OFF);
}


static void pmic_red_led_on(struct work_struct *work)
{
       struct BLINK_LED_data *b_led = container_of(work, struct BLINK_LED_data, work_led_on);
	pmic_set_led_intensity(LED_KEYPAD, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
}

static void pmic_red_led_off(struct work_struct *work)
{
	pmic_set_led_intensity(LED_KEYPAD, LED_OFF);
}

static void pmic_green_led_on(struct work_struct *work)
{
       struct BLINK_LED_data *b_led = container_of(work, struct BLINK_LED_data, work_led_on);
	pmic_set_led_intensity(LED_LCD, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
}

static void pmic_green_led_off(struct work_struct *work)
{
	pmic_set_led_intensity(LED_LCD, LED_OFF);
}

static void (*func[8])(struct work_struct *work) = {
	pmic_red_led_on,
	pmic_red_led_off,
	pmic_green_led_on,
	pmic_green_led_off,
	pmic_data_red_led_on,
	pmic_data_red_led_off,
	pmic_data_green_led_on,
	pmic_data_green_led_off,
};

static void msm_pmic_bl_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	int ret;
	
	if (!strcmp(led_cdev->name, "red")) {
		ret = pmic_set_led_intensity(LED_KEYPAD, value / MAX_PMIC_BL_LEVEL);
	} 
	else if (!strcmp(led_cdev->name, "green"))
	{
		ret = pmic_set_led_intensity(LED_LCD, value / MAX_PMIC_BL_LEVEL);
	}
	else if (!strcmp(led_cdev->name, "data_red"))
	{
             ret = pmic_set_data_led_intensity(LED_DATA_RED, value/ MAX_PMIC_BL_LEVEL);
	}
	else
	{
            ret = pmic_set_data_led_intensity(LED_DATA_GREEN, value/ MAX_PMIC_BL_LEVEL);
	}
	
	if (ret)
		dev_err(led_cdev->dev, "[ZYF@PMIC LEDS]can't set pmic backlight\n");
}

static void pmic_leds_timer(unsigned long arg)
{
      struct BLINK_LED_data *b_led = (struct BLINK_LED_data *)arg;


              if(b_led->blink_flag)
		{
		       if(b_led->blink_led_flag)
		       {
		              schedule_work(&b_led->work_led_off);
		       	b_led->blink_led_flag = 0;  
		       	mod_timer(&b_led->timer,jiffies + msecs_to_jiffies(b_led->blink_off_time));
		       }
			else
			{
			       schedule_work(&b_led->work_led_on);
		       	b_led->blink_led_flag = 1;
				mod_timer(&b_led->timer,jiffies + msecs_to_jiffies(b_led->blink_on_time));
		       }
		}	
		else
		{
		       schedule_work(&b_led->work_led_on);
		}
		
}

static ssize_t led_blink_solid_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	ssize_t ret = 0;

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "green"))
		idx = 1;
	else if (!strcmp(led_cdev->name, "data_red"))
		idx = 2;
	else
		idx = 3;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	/* no lock needed for this */
	sprintf(buf, "%u\n", STATUS_LED->blink_led[idx].blink_flag);
	ret = strlen(buf) + 1;

	return ret;
}

static ssize_t led_blink_solid_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	char *after;
	unsigned long state;
	ssize_t ret = -EINVAL;
	size_t count;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "green"))
		idx = 1;
	else if (!strcmp(led_cdev->name, "data_red"))
		idx = 2;
	else
		idx = 3;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	state = simple_strtoul(buf, &after, 10);

	count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		spin_lock(&STATUS_LED->data_lock);
		if(0==state)
		{
		       STATUS_LED->blink_led[idx].blink_flag= 0;
			pr_info(ZYF_BL_TAG "DISABLE %s led blink \n",idx?"green":"red");
		}
		else
		{
		       STATUS_LED->blink_led[idx].blink_flag= 1;
		       STATUS_LED->blink_led[idx].blink_led_flag = 0;
			schedule_work(&STATUS_LED->blink_led[idx].work_led_off);
			mod_timer(&STATUS_LED->blink_led[idx].timer,jiffies + msecs_to_jiffies(STATUS_LED->blink_led[idx].blink_off_time));
			pr_info(ZYF_BL_TAG "ENABLE %s led blink \n",idx?"green":"red");
		}
		spin_unlock(&STATUS_LED->data_lock);
	}

	return ret;
}

static DEVICE_ATTR(blink, 0644, led_blink_solid_show, led_blink_solid_store);

static ssize_t cpldled_grpfreq_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "green"))
		idx = 1;
	else if (!strcmp(led_cdev->name, "data_red"))
		idx = 2;
	else
		idx = 3;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);
	return sprintf(buf, "blink_on_time = %u ms \n", STATUS_LED->blink_led[idx].blink_on_time);
}

static ssize_t cpldled_grpfreq_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	char *after;
	unsigned long state;
	ssize_t ret = -EINVAL;
	size_t count;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "green"))
		idx = 1;
	else if (!strcmp(led_cdev->name, "data_red"))
		idx = 2;
	else
		idx = 3;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	state = simple_strtoul(buf, &after, 10);

	count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		spin_lock(&STATUS_LED->data_lock);
		STATUS_LED->blink_led[idx].blink_on_time = state;  //in ms
		pr_info(ZYF_BL_TAG "Set %s led blink_on_time=%d ms \n",idx?"green":"red",STATUS_LED->blink_led[idx].blink_on_time);
		spin_unlock(&STATUS_LED->data_lock);
	}

	return ret;
}

static DEVICE_ATTR(grpfreq, 0644, cpldled_grpfreq_show, cpldled_grpfreq_store);

static ssize_t cpldled_grppwm_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "green"))
		idx = 1;
	else if (!strcmp(led_cdev->name, "data_red"))
		idx = 2;
	else
		idx = 3;
	
	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);
	return sprintf(buf, "blink_off_time = %u ms\n", STATUS_LED->blink_led[idx].blink_off_time);
}

static ssize_t cpldled_grppwm_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	char *after;
	unsigned long state;
	ssize_t ret = -EINVAL;
	size_t count;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "green"))
		idx = 1;
	else if (!strcmp(led_cdev->name, "data_red"))
		idx = 2;
	else
		idx = 3;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	state = simple_strtoul(buf, &after, 10);

	count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		spin_lock(&STATUS_LED->data_lock);
		STATUS_LED->blink_led[idx].blink_off_time= state;  //in ms
		pr_info(ZYF_BL_TAG "Set %s led blink_off_time=%d ms \n",idx?"green":"red",STATUS_LED->blink_led[idx].blink_off_time);
		spin_unlock(&STATUS_LED->data_lock);
	}

	return ret;
}

static DEVICE_ATTR(grppwm, 0644, cpldled_grppwm_show, cpldled_grppwm_store);

static int msm_pmic_led_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i, j;

	/*****************************************/
	for (i = 0;i < 2; i++) 
	{
		ret= gpio_tlmm_config(data_leds_gpio[i],GPIO_ENABLE);
		if (ret) 
		{
				printk(KERN_ERR "%s: gpio_tlmm_config(%#x)=%d\n", __func__, data_leds_gpio[i], ret);
				return -EIO;
	        }
	}	

	 if((gpio_direction_output(DATA_RED_LED_GPIO, 0))<0)  //turn  off red
	{
			printk(KERN_ERR "gpio_direction_output: %d failed!\n", DATA_GREEN_LED_GPIO);
                     return -EIO;
	 }	

	 if((gpio_direction_output(DATA_GREEN_LED_GPIO, 1))<0)  //turn  on  green
	{
		       printk(KERN_ERR "gpio_direction_output: %d failed!\n", DATA_GREEN_LED_GPIO);
                     return -EIO;
	 }
	/****************************************/

	STATUS_LED = kzalloc(sizeof(struct STATUS_LED_data), GFP_KERNEL);
	if (STATUS_LED == NULL) {
		printk(KERN_ERR "STATUS_LED_probe: no memory for device\n");
		ret = -ENOMEM;
		goto err_alloc_failed;
	}
	
	STATUS_LED->blink_led[0].led.name = "red";
	STATUS_LED->blink_led[0].led.brightness_set = msm_pmic_bl_led_set;
	STATUS_LED->blink_led[0].led.brightness = LED_OFF;
	STATUS_LED->blink_led[0].blink_flag = 0;
	STATUS_LED->blink_led[0].blink_on_time = 500;
	STATUS_LED->blink_led[0].blink_off_time = 500;

	STATUS_LED->blink_led[1].led.name = "green";
	STATUS_LED->blink_led[1].led.brightness_set = msm_pmic_bl_led_set;
	STATUS_LED->blink_led[1].led.brightness = LED_FULL; //ZTE_PM_LEDS_20101101
	STATUS_LED->blink_led[1].blink_flag = 0;
	STATUS_LED->blink_led[1].blink_on_time = 500;
	STATUS_LED->blink_led[1].blink_off_time = 500;

	STATUS_LED->blink_led[2].led.name = "data_red";
	STATUS_LED->blink_led[2].led.brightness_set = msm_pmic_bl_led_set;
	STATUS_LED->blink_led[2].led.brightness = LED_OFF;
	STATUS_LED->blink_led[2].blink_flag = 0;
	STATUS_LED->blink_led[2].blink_on_time = 500;
	STATUS_LED->blink_led[2].blink_off_time = 500;

	STATUS_LED->blink_led[3].led.name = "data_green";
	STATUS_LED->blink_led[3].led.brightness_set = msm_pmic_bl_led_set;
	STATUS_LED->blink_led[3].led.brightness = LED_OFF;
	STATUS_LED->blink_led[3].blink_flag = 0;
	STATUS_LED->blink_led[3].blink_on_time = 500;
	STATUS_LED->blink_led[3].blink_off_time = 500;

	spin_lock_init(&STATUS_LED->data_lock);

	for (i = 0; i < 4; i++) {	/* red, green,data_red,data_green */
		ret = led_classdev_register(&pdev->dev, &STATUS_LED->blink_led[i].led);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: led_classdev_register failed\n");
			goto err_led_classdev_register_failed;
		}
	}

	for (i = 0; i < 4; i++) {
		ret =
		    device_create_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: create dev_attr_blink failed\n");
			goto err_out_attr_blink;
		}
	}

	for (i = 0; i < 4; i++) {
		ret =
		    device_create_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_grppwm);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: create dev_attr_grppwm failed\n");
			goto err_out_attr_grppwm;
		}
	}

	for (i = 0; i < 4; i++) {
		ret =
		    device_create_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_grpfreq);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: create dev_attr_grpfreq failed\n");
			goto err_out_attr_grpfreq;
		}
	}
	dev_set_drvdata(&pdev->dev, STATUS_LED);
	
	for (i = 0; i < 4; i++)
	{
	       INIT_WORK(&STATUS_LED->blink_led[i].work_led_on, func[i*2]);
	       INIT_WORK(&STATUS_LED->blink_led[i].work_led_off, func[i*2+1]);
	       setup_timer(&STATUS_LED->blink_led[i].timer, pmic_leds_timer, (unsigned long)&STATUS_LED->blink_led[i]);

              if(i==1) //ZTE_PM_LEDS_20101101
		msm_pmic_bl_led_set(&STATUS_LED->blink_led[i].led, LED_FULL);//green on
		else
		msm_pmic_bl_led_set(&STATUS_LED->blink_led[i].led, LED_OFF); //other off
	}
	
       return 0;
	   
err_out_attr_grpfreq:
	for (j = 0; j < i; j++)
		device_remove_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
	i = 4;
	
err_out_attr_grppwm:
	for (j = 0; j < i; j++)
		device_remove_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
	i = 4;
	
err_out_attr_blink:
	for (j = 0; j < i; j++)
		device_remove_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
	i = 4;

err_led_classdev_register_failed:
	for (j = 0; j < i; j++)
		led_classdev_unregister(&STATUS_LED->blink_led[i].led);

err_alloc_failed:
	kfree(STATUS_LED);

	return ret;
	
}

static int __devexit msm_pmic_led_remove(struct platform_device *pdev)
{
       int i;
	   
       for (i = 0; i < 4; i++)
		led_classdev_unregister(&STATUS_LED->blink_led[i].led);

	return 0;
}

#ifdef CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND
#include "../../arch/arm/mach-msm/proc_comm.h"
#define NLED_APP2SLEEP_FLAG_LCD 0x0001//green
#define NLED_APP2SLEEP_FLAG_KBD 0x0010//red
//#define NLED_APP2SLEEP_FLAG_VIB 0x0100
#define ZTE_PROC_COMM_CMD3_NLED_BLINK_DISABLE 2
#define ZTE_PROC_COMM_CMD3_NLED_BLINK_ENABLE 3

//inform modem the states of NLED through PCOM_CUSTOMER_CMD2
int msm_pmic_led_config_while_app2sleep(unsigned blink_kbd,unsigned blink_lcd, unsigned set)
{
	int config_last = 0;
	if(blink_kbd > 0)
	{
		config_last |= NLED_APP2SLEEP_FLAG_KBD;
	}
	if(blink_lcd > 0)
	{
		config_last |= NLED_APP2SLEEP_FLAG_LCD;
	}
	pr_info("LHX LED PROC:Green %d,RED%d;config 0x%x,set(2dis 3enable):%d\n",blink_lcd,blink_kbd,config_last,set);
 	return msm_proc_comm(PCOM_CUSTOMER_CMD3, &config_last, &set);
}
#endif

#ifdef CONFIG_PM
static int msm_pmic_led_suspend(struct platform_device *dev,
		pm_message_t state)
{
       int i;
	   #ifdef CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND
	   //blink_led[0] red,blink_led[1] green
	   msm_pmic_led_config_while_app2sleep( STATUS_LED->blink_led[0].led.brightness,//red
	   										STATUS_LED->blink_led[1].led.brightness, ZTE_PROC_COMM_CMD3_NLED_BLINK_ENABLE);//green
	   #endif
       for (i = 0; i < 4; i++)
		led_classdev_suspend(&STATUS_LED->blink_led[i].led);

	return 0;
}

static int msm_pmic_led_resume(struct platform_device *dev)
{
       int i;
	#ifdef CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND
	   msm_pmic_led_config_while_app2sleep( 0,0, ZTE_PROC_COMM_CMD3_NLED_BLINK_DISABLE);
	#endif
       for (i = 0; i < 4; i++)
		led_classdev_resume(&STATUS_LED->blink_led[i].led);
	

	return 0;
}
#else
#define msm_pmic_led_suspend NULL
#define msm_pmic_led_resume NULL
#endif

static struct platform_driver msm_pmic_led_driver = {
	.probe		= msm_pmic_led_probe,
	.remove		= __devexit_p(msm_pmic_led_remove),
	.suspend	= msm_pmic_led_suspend,
	.resume		= msm_pmic_led_resume,
	.driver		= {
		.name	= "pmic-leds-status",
		.owner	= THIS_MODULE,
	},
};

static int __init msm_pmic_led_init(void)
{
	return platform_driver_register(&msm_pmic_led_driver);
}
module_init(msm_pmic_led_init);

static void __exit msm_pmic_led_exit(void)
{
	platform_driver_unregister(&msm_pmic_led_driver);
}
module_exit(msm_pmic_led_exit);

MODULE_DESCRIPTION("MSM PMIC LEDs driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pmic-leds");

#else

#ifdef CONFIG_MACH_SISA //added by zte_zhang.yu_1 for V32 @110414
/************************
V32 add new below leds
ARM11 support

MPP_22   power on leds(4pcs) 

MPP_21   rssi_led1
MPP_20   rssi_led2
MPP_19   rssi_led3

MPP_18   wifi_led 
************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/sched.h>

#include <mach/pmic.h>
//#include <mach/gpio.h>

#define ZYF_BL_TAG "[ZYF@pmic-leds]"

#define MAX_PMIC_BL_LEVEL	16
#define BLINK_LED_NUM   5

struct BLINK_LED_data{
       int blink_flag;
	int blink_led_flag;  // 0: off, 1:0n
	int blink_on_time;  //ms
	int blink_off_time;  //ms
	struct timer_list timer;
         struct work_struct work_led_on;
         struct work_struct work_led_off;
	struct led_classdev led;
};

struct STATUS_LED_data {
	spinlock_t data_lock;
	struct BLINK_LED_data blink_led[BLINK_LED_NUM];  
};

struct STATUS_LED_data *STATUS_LED;

enum led_indicator_t{
	POWER_LED,
	RSSI_LED1,
	RSSI_LED2,
	RSSI_LED3,
	WIFI_LED,
};

static int pmic_set_led_indicator_intensity(enum led_indicator_t type, int level)
{
      int ret_val = 0;

      printk ("ZHY %s: set led intensity\n", __func__);

      if(type ==POWER_LED)
      {
          if(level == LED_OFF)
      	 {
   	     printk ("ZHY %s: set power  intensity as off\n", __func__);    
	     ret_val = pmic_secure_mpp_control_digital_output(PM_MPP_22, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_HIGH);
          }		 
          else
          {
              printk ("ZHY %s: set power intensity\n", __func__);
	     ret_val = pmic_secure_mpp_control_digital_output(PM_MPP_22, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_LOW);          
          }
      }	  
      else if(type ==RSSI_LED1)
      {
          if(level == LED_OFF)
      	 {
   	     printk ("ZHY %s: set rssi_led1 intensity as off\n", __func__);    
	     ret_val = pmic_secure_mpp_control_digital_output(PM_MPP_21, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_HIGH);
          }		 
          else
          {
              printk ("ZHY %s: set rssi_led1 intensity\n", __func__);
	     ret_val = pmic_secure_mpp_control_digital_output(PM_MPP_21, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_LOW);          
          }
      }
      else if(type ==RSSI_LED2)
      {
          if(level == LED_OFF)
      	 {
      	     printk ("ZHY %s: set rssi_led2 intensity as off\n", __func__);  
	     ret_val = pmic_secure_mpp_control_digital_output(PM_MPP_20, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_HIGH);
          }		 
          else
          {
                printk ("ZHY %s: set rssi_led2 intensity\n", __func__);
	     ret_val = pmic_secure_mpp_control_digital_output(PM_MPP_20, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_LOW);          
          }
      }
      else if(type ==RSSI_LED3)
      {
          if(level == LED_OFF)
      	 {
      	 	     printk ("ZHY %s: set rssi_led3 intensity as off\n", __func__);  
	     ret_val = pmic_secure_mpp_control_digital_output(PM_MPP_19, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_HIGH);
          }		 
          else
          {
                printk ("ZHY %s: set rssi_led3 intensity\n", __func__);
	     ret_val = pmic_secure_mpp_control_digital_output(PM_MPP_19, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_LOW);          
          }
      }
      else //WIFI_LED
      {
          if(level == LED_OFF)
      	 {
      	     printk ("ZHY %s: set wifi led intensity as off\n", __func__);    
	     ret_val = pmic_secure_mpp_control_digital_output(PM_MPP_18, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_HIGH);
          }		 
          else
          {
              printk ("ZHY %s: set wifi led intensity\n", __func__);          
	     ret_val = pmic_secure_mpp_control_digital_output(PM_MPP_18, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_LOW);          
          }
      }

      printk( "ZHY %s: led level =%d :type = %d : ret val = %d \n", __func__, level , type, ret_val);
	 
      return ret_val;
}

static void pmic_power_led_on(struct work_struct *work)
{
      struct BLINK_LED_data *b_led = container_of(work, struct BLINK_LED_data, work_led_on);
	pmic_set_led_indicator_intensity(POWER_LED, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
};

static void pmic_power_led_off(struct work_struct *work)
{
	pmic_set_led_indicator_intensity(POWER_LED, LED_OFF);
};
	
static void pmic_rssi_led1_on(struct work_struct *work)
{
      struct BLINK_LED_data *b_led = container_of(work, struct BLINK_LED_data, work_led_on);
	pmic_set_led_indicator_intensity(RSSI_LED1, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
};

static void pmic_rssi_led1_off(struct work_struct *work)
{
	pmic_set_led_indicator_intensity(RSSI_LED1, LED_OFF);
};

static void pmic_rssi_led2_on(struct work_struct *work)
{
      struct BLINK_LED_data *b_led = container_of(work, struct BLINK_LED_data, work_led_on);
         pmic_set_led_indicator_intensity(RSSI_LED2, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
};

static void pmic_rssi_led2_off(struct work_struct *work)
{     
	pmic_set_led_indicator_intensity(RSSI_LED2, LED_OFF);
};

static void pmic_rssi_led3_on(struct work_struct *work)
{
      struct BLINK_LED_data *b_led = container_of(work, struct BLINK_LED_data, work_led_on);
         pmic_set_led_indicator_intensity(RSSI_LED3, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
};

static void pmic_rssi_led3_off(struct work_struct *work)
{     
	pmic_set_led_indicator_intensity(RSSI_LED3, LED_OFF);
};

static void pmic_wifi_led_on(struct work_struct *work)
{
      struct BLINK_LED_data *b_led = container_of(work, struct BLINK_LED_data, work_led_on);
         pmic_set_led_indicator_intensity(WIFI_LED, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
};

static void pmic_wifi_led_off(struct work_struct *work)
{     
	pmic_set_led_indicator_intensity(WIFI_LED, LED_OFF);
};

static void (*func[BLINK_LED_NUM*2])(struct work_struct *work) = {
	pmic_power_led_on,
	pmic_power_led_off,
	pmic_rssi_led1_on,
	pmic_rssi_led1_off,
	pmic_rssi_led2_on,
	pmic_rssi_led2_off,
	pmic_rssi_led3_on,
	pmic_rssi_led3_off,
	pmic_wifi_led_on,
	pmic_wifi_led_off,
};

static void msm_pmic_bl_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	int ret;
	
	
	if (!strcmp(led_cdev->name, "power_led")) {
		ret = pmic_set_led_indicator_intensity(POWER_LED, value / MAX_PMIC_BL_LEVEL);
	} 
	else if (!strcmp(led_cdev->name, "rssi_led1")) {
		ret = pmic_set_led_indicator_intensity(RSSI_LED1, value / MAX_PMIC_BL_LEVEL);
	}	
	else if (!strcmp(led_cdev->name, "rssi_led2"))
	{
		ret = pmic_set_led_indicator_intensity(RSSI_LED2, value / MAX_PMIC_BL_LEVEL);
	}
	else if (!strcmp(led_cdev->name, "rssi_led3"))
	{
	         ret = pmic_set_led_indicator_intensity(RSSI_LED3, value / MAX_PMIC_BL_LEVEL);
	}
	else//wifi_led
	{
                  ret = pmic_set_led_indicator_intensity(WIFI_LED, value / MAX_PMIC_BL_LEVEL);
	}

	if (ret)
		dev_err(led_cdev->dev, "[ZYF@PMIC LEDS]can't set pmic backlight\n");
}

static void pmic_leds_timer(unsigned long arg)
{
      struct BLINK_LED_data *b_led = (struct BLINK_LED_data *)arg;


              if(b_led->blink_flag)
		{
		       if(b_led->blink_led_flag)
		       {
		              schedule_work(&b_led->work_led_off);
		       	b_led->blink_led_flag = 0;  
		       	mod_timer(&b_led->timer,jiffies + msecs_to_jiffies(b_led->blink_off_time));
		       }
			else
			{
			       schedule_work(&b_led->work_led_on);
		       	b_led->blink_led_flag = 1;
				mod_timer(&b_led->timer,jiffies + msecs_to_jiffies(b_led->blink_on_time));
		       }
		}	
		else
		{
		       schedule_work(&b_led->work_led_on);
		}
		
}

static ssize_t led_blink_solid_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	ssize_t ret = 0;

	if (!strcmp(led_cdev->name, "power_led"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "rssi_led1"))
		idx = 1;	
	else if (!strcmp(led_cdev->name, "rssi_led2"))
		idx = 2;
	else if (!strcmp(led_cdev->name, "rssi_led3"))
		idx = 3;
	else//wifi_led
		idx = 4;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	/* no lock needed for this */
	sprintf(buf, "%u\n", STATUS_LED->blink_led[idx].blink_flag);
	ret = strlen(buf) + 1;

	return ret;
}

static ssize_t led_blink_solid_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	char *after;
	unsigned long state;
	ssize_t ret = -EINVAL;
	size_t count;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);


	if (!strcmp(led_cdev->name, "power_led"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "rssi_led1"))
		idx = 1;
	else if (!strcmp(led_cdev->name, "rssi_led2"))
		idx = 2;
	else if (!strcmp(led_cdev->name, "rssi_led3"))
		idx = 3;
	else//wifi_led
		idx = 4;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	state = simple_strtoul(buf, &after, 10);

	count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		spin_lock(&STATUS_LED->data_lock);
		if(0==state)
		{
		       STATUS_LED->blink_led[idx].blink_flag= 0;
			pr_info(ZYF_BL_TAG "DISABLE %d led blink \n",idx);
		}
		else
		{
		       STATUS_LED->blink_led[idx].blink_flag= 1;
		       STATUS_LED->blink_led[idx].blink_led_flag = 0;
			schedule_work(&STATUS_LED->blink_led[idx].work_led_off);
			mod_timer(&STATUS_LED->blink_led[idx].timer,jiffies + msecs_to_jiffies(STATUS_LED->blink_led[idx].blink_off_time));
			pr_info(ZYF_BL_TAG "ENABLE %d led blink \n",idx);
		}
		spin_unlock(&STATUS_LED->data_lock);
	}

	return ret;
}

static DEVICE_ATTR(blink, 0644, led_blink_solid_show, led_blink_solid_store);

static ssize_t cpldled_grpfreq_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	
	if (!strcmp(led_cdev->name, "power_led"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "rssi_led1"))
		idx = 1;
	else if (!strcmp(led_cdev->name, "rssi_led2"))
		idx = 2;
	else if (!strcmp(led_cdev->name, "rssi_led3"))
		idx = 3;
	else//wifi_led
		idx = 4;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);
	return sprintf(buf, "blink_on_time = %u ms \n", STATUS_LED->blink_led[idx].blink_on_time);
}

static ssize_t cpldled_grpfreq_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	char *after;
	unsigned long state;
	ssize_t ret = -EINVAL;
	size_t count;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "power_led"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "rssi_led1"))
		idx = 1;
	else if (!strcmp(led_cdev->name, "rssi_led2"))
		idx = 2;
	else if (!strcmp(led_cdev->name, "rssi_led3"))
		idx = 3;
	else//wifi_led
		idx = 4;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	state = simple_strtoul(buf, &after, 10);

	count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		spin_lock(&STATUS_LED->data_lock);
		STATUS_LED->blink_led[idx].blink_on_time = state;  //in ms
		pr_info(ZYF_BL_TAG "Set %d led blink_on_time=%d ms \n",idx,STATUS_LED->blink_led[idx].blink_on_time);
		spin_unlock(&STATUS_LED->data_lock);
	}

	return ret;
}

static DEVICE_ATTR(grpfreq, 0644, cpldled_grpfreq_show, cpldled_grpfreq_store);

static ssize_t cpldled_grppwm_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "power_led"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "rssi_led1"))
		idx = 1;
	else if (!strcmp(led_cdev->name, "rssi_led2"))
		idx = 2;
	else if (!strcmp(led_cdev->name, "rssi_led3"))
		idx = 3;
	else//wifi_led
		idx = 4;
	
	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);
	return sprintf(buf, "blink_off_time = %u ms\n", STATUS_LED->blink_led[idx].blink_off_time);
}

static ssize_t cpldled_grppwm_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	char *after;
	unsigned long state;
	ssize_t ret = -EINVAL;
	size_t count;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "power_led"))
		idx = 0;
	else if (!strcmp(led_cdev->name, "rssi_led1"))
		idx = 1;
	else if (!strcmp(led_cdev->name, "rssi_led2"))
		idx = 2;
	else if (!strcmp(led_cdev->name, "rssi_led3"))
		idx = 3;
	else//wifi_led
		idx = 4;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	state = simple_strtoul(buf, &after, 10);

	count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		spin_lock(&STATUS_LED->data_lock);
		STATUS_LED->blink_led[idx].blink_off_time= state;  //in ms
		pr_info(ZYF_BL_TAG "Set %d led blink_off_time=%d ms \n",idx,STATUS_LED->blink_led[idx].blink_off_time);
		spin_unlock(&STATUS_LED->data_lock);
	}

	return ret;
}

static DEVICE_ATTR(grppwm, 0644, cpldled_grppwm_show, cpldled_grppwm_store);

static int msm_pmic_led_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i, j;
	
         /* initialize indicator leds */
	//power off power led
         //pmic_secure_mpp_control_digital_output(PM_MPP_22, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_HIGH);		 
	//power off rssi_led1
         //pmic_secure_mpp_control_digital_output(PM_MPP_21, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_HIGH);	
	//power off rssi_led2	
	//pmic_secure_mpp_control_digital_output(PM_MPP_20, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_HIGH);
	//power off rssi_led3	
         //pmic_secure_mpp_control_digital_output(PM_MPP_19, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_HIGH); 
	//power off wifi_led	
         //pmic_secure_mpp_control_digital_output(PM_MPP_18, PM_MPP__DLOGIC__LVL_VDD, PM_MPP__DLOGIC_OUT__CTRL_HIGH);		 
	
	STATUS_LED = kzalloc(sizeof(struct STATUS_LED_data), GFP_KERNEL);
	if (STATUS_LED == NULL) {
		printk(KERN_ERR "STATUS_LED_probe: no memory for device\n");
		ret = -ENOMEM;
		goto err_alloc_failed;
	}
	
	STATUS_LED->blink_led[0].led.name = "power_led";
	STATUS_LED->blink_led[0].led.brightness_set = msm_pmic_bl_led_set;
	STATUS_LED->blink_led[0].led.brightness = LED_OFF;
	STATUS_LED->blink_led[0].blink_flag = 0;
	STATUS_LED->blink_led[0].blink_on_time = 500;
	STATUS_LED->blink_led[0].blink_off_time = 500;

	STATUS_LED->blink_led[1].led.name = "rssi_led1";
	STATUS_LED->blink_led[1].led.brightness_set = msm_pmic_bl_led_set;
	STATUS_LED->blink_led[1].led.brightness = LED_OFF;
	STATUS_LED->blink_led[1].blink_flag = 0;
	STATUS_LED->blink_led[1].blink_on_time = 500;
	STATUS_LED->blink_led[1].blink_off_time = 500;	

	STATUS_LED->blink_led[2].led.name = "rssi_led2";
	STATUS_LED->blink_led[2].led.brightness_set = msm_pmic_bl_led_set;
	STATUS_LED->blink_led[2].led.brightness = LED_OFF;
	STATUS_LED->blink_led[2].blink_flag = 0;
	STATUS_LED->blink_led[2].blink_on_time = 500;
	STATUS_LED->blink_led[2].blink_off_time = 500;

	STATUS_LED->blink_led[3].led.name = "rssi_led3";
	STATUS_LED->blink_led[3].led.brightness_set = msm_pmic_bl_led_set;
	STATUS_LED->blink_led[3].led.brightness = LED_OFF;
	STATUS_LED->blink_led[3].blink_flag = 0;
	STATUS_LED->blink_led[3].blink_on_time = 500;
	STATUS_LED->blink_led[3].blink_off_time = 500;

	STATUS_LED->blink_led[4].led.name = "wifi_led";
	STATUS_LED->blink_led[4].led.brightness_set = msm_pmic_bl_led_set;
	STATUS_LED->blink_led[4].led.brightness = LED_OFF;
	STATUS_LED->blink_led[4].blink_flag = 0;
	STATUS_LED->blink_led[4].blink_on_time = 500;
	STATUS_LED->blink_led[4].blink_off_time = 500;

	spin_lock_init(&STATUS_LED->data_lock);

	for (i = 0; i < BLINK_LED_NUM; i++) {	/* poer_led,rssi_led1, rssi_led2,rssi_led3,wifi_led */
		ret = led_classdev_register(&pdev->dev, &STATUS_LED->blink_led[i].led);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: led_classdev_register failed\n");
			goto err_led_classdev_register_failed;
		}
	}

	for (i = 0; i < BLINK_LED_NUM; i++) {
		ret =
		    device_create_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: create dev_attr_blink failed\n");
			goto err_out_attr_blink;
		}
	}

	for (i = 0; i < BLINK_LED_NUM; i++) {
		ret =
		    device_create_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_grppwm);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: create dev_attr_grppwm failed\n");
			goto err_out_attr_grppwm;
		}
	}

	for (i = 0; i < BLINK_LED_NUM; i++) {
		ret =
		    device_create_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_grpfreq);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: create dev_attr_grpfreq failed\n");
			goto err_out_attr_grpfreq;
		}
	}
	dev_set_drvdata(&pdev->dev, STATUS_LED);
	
	for (i = 0; i < BLINK_LED_NUM; i++)
	{
	       INIT_WORK(&STATUS_LED->blink_led[i].work_led_on, func[i*2]);
	       INIT_WORK(&STATUS_LED->blink_led[i].work_led_off, func[i*2+1]);
	       setup_timer(&STATUS_LED->blink_led[i].timer, pmic_leds_timer, (unsigned long)&STATUS_LED->blink_led[i]);
	}
	
       return 0;
	   
err_out_attr_grpfreq:
	for (j = 0; j < i; j++)
		device_remove_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
	i = BLINK_LED_NUM;
	
err_out_attr_grppwm:
	for (j = 0; j < i; j++)
		device_remove_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
	i = BLINK_LED_NUM;
	
err_out_attr_blink:
	for (j = 0; j < i; j++)
		device_remove_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
	i = BLINK_LED_NUM;

err_led_classdev_register_failed:
	for (j = 0; j < i; j++)
		led_classdev_unregister(&STATUS_LED->blink_led[i].led);

err_alloc_failed:
	kfree(STATUS_LED);

	return ret;
	
}

static int __devexit msm_pmic_led_remove(struct platform_device *pdev)
{
       int i;
	   
       for (i = 0; i < BLINK_LED_NUM; i++)
		led_classdev_unregister(&STATUS_LED->blink_led[i].led);

	return 0;
}

#ifdef CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND
#include "../../arch/arm/mach-msm/proc_comm.h"
#define NLED_APP2SLEEP_FLAG_LCD 0x0001//green
#define NLED_APP2SLEEP_FLAG_KBD 0x0010//red
//#define NLED_APP2SLEEP_FLAG_VIB 0x0100
#define ZTE_PROC_COMM_CMD3_NLED_BLINK_DISABLE 2
#define ZTE_PROC_COMM_CMD3_NLED_BLINK_ENABLE 3

//inform modem the states of NLED through PCOM_CUSTOMER_CMD2
int msm_pmic_led_config_while_app2sleep(unsigned blink_kbd,unsigned blink_lcd, unsigned set)
{
	int config_last = 0;
	if(blink_kbd > 0)
	{
		config_last |= NLED_APP2SLEEP_FLAG_KBD;
	}
	if(blink_lcd > 0)
	{
		config_last |= NLED_APP2SLEEP_FLAG_LCD;
	}
	pr_info("LHX LED PROC:Green %d,RED%d;config 0x%x,set(2dis 3enable):%d\n",blink_lcd,blink_kbd,config_last,set);
 	return msm_proc_comm(PCOM_CUSTOMER_CMD3, &config_last, &set);
}
#endif

#ifdef CONFIG_PM
static int msm_pmic_led_suspend(struct platform_device *dev,
		pm_message_t state)
{
       int i;
	   #ifdef CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND
	   //blink_led[0] red,blink_led[1] green
	   msm_pmic_led_config_while_app2sleep( STATUS_LED->blink_led[0].led.brightness,//red
	   										STATUS_LED->blink_led[1].led.brightness, ZTE_PROC_COMM_CMD3_NLED_BLINK_ENABLE);//green
	   #endif
       for (i = 0; i < BLINK_LED_NUM; i++)
		led_classdev_suspend(&STATUS_LED->blink_led[i].led);

	return 0;
}

static int msm_pmic_led_resume(struct platform_device *dev)
{
       int i;
	#ifdef CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND
	   msm_pmic_led_config_while_app2sleep( 0,0, ZTE_PROC_COMM_CMD3_NLED_BLINK_DISABLE);
	#endif
       for (i = 0; i < BLINK_LED_NUM; i++)
		led_classdev_resume(&STATUS_LED->blink_led[i].led);
	

	return 0;
}
#else
#define msm_pmic_led_suspend NULL
#define msm_pmic_led_resume NULL
#endif

static struct platform_driver msm_pmic_led_driver = {
	.probe		= msm_pmic_led_probe,
	.remove		= __devexit_p(msm_pmic_led_remove),
	.suspend	= msm_pmic_led_suspend,
	.resume		= msm_pmic_led_resume,
	.driver		= {
		.name	= "pmic-leds-status",
		.owner	= THIS_MODULE,
	},
};

static int __init msm_pmic_led_init(void)
{
	return platform_driver_register(&msm_pmic_led_driver);
}
module_init(msm_pmic_led_init);

static void __exit msm_pmic_led_exit(void)
{
	platform_driver_unregister(&msm_pmic_led_driver);
}
module_exit(msm_pmic_led_exit);

MODULE_DESCRIPTION("MSM PMIC LEDs driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pmic-leds");

#else//current froyo projects, merged by zte_zhang.yu_1 from eclair @110414

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/sched.h>

#include <mach/pmic.h>

#define ZYF_BL_TAG "[ZYF@pmic-leds]"

#define MAX_PMIC_BL_LEVEL	16
#define BLINK_LED_NUM   2

struct BLINK_LED_data{
       int blink_flag;
	int blink_led_flag;  // 0: off, 1:0n
	int blink_on_time;  //ms
	int blink_off_time;  //ms
	struct timer_list timer;
       struct work_struct work_led_on;
       struct work_struct work_led_off;
	struct led_classdev led;
};

struct STATUS_LED_data {
	spinlock_t data_lock;
	struct BLINK_LED_data blink_led[2];  /*green, red */
};

struct STATUS_LED_data *STATUS_LED;

static void pmic_red_led_on(struct work_struct *work)
{
       struct BLINK_LED_data *b_led = container_of(work, struct BLINK_LED_data, work_led_on);
	pmic_set_led_intensity(LED_KEYPAD, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
}

static void pmic_red_led_off(struct work_struct *work)
{
	pmic_set_led_intensity(LED_KEYPAD, LED_OFF);
}

static void pmic_green_led_on(struct work_struct *work)
{
       struct BLINK_LED_data *b_led = container_of(work, struct BLINK_LED_data, work_led_on);
	pmic_set_led_intensity(LED_LCD, b_led->led.brightness / MAX_PMIC_BL_LEVEL);
}

static void pmic_green_led_off(struct work_struct *work)
{
	pmic_set_led_intensity(LED_LCD, LED_OFF);
}

static void (*func[4])(struct work_struct *work) = {
	pmic_red_led_on,
	pmic_red_led_off,
	pmic_green_led_on,
	pmic_green_led_off,
};

static void msm_pmic_bl_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	int ret;
	
	if (!strcmp(led_cdev->name, "red")) {
		ret = pmic_set_led_intensity(LED_KEYPAD, value / MAX_PMIC_BL_LEVEL);
	} else {
		ret = pmic_set_led_intensity(LED_LCD, value / MAX_PMIC_BL_LEVEL);
	}
	
	if (ret)
		dev_err(led_cdev->dev, "[ZYF@PMIC LEDS]can't set pmic backlight\n");
}

static void pmic_leds_timer(unsigned long arg)
{
      struct BLINK_LED_data *b_led = (struct BLINK_LED_data *)arg;


              if(b_led->blink_flag)
		{
		       if(b_led->blink_led_flag)
		       {
		              schedule_work(&b_led->work_led_off);
		       	b_led->blink_led_flag = 0;  
		       	mod_timer(&b_led->timer,jiffies + msecs_to_jiffies(b_led->blink_off_time));
		       }
			else
			{
			       schedule_work(&b_led->work_led_on);
		       	b_led->blink_led_flag = 1;
				mod_timer(&b_led->timer,jiffies + msecs_to_jiffies(b_led->blink_on_time));
		       }
		}	
		else
		{
		       schedule_work(&b_led->work_led_on);
		}
		
}

static ssize_t led_blink_solid_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	ssize_t ret = 0;

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else
		idx = 1;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	/* no lock needed for this */
	sprintf(buf, "%u\n", STATUS_LED->blink_led[idx].blink_flag);
	ret = strlen(buf) + 1;

	return ret;
}

static ssize_t led_blink_solid_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	char *after;
	unsigned long state;
	ssize_t ret = -EINVAL;
	size_t count;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else
		idx = 1;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	state = simple_strtoul(buf, &after, 10);

	count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		spin_lock(&STATUS_LED->data_lock);
		if(0==state)
		{
		       STATUS_LED->blink_led[idx].blink_flag= 0;
			pr_info(ZYF_BL_TAG "DISABLE %s led blink \n",idx?"green":"red");
		}
		else
		{
		       STATUS_LED->blink_led[idx].blink_flag= 1;
		       STATUS_LED->blink_led[idx].blink_led_flag = 0;
			schedule_work(&STATUS_LED->blink_led[idx].work_led_off);
			mod_timer(&STATUS_LED->blink_led[idx].timer,jiffies + msecs_to_jiffies(STATUS_LED->blink_led[idx].blink_off_time));
			pr_info(ZYF_BL_TAG "ENABLE %s led blink \n",idx?"green":"red");
		}
		spin_unlock(&STATUS_LED->data_lock);
	}

	return ret;
}

static DEVICE_ATTR(blink, 0644, led_blink_solid_show, led_blink_solid_store);

static ssize_t cpldled_grpfreq_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else
		idx = 1;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);
	return sprintf(buf, "blink_on_time = %u ms \n", STATUS_LED->blink_led[idx].blink_on_time);
}

static ssize_t cpldled_grpfreq_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	char *after;
	unsigned long state;
	ssize_t ret = -EINVAL;
	size_t count;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else
		idx = 1;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	state = simple_strtoul(buf, &after, 10);

	count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		spin_lock(&STATUS_LED->data_lock);
		STATUS_LED->blink_led[idx].blink_on_time = state;  //in ms
		pr_info(ZYF_BL_TAG "Set %s led blink_on_time=%d ms \n",idx?"green":"red",STATUS_LED->blink_led[idx].blink_on_time);
		spin_unlock(&STATUS_LED->data_lock);
	}

	return ret;
}

static DEVICE_ATTR(grpfreq, 0644, cpldled_grpfreq_show, cpldled_grpfreq_store);

static ssize_t cpldled_grppwm_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else
		idx = 1;
	
	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);
	return sprintf(buf, "blink_off_time = %u ms\n", STATUS_LED->blink_led[idx].blink_off_time);
}

static ssize_t cpldled_grppwm_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct STATUS_LED_data *STATUS_LED;
	int idx = 1;
	char *after;
	unsigned long state;
	ssize_t ret = -EINVAL;
	size_t count;

	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (!strcmp(led_cdev->name, "red"))
		idx = 0;
	else
		idx = 1;

	STATUS_LED = container_of(led_cdev, struct STATUS_LED_data, blink_led[idx].led);

	state = simple_strtoul(buf, &after, 10);

	count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		spin_lock(&STATUS_LED->data_lock);
		STATUS_LED->blink_led[idx].blink_off_time= state;  //in ms
		pr_info(ZYF_BL_TAG "Set %s led blink_off_time=%d ms \n",idx?"green":"red",STATUS_LED->blink_led[idx].blink_off_time);
		spin_unlock(&STATUS_LED->data_lock);
	}

	return ret;
}

static DEVICE_ATTR(grppwm, 0644, cpldled_grppwm_show, cpldled_grppwm_store);

static int msm_pmic_led_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i, j;

	STATUS_LED = kzalloc(sizeof(struct STATUS_LED_data), GFP_KERNEL);
	if (STATUS_LED == NULL) {
		printk(KERN_ERR "STATUS_LED_probe: no memory for device\n");
		ret = -ENOMEM;
		goto err_alloc_failed;
	}
	
	STATUS_LED->blink_led[0].led.name = "red";
	STATUS_LED->blink_led[0].led.brightness_set = msm_pmic_bl_led_set;
	STATUS_LED->blink_led[0].led.brightness = LED_OFF;
	STATUS_LED->blink_led[0].blink_flag = 0;
	STATUS_LED->blink_led[0].blink_on_time = 500;
	STATUS_LED->blink_led[0].blink_off_time = 500;

	STATUS_LED->blink_led[1].led.name = "green";
	STATUS_LED->blink_led[1].led.brightness_set = msm_pmic_bl_led_set;
	STATUS_LED->blink_led[1].led.brightness = LED_OFF;
	STATUS_LED->blink_led[1].blink_flag = 0;
	STATUS_LED->blink_led[1].blink_on_time = 500;
	STATUS_LED->blink_led[1].blink_off_time = 500;

	spin_lock_init(&STATUS_LED->data_lock);

	for (i = 0; i < 2; i++) {	/* red, green */
		ret = led_classdev_register(&pdev->dev, &STATUS_LED->blink_led[i].led);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: led_classdev_register failed\n");
			goto err_led_classdev_register_failed;
		}
	}

	for (i = 0; i < 2; i++) {
		ret =
		    device_create_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: create dev_attr_blink failed\n");
			goto err_out_attr_blink;
		}
	}

	for (i = 0; i < 2; i++) {
		ret =
		    device_create_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_grppwm);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: create dev_attr_grppwm failed\n");
			goto err_out_attr_grppwm;
		}
	}

	for (i = 0; i < 2; i++) {
		ret =
		    device_create_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_grpfreq);
		if (ret) {
			printk(KERN_ERR
			       "STATUS_LED: create dev_attr_grpfreq failed\n");
			goto err_out_attr_grpfreq;
		}
	}
	dev_set_drvdata(&pdev->dev, STATUS_LED);
	
	for (i = 0; i < 2; i++)
	{
	       INIT_WORK(&STATUS_LED->blink_led[i].work_led_on, func[i*2]);
	       INIT_WORK(&STATUS_LED->blink_led[i].work_led_off, func[i*2+1]);
	       setup_timer(&STATUS_LED->blink_led[i].timer, pmic_leds_timer, (unsigned long)&STATUS_LED->blink_led[i]);
		msm_pmic_bl_led_set(&STATUS_LED->blink_led[i].led, LED_OFF);
	}
	
       return 0;
	   
err_out_attr_grpfreq:
	for (j = 0; j < i; j++)
		device_remove_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
	i = 2;
	
err_out_attr_grppwm:
	for (j = 0; j < i; j++)
		device_remove_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
	i = 2;
	
err_out_attr_blink:
	for (j = 0; j < i; j++)
		device_remove_file(STATUS_LED->blink_led[i].led.dev, &dev_attr_blink);
	i = 2;

err_led_classdev_register_failed:
	for (j = 0; j < i; j++)
		led_classdev_unregister(&STATUS_LED->blink_led[i].led);

err_alloc_failed:
	kfree(STATUS_LED);

	return ret;
	
}

static int __devexit msm_pmic_led_remove(struct platform_device *pdev)
{
       int i;
	   
       for (i = 0; i < 2; i++)
		led_classdev_unregister(&STATUS_LED->blink_led[i].led);

	return 0;
}

#ifdef CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND
#include "../../arch/arm/mach-msm/proc_comm.h"
#define NLED_APP2SLEEP_FLAG_LCD 0x0001//green
#define NLED_APP2SLEEP_FLAG_KBD 0x0010//red
//#define NLED_APP2SLEEP_FLAG_VIB 0x0100
#define ZTE_PROC_COMM_CMD3_NLED_BLINK_DISABLE 2
#define ZTE_PROC_COMM_CMD3_NLED_BLINK_ENABLE 3

//inform modem the states of NLED through PCOM_CUSTOMER_CMD2
int msm_pmic_led_config_while_app2sleep(unsigned blink_kbd,unsigned blink_lcd, unsigned set)
{
	int config_last = 0;
	if(blink_kbd > 0)
	{
		config_last |= NLED_APP2SLEEP_FLAG_KBD;
	}
	if(blink_lcd > 0)
	{
		config_last |= NLED_APP2SLEEP_FLAG_LCD;
	}
	pr_info("LHX LED PROC:Green %d,RED%d;config 0x%x,set(2dis 3enable):%d\n",blink_lcd,blink_kbd,config_last,set);
 	return msm_proc_comm(PCOM_CUSTOMER_CMD3, &config_last, &set);
}
#endif

#ifdef CONFIG_PM
static int msm_pmic_led_suspend(struct platform_device *dev,
		pm_message_t state)
{
       int i;
	   #ifdef CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND
	   //blink_led[0] red,blink_led[1] green
	   msm_pmic_led_config_while_app2sleep( STATUS_LED->blink_led[0].led.brightness,//red
	   										STATUS_LED->blink_led[1].led.brightness, ZTE_PROC_COMM_CMD3_NLED_BLINK_ENABLE);//green
	   #endif
       for (i = 0; i < 2; i++)
		led_classdev_suspend(&STATUS_LED->blink_led[i].led);

	return 0;
}

static int msm_pmic_led_resume(struct platform_device *dev)
{
       int i;
	#ifdef CONFIG_ZTE_NLED_BLINK_WHILE_APP_SUSPEND
	   msm_pmic_led_config_while_app2sleep( 0,0, ZTE_PROC_COMM_CMD3_NLED_BLINK_DISABLE);
	#endif
       for (i = 0; i < 2; i++)
		led_classdev_resume(&STATUS_LED->blink_led[i].led);
	

	return 0;
}
#else
#define msm_pmic_led_suspend NULL
#define msm_pmic_led_resume NULL
#endif

static struct platform_driver msm_pmic_led_driver = {
	.probe		= msm_pmic_led_probe,
	.remove		= __devexit_p(msm_pmic_led_remove),
	.suspend	= msm_pmic_led_suspend,
	.resume		= msm_pmic_led_resume,
	.driver		= {
		.name	= "pmic-leds-status",
		.owner	= THIS_MODULE,
	},
};

static int __init msm_pmic_led_init(void)
{
	return platform_driver_register(&msm_pmic_led_driver);
}
module_init(msm_pmic_led_init);

static void __exit msm_pmic_led_exit(void)
{
	platform_driver_unregister(&msm_pmic_led_driver);
}
module_exit(msm_pmic_led_exit);

MODULE_DESCRIPTION("MSM PMIC LEDs driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pmic-leds");
#endif//CONFIG_MACH_SISA

#endif//CONFIG_MACH_IBOX
