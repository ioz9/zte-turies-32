/*
 * Gadget Driver for Android
 *
 * Copyright (C) 2008 Google, Inc.
 * Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 * Author: Mike Lockwood <lockwood@android.com>
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
 */



/* #define DEBUG */
/* #define VERBOSE_DEBUG */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/miscdevice.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>

#include <linux/usb/android.h>
#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>
#if defined(CONFIG_USB_ANDROID_CDC_ECM) || defined(CONFIG_USB_ANDROID_RNDIS)
#include "u_ether.h"
#endif

#include "f_mass_storage.h"
#include "f_adb.h"
#include "u_serial.h"
#ifdef CONFIG_USB_ANDROID_DIAG
#include "f_diag.h"
#endif
#ifdef CONFIG_USB_ANDROID_RMNET
#include "f_rmnet.h"
#endif

#include "gadget_chips.h"

/*
 * Kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */
#include "usbstring.c"
#include "config.c"
#include "epautoconf.c"
#include "composite.c"

MODULE_AUTHOR("Mike Lockwood");
MODULE_DESCRIPTION("Android Composite USB Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");

/* product id */
static u16 product_id;
static int android_set_pid(const char *val, struct kernel_param *kp);
static int android_get_pid(char *buffer, struct kernel_param *kp);
module_param_call(product_id, android_set_pid, android_get_pid,
					&product_id, 0664);
MODULE_PARM_DESC(product_id, "USB device product id");

struct usb_composition *android_validate_product_id(unsigned short pid);


#define PRODUCT_ID_ALL_INTERFACE          0x1350
#define PRODUCT_ID_MS_ADB                      0x1351
#define PRODUCT_ID_ADB                             0x1352
#define PRODUCT_ID_MS                               0x1353
#define PRODUCT_ID_DIAG                           0x0112
#define PRODUCT_ID_DIAG_NMEA_MODEM   0x0111
#define PRODUCT_ID_MODEM_MS_ADB         0x1354
#define PRODUCT_ID_MODEM_MS                 0x1355
#define PRODUCT_ID_MS_CDROM                 0x0083
#define PRODUCT_ID_RNDIS_MS                 0x1364
#define PRODUCT_ID_RNDIS_MS_ADB             0x1364
#define PRODUCT_ID_RNDIS             0x1365
#define PRODUCT_ID_RNDIS_ADB             0x1373

#define PRODUCT_ID_DIAG_MODEM_NEMA_MS_AT             0x1367
#define PRODUCT_ID_DIAG_MODEM_NEMA_MS_ADB_AT             0x1366

int support_assoc_desc(void)
{
	return product_id == PRODUCT_ID_RNDIS ? 0 : 1;
}

struct usb_ex_work
{
	struct workqueue_struct *workqueue;
	int    enable_switch;
	int    enable_linux_switch;
	int switch_pid;
	int has_switch;
	int cur_pid;
	int linux_pid;
	struct delayed_work switch_work;
	struct delayed_work linux_switch_work;
	struct delayed_work plug_work;
	spinlock_t lock;
};

struct usb_ex_work global_usbwork = {0};
static int create_usb_work_queue(void);
static int destroy_usb_work_queue(void);
static void usb_plug_work(struct work_struct *w);
static void usb_switch_work(struct work_struct *w);
static void usb_switch_os_work(struct work_struct *w);
static void clear_switch_flag(void);
static int usb_cdrom_is_enable(void);
static int enble_cdrom_after_switch(void);



enum usb_opt_nv_item
{
	NV_BACK_LIGHT_I=77,
	NV_FTM_MODE_I = 453
};
enum usb_opt_nv_type
{
	NV_READ=0,
	NV_WRITE
};

int
msm_hsusb_get_set_usb_conf_nv_value(uint32_t nv_item,uint32_t value,uint32_t is_write);
int get_ftm_from_tag(void);
static unsigned short g_enable_cdrom = 0;
static int zte_usb_pid[]={
	PRODUCT_ID_ALL_INTERFACE,
        PRODUCT_ID_MS_ADB,
        PRODUCT_ID_ADB,
        PRODUCT_ID_MS,
        PRODUCT_ID_DIAG,
        PRODUCT_ID_DIAG_NMEA_MODEM,
        PRODUCT_ID_MODEM_MS_ADB,
        PRODUCT_ID_MODEM_MS,
        /*ZTE_USB_WZY_101021 start*/
        PRODUCT_ID_DIAG_MODEM_NEMA_MS_AT, 
	PRODUCT_ID_DIAG_MODEM_NEMA_MS_ADB_AT, 
				/*ZTE_USB_WZY_101021 end*/
        PRODUCT_ID_MS_CDROM,
};
#define PRODUCT_ID_COUNT    (sizeof(zte_usb_pid)/sizeof(zte_usb_pid[0]))


#define NV_WRITE_SUCCESS 10



static int ftm_mode = 0;
static int is_ftm_mode(void)
{
	return !!ftm_mode;
}
static void set_ftm_mode(int i)
{
	ftm_mode = i;
	return ;
}

static int get_nv(void) 
{
	return msm_hsusb_get_set_usb_conf_nv_value(NV_BACK_LIGHT_I,0,NV_READ);
}
static int set_nv(int nv)
{
	int r = msm_hsusb_get_set_usb_conf_nv_value(NV_BACK_LIGHT_I,nv,NV_WRITE);
	return (r == NV_WRITE_SUCCESS)? 0:-1;
}


static int config_ftm_from_nv(void)
{
	int i = 0;
	if (is_ftm_mode()) {
		return 0;
	}
	i = get_nv();
	if (i < 0 || i >= PRODUCT_ID_COUNT) {
		return -1;
	}
	set_ftm_mode((PRODUCT_ID_DIAG == zte_usb_pid[i])? 1:0);
	printk("usb: %s, %d: ftm_mode %s\n",
	       __FUNCTION__, __LINE__,
	       is_ftm_mode()?"enable":"disable");
	if (is_ftm_mode()) {
		product_id = PRODUCT_ID_DIAG;
	}
	return 0;
}

static int config_ftm_from_tag(void)
{
	if (is_ftm_mode()) {
		return 0;
	}
	
	set_ftm_mode(get_ftm_from_tag());

	printk("usb: %s, %d: ftm_mode %s\n",
	       __FUNCTION__, __LINE__,
	       is_ftm_mode()?"enable":"disable");
	if (is_ftm_mode()) {
		product_id = PRODUCT_ID_DIAG;
	}
	return 0;
}

static ssize_t msm_hsusb_set_pidnv(struct device *dev,
                                   struct device_attribute *attr,
                                   const char *buf, size_t size)
{
        int value;
        sscanf(buf, "%d", &value);
        set_nv(value);
        return size;
}

static ssize_t msm_hsusb_show_pidnv(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
        int i = 0;
        i = scnprintf(buf, PAGE_SIZE, "nv %d\n", get_nv());

        return i;
}


static ssize_t msm_hsusb_show_ftm(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
        int i = 0;
        i = scnprintf(buf, PAGE_SIZE, "%s\n", is_ftm_mode()?"enable":"disable");
        return i;
}

static ssize_t msm_hsusb_show_enable_cdrom(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int i = 0;	
	i = scnprintf(buf, PAGE_SIZE, "%d\n", g_enable_cdrom);	
	return i;
}

static ssize_t msm_hsusb_store_enable_cdrom(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
       unsigned long cdrom_enable = 0;
      	if (!strict_strtoul(buf, 16, &cdrom_enable)) {
		g_enable_cdrom = (cdrom_enable == 0x5656)? 1 : 0;
		pr_info("%s: Requested enable_cdrom = %d 0x%lx\n",
			__func__, g_enable_cdrom, cdrom_enable);	
	} else {
		pr_info("%s: strict_strtoul conversion failed, %s\n", __func__, buf);
	}
	return size;
}

#define MAX_SERIAL_LEN 256
static char serial_number[MAX_SERIAL_LEN] = "1234567890ABCDEF";
static struct kparam_string kps = {
	.string			= serial_number,
	.maxlen			= MAX_SERIAL_LEN,
};
static int android_set_sn(const char *kmessage, struct kernel_param *kp);
module_param_call(serial_number, android_set_sn, param_get_string,
						&kps, 0664);
MODULE_PARM_DESC(serial_number, "SerialNumber string");

static const char longname[] = "Gadget Android";
#if defined(CONFIG_USB_ANDROID_CDC_ECM) || defined(CONFIG_USB_ANDROID_RNDIS)
static u8 hostaddr[ETH_ALEN];
#endif

/* Default vendor ID, overridden by platform data */
#define VENDOR_ID		0x18D1

struct android_dev {
	struct usb_gadget *gadget;
	struct usb_composite_dev *cdev;

	int version;

	int adb_enabled;
	int nluns;
	struct mutex lock;
	struct android_usb_platform_data *pdata;
	unsigned long functions;
};

static struct android_dev *_android_dev;

/* string IDs are assigned dynamically */

#define STRING_MANUFACTURER_IDX		0
#define STRING_PRODUCT_IDX		1
#define STRING_SERIAL_IDX		2

char g_manufacturer_name[64] = {0};
char g_product_name[64] = {0};
char g_mass_vendor_name[64] = {"ZTE"};
  

/* String Table */
static struct usb_string strings_dev[] = {
	/* These dummy values should be overridden by platform data */
	[STRING_MANUFACTURER_IDX].s = "Android",
	[STRING_PRODUCT_IDX].s = "Android",
	{  }			/* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};

static struct usb_device_descriptor device_desc = {
	.bLength              = sizeof(device_desc),
	.bDescriptorType      = USB_DT_DEVICE,
	.bcdUSB               = __constant_cpu_to_le16(0x0200),
	.bDeviceClass         = USB_CLASS_PER_INTERFACE,
	.idVendor             = __constant_cpu_to_le16(VENDOR_ID),
	.bcdDevice            = __constant_cpu_to_le16(0xffff),
	.bNumConfigurations   = 1,
};

#define android_func_attr(function, index)				\
static ssize_t  show_##function(struct device *dev,			\
		struct device_attribute *attr, char *buf)		\
{									\
									\
	unsigned long n = _android_dev->functions;			\
	int val = 0;							\
									\
	while (n) {							\
		if ((n & 0x0F) == index)				\
			val = 1;					\
		n = n >> 4;						\
	}								\
	return sprintf(buf, "%d\n", val);				\
									\
}									\
									\
static DEVICE_ATTR(function, S_IRUGO, show_##function, NULL);

android_func_attr(adb, ANDROID_ADB);
android_func_attr(mass_storage, ANDROID_MSC);
android_func_attr(acm_modem, ANDROID_ACM_MODEM);
android_func_attr(acm_nmea, ANDROID_ACM_NMEA);
android_func_attr(diag, ANDROID_DIAG);
android_func_attr(modem, ANDROID_GENERIC_MODEM);
android_func_attr(nmea, ANDROID_GENERIC_NMEA);
android_func_attr(at, ANDROID_GENERIC_AT);/*ZTE_USB_WZY_101021*/
android_func_attr(cdc_ecm, ANDROID_CDC_ECM);
android_func_attr(rmnet, ANDROID_RMNET);
android_func_attr(rndis, ANDROID_RNDIS);


static ssize_t msm_hsusb_show_exwork(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int i = 0;
	struct usb_ex_work *p = &global_usbwork;
	i = scnprintf(buf, PAGE_SIZE,
				"auto switch: %s\nlinux switch: %s\nswitch_pid: 0x%x\ncur_pid: 0x%x\nlinux_pid: 0x%x\n",
		      p->enable_switch?"enable":"disable",
		      p->enable_linux_switch?"enable":"disable",
		      p->switch_pid,
		      p->cur_pid,
		      p->linux_pid);
	
	return i;
}
static ssize_t msm_hsusb_store_enable_switch(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
     unsigned long enable;
      	if (!strict_strtoul(buf, 16, &enable)) {
		global_usbwork.enable_switch = enable;
	} 
	return size;
}


static ssize_t msm_hsusb_store_enable_linux_switch(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
     unsigned long enable;
      	if (!strict_strtoul(buf, 16, &enable)) {
		global_usbwork.enable_linux_switch = enable;
	} 
	return size;
}

static ssize_t msm_hsusb_store_cur_pid(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t size)
{     
    
       unsigned long value;
	if (!strict_strtoul(buf, 16, &value)) {
      		printk("usb: cur_pid value=0x%x\n",(unsigned int)value);
		if(android_validate_product_id((unsigned short)value))
			global_usbwork.cur_pid=(unsigned int) value;
	}
	return size;
}


static ssize_t msm_hsusb_store_switch_pid(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t size)
{     
     
       unsigned long value;
	if (!strict_strtoul(buf, 16, &value)) {
      		printk("usb: switch_pid value=0x%x\n",(unsigned int)value);
		if(android_validate_product_id((u16)value))
			global_usbwork.switch_pid= (unsigned int)value;
	}
	
	return size;
}

static ssize_t msm_hsusb_store_linux_pid(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t size)
{     
   
       unsigned long value;
	if (!strict_strtoul(buf, 16, &value)) {
      		printk("usb: linux_pid value=0x%x\n",(unsigned int)value);
		if(android_validate_product_id((u16)value))
			global_usbwork.linux_pid= (unsigned int)value;
	}

	return size;
}



static ssize_t android_show_product_name(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int len = 0;
	len = scnprintf(buf, PAGE_SIZE,
		"hsusb product_name is %s \n",
			strings_dev[STRING_PRODUCT_IDX].s);
	return len;
}
	

static ssize_t android_store_product_name(struct device *dev, 
						struct device_attribute *attr,
						const char* buf, size_t size)
{

	strncpy(g_product_name, buf, sizeof(g_product_name));
	g_product_name[sizeof(g_product_name) - 1] = 0;
	strings_dev[STRING_PRODUCT_IDX].s = g_product_name;
	return size;
}

static ssize_t android_show_manufacturer_name(struct device *dev,
			     struct device_attribute *attr,
					      char *buf)
{
	int len = 0;
	
	len = scnprintf(buf, PAGE_SIZE,
			"hsusb manufacturer_name is %s\n",
			strings_dev[STRING_MANUFACTURER_IDX].s);
	return len;
}


static ssize_t android_store_manufacturer_name(struct device *dev, 
						struct device_attribute *attr,
						const char* buf, size_t size)
{
	strncpy(g_manufacturer_name, buf, sizeof(g_manufacturer_name));
	g_manufacturer_name[sizeof(g_manufacturer_name) - 1] = 0;
	strings_dev[STRING_MANUFACTURER_IDX].s = g_manufacturer_name;
	return size;
}

static ssize_t android_show_mass_vendor_name(struct device *dev,
			     struct device_attribute *attr,
					      char *buf)
{
	int len = 0;
	
	len = scnprintf(buf, PAGE_SIZE,
			"hsusb mass manufacturer_name is %s\n",
			g_mass_vendor_name);
	return len;
}


static ssize_t android_store_mass_vendor_name(struct device *dev, 
						struct device_attribute *attr,
						const char* buf, size_t size)
{
	strncpy(g_mass_vendor_name, buf, sizeof(g_mass_vendor_name));
	g_mass_vendor_name[sizeof(g_mass_vendor_name) - 1] = 0;
	return size;
}

const char * get_mass_vendor_name(void)
{
	return g_mass_vendor_name;
}
EXPORT_SYMBOL(get_mass_vendor_name);

static DEVICE_ATTR(exwork, 0664,
		   msm_hsusb_show_exwork, NULL);
static DEVICE_ATTR(enable_switch, 0664,
		   NULL, msm_hsusb_store_enable_switch);

static DEVICE_ATTR(linux_switch, 0664,
		   NULL, msm_hsusb_store_enable_linux_switch);
static DEVICE_ATTR(cur_pid, 0664,
		   NULL, msm_hsusb_store_cur_pid);
static DEVICE_ATTR(switch_pid, 0664,
		   NULL, msm_hsusb_store_switch_pid);
static DEVICE_ATTR(linux_pid, 0664,
		   NULL, msm_hsusb_store_linux_pid);
static DEVICE_ATTR(pidnv, 0664,
                   msm_hsusb_show_pidnv, msm_hsusb_set_pidnv);
static DEVICE_ATTR(ftm_mode, 0664,
                   msm_hsusb_show_ftm, NULL);

static DEVICE_ATTR(enable_cdrom, 0664,
		msm_hsusb_show_enable_cdrom, msm_hsusb_store_enable_cdrom);

static DEVICE_ATTR(product_name, 0664,
		   android_show_product_name, android_store_product_name);

static DEVICE_ATTR(manufacturer_name, 0664,
		android_show_manufacturer_name, android_store_manufacturer_name);
static DEVICE_ATTR(mass_vendor_name, 0664,
		   android_show_mass_vendor_name, android_store_mass_vendor_name);


static struct attribute *android_func_attrs[] = {
	&dev_attr_adb.attr,
	&dev_attr_mass_storage.attr,
	&dev_attr_acm_modem.attr,
	&dev_attr_acm_nmea.attr,
	&dev_attr_diag.attr,
	&dev_attr_modem.attr,
	&dev_attr_nmea.attr,
	&dev_attr_at.attr,
	&dev_attr_cdc_ecm.attr,
	&dev_attr_rmnet.attr,
	&dev_attr_rndis.attr,

	&dev_attr_exwork.attr,
	&dev_attr_cur_pid.attr,
	&dev_attr_switch_pid.attr,
	&dev_attr_linux_pid.attr,
	&dev_attr_linux_switch.attr,
	&dev_attr_enable_switch.attr,
	

	&dev_attr_pidnv.attr,
	&dev_attr_ftm_mode.attr,
	&dev_attr_enable_cdrom.attr, 

	&dev_attr_manufacturer_name.attr,
	&dev_attr_product_name.attr,
	&dev_attr_mass_vendor_name.attr,

	NULL,
};

static struct attribute_group android_func_attr_grp = {
	.name  = "functions",
	.attrs = android_func_attrs,
};

static int  android_bind_config(struct usb_configuration *c)
{
	struct android_dev *dev = _android_dev;
	int ret = -EINVAL;
	unsigned long n;
	pr_debug("android_bind_config c = 0x%x dev->cdev=0x%x\n",
		(unsigned int) c, (unsigned int) dev->cdev);
	n = dev->functions;
	while (n) {
		switch (n & 0x0F) {
		case ANDROID_ADB:
			ret = adb_function_add(dev->cdev, c);
			if (ret)
				return ret;
			break;
		case ANDROID_MSC:
			ret = mass_storage_function_add(dev->cdev, c,
					     (usb_cdrom_is_enable()||enble_cdrom_after_switch())?2:dev->nluns);
			if (ret)
				return ret;
			break;
		case ANDROID_ACM_MODEM:
			ret = acm_bind_config(c, 0);
			if (ret)
				return ret;
			break;
		case ANDROID_ACM_NMEA:
			ret = acm_bind_config(c, 1);
			if (ret)
				return ret;
			break;
#ifdef CONFIG_USB_ANDROID_DIAG
		case ANDROID_DIAG:
			ret = diag_function_add(c, serial_number);
			if (ret)
				return ret;
			break;
#endif
#ifdef CONFIG_USB_F_SERIAL
		case ANDROID_GENERIC_MODEM:
			ret = gser_bind_config(c, 0);
			if (ret)
				return ret;
			break;
		case ANDROID_GENERIC_NMEA:
			ret = gser_bind_config(c, 1);
			if (ret)
				return ret;
			break;
		/*ZTE_USB_WZY_101021 start*/	
		case ANDROID_GENERIC_AT:
			ret = gser_bind_config(c, 2);
			if (ret)
				return ret;
			break;	
		/*ZTE_USB_WZY_101021 end*/	
#endif
#ifdef CONFIG_USB_ANDROID_CDC_ECM
		case ANDROID_CDC_ECM:
			ret = ecm_bind_config(c, hostaddr);
			if (ret)
				return ret;
			break;
#endif
#ifdef CONFIG_USB_ANDROID_RMNET
		case ANDROID_RMNET:
			ret = rmnet_function_add(c);
			if (ret) {
				pr_err("failed to add rmnet function\n");
				return ret;
			}
			break;
#endif
#ifdef CONFIG_USB_ANDROID_RNDIS
		case ANDROID_RNDIS:
			ret = rndis_bind_config(c, hostaddr);
			if (ret)
				return ret;
			break;
#endif
		default:
			ret = -EINVAL;
			return ret;
		}
		n = n >> 4;
	}
	return ret;

}

static int get_num_of_serial_ports(void)
{
	struct android_dev *dev = _android_dev;
	unsigned long n = dev->functions;
	unsigned ports = 0;

	while (n) {
		switch (n & 0x0F) {
		case ANDROID_ACM_MODEM:
		case ANDROID_ACM_NMEA:
		case ANDROID_GENERIC_MODEM:
		case ANDROID_GENERIC_NMEA:
		case ANDROID_GENERIC_AT:	
			ports++;
		}
		n = n >> 4;
	}

	return ports;
}

static int is_iad_enabled(void)
{
	struct android_dev *dev = _android_dev;
	unsigned long n = dev->functions;

	while (n) {
		switch (n & 0x0F) {
		case ANDROID_ACM_MODEM:
		case ANDROID_ACM_NMEA:
#ifdef CONFIG_USB_ANDROID_RNDIS
		case ANDROID_RNDIS:
#endif
			return 1;
		}
		n = n >> 4;
	}

	return 0;
}

static struct usb_configuration android_config_driver = {
	.label		= "android",
	.bind		= android_bind_config,
	.bConfigurationValue = 1,
	.bMaxPower	= 0xFA, /* 500ma */
};

static int android_unbind(struct usb_composite_dev *cdev)
{
	if (get_num_of_serial_ports())
		gserial_cleanup();

	return 0;
}

static int  android_bind(struct usb_composite_dev *cdev)
{
	struct android_dev *dev = _android_dev;
	struct usb_gadget	*gadget = cdev->gadget;
	int			gcnum;
	int			id;
	int			ret;
	int                     num_ports;

	pr_debug("android_bind\n");

	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */
	id = usb_string_id(cdev);
	if (id < 0)
		return id;
	strings_dev[STRING_MANUFACTURER_IDX].id = id;
	device_desc.iManufacturer = id;

	id = usb_string_id(cdev);
	if (id < 0)
		return id;
	strings_dev[STRING_PRODUCT_IDX].id = id;
	device_desc.iProduct = id;


	if (!is_ftm_mode()) {
		id = usb_string_id(cdev);
		if (id < 0)
			return id;
		strings_dev[STRING_SERIAL_IDX].id = id;
		device_desc.iSerialNumber = id;
	} else {
		strings_dev[STRING_SERIAL_IDX].id = 0;
		device_desc.iSerialNumber = 0;
	}
	

	device_desc.idProduct = __constant_cpu_to_le16(product_id);
	/* Supporting remote wakeup for mass storage only function
	 * does n't make sense, since there are no notifications that
	 * can be sent from mass storage during suspend */
	if ((gadget->ops->wakeup) && (dev->functions != ANDROID_MSC))
		android_config_driver.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	else
		android_config_driver.bmAttributes &= ~USB_CONFIG_ATT_WAKEUP;

	if (dev->pdata->self_powered && !usb_gadget_set_selfpowered(gadget)) {
		android_config_driver.bmAttributes |= USB_CONFIG_ATT_SELFPOWER;
		android_config_driver.bMaxPower	= 0x32; /* 100 mA */
	}
	dev->cdev = cdev;
	pr_debug("android_bind assigned dev->cdev\n");
	dev->gadget = gadget;

	num_ports = get_num_of_serial_ports();
	if (num_ports) {
		ret = gserial_setup(cdev->gadget, num_ports);
		if (ret < 0)
			return ret;
	}

	/* Android user space allows USB tethering only when usb0 is listed
	 * in network interfaces. Setup network link though RNDIS/CDC-ECM
	 * is not listed in current composition. Network links is not setup
	 * for every composition switch. It is setup one time and teared down
	 * during module removal.
	 */
#if defined(CONFIG_USB_ANDROID_CDC_ECM) || defined(CONFIG_USB_ANDROID_RNDIS)
	/* set up network link layer */
	ret = gether_setup(cdev->gadget, hostaddr);
	if (ret && (ret != -EBUSY)) {
		gserial_cleanup();
		return ret;
	}
#endif

	/* register our configuration */
	ret = usb_add_config(cdev, &android_config_driver);
	if (ret) {
		pr_err("usb_add_config failed\n");
		return ret;
	}

	gcnum = usb_gadget_controller_number(gadget);
	if (gcnum >= 0)
		device_desc.bcdDevice = cpu_to_le16(0x0200 + gcnum);
	else {
		/* gadget zero is so simple (for now, no altsettings) that
		 * it SHOULD NOT have problems with bulk-capable hardware.
		 * so just warn about unrcognized controllers -- don't panic.
		 *
		 * things like configuration and altsetting numbering
		 * can need hardware-specific attention though.
		 */
		pr_warning("%s: controller '%s' not recognized\n",
			longname, gadget->name);
		device_desc.bcdDevice = __constant_cpu_to_le16(0x9999);
	}

	if (is_iad_enabled()) {
		device_desc.bDeviceClass         = USB_CLASS_MISC;
		device_desc.bDeviceSubClass      = 0x02;
		device_desc.bDeviceProtocol      = 0x01;

		if (!support_assoc_desc()) {
			device_desc.bDeviceClass         =  USB_CLASS_WIRELESS_CONTROLLER;
			device_desc.bDeviceSubClass      = 0;
			device_desc.bDeviceProtocol      = 0;
		}

	} else {
		device_desc.bDeviceClass         = USB_CLASS_PER_INTERFACE;
		device_desc.bDeviceSubClass      = 0;
		device_desc.bDeviceProtocol      = 0;
	}
	pr_debug("android_bind done\n");
	return 0;
}

static struct usb_composite_driver android_usb_driver = {
	.name		= "android_usb",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.bind		= android_bind,
	.unbind		= android_unbind,
};

struct usb_composition *android_validate_product_id(unsigned short pid)
{
	struct android_dev *dev = _android_dev;
	struct usb_composition *fi;
	int i;

	for (i = 0; i < dev->pdata->num_compositions; i++) {
		fi = &dev->pdata->compositions[i];
		pr_debug("pid=0x%x apid=0x%x\n",
		       fi->product_id, fi->adb_product_id);
		if ((fi->product_id == pid) || (fi->adb_product_id == pid))
			return fi;
	}
	return NULL;
}

static int android_switch_composition(u16 pid)
{
	struct android_dev *dev = _android_dev;
	struct usb_composition *func;
	int ret;

	/* Validate the prodcut id */
	func = android_validate_product_id(pid);
	if (!func) {
		pr_err("%s: invalid product id %x\n", __func__, pid);
		return -EINVAL;
	}

	/* Honour adb users */
	if (dev->adb_enabled) {
		product_id = func->adb_product_id;
		dev->functions = func->adb_functions;
	} else {
		product_id = func->product_id;
		dev->functions = func->functions;
	}
	printk("usb:%s: %d: current pid 0x%x\n", __FUNCTION__, __LINE__, product_id);
	usb_composite_unregister(&android_usb_driver);
	ret = usb_composite_register(&android_usb_driver);

	return ret;
}

static ssize_t android_remote_wakeup(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_gadget *gadget = _android_dev->gadget;

	if (!gadget)
		return -ENODEV;

	pr_debug("Calling remote wakeup....\n");
	usb_gadget_wakeup(gadget);

	return count;
}
static DEVICE_ATTR(remote_wakeup, S_IWUSR, 0, android_remote_wakeup);

static struct attribute *android_attrs[] = {
	&dev_attr_remote_wakeup.attr,
	NULL,
};

static struct attribute_group android_attr_grp = {
	.attrs = android_attrs,
};

static int android_set_sn(const char *kmessage, struct kernel_param *kp)
{
	int len = strlen(kmessage);

	if (len >= MAX_SERIAL_LEN) {
		pr_err("serial number string too long\n");
		return -ENOSPC;
	}

	strlcpy(serial_number, kmessage, MAX_SERIAL_LEN);
	/* Chop out \n char as a result of echo */
	if (serial_number[len - 1] == '\n')
		serial_number[len - 1] = '\0';

	return 0;
}

static int android_set_pid(const char *val, struct kernel_param *kp)
{
	int ret = 0;
	unsigned long tmp;

	ret = strict_strtoul(val, 16, &tmp);
	if (ret)
		goto out;
	printk("usb:%s: %d 0x%lx\n", __FUNCTION__, __LINE__, tmp);
	/* We come here even before android_probe, when product id
	 * is passed via kernel command line.
	 */
	if (!_android_dev) {
		product_id = tmp;
		goto out;
	}
	
	mutex_lock(&_android_dev->lock);
	ret = android_switch_composition(tmp);
	mutex_unlock(&_android_dev->lock);

	clear_switch_flag();

out:
	return ret;
}

static int android_get_pid(char *buffer, struct kernel_param *kp)
{
	int ret;

	mutex_lock(&_android_dev->lock);
	ret = sprintf(buffer, "%x", product_id);
	mutex_unlock(&_android_dev->lock);

	return ret;
}

static int adb_enable_open(struct inode *ip, struct file *fp)
{
	struct android_dev *dev = _android_dev;
	int ret = 0;

	mutex_lock(&dev->lock);

	if (dev->adb_enabled)
		goto out;

	dev->adb_enabled = 1;
	pr_debug("enabling adb\n");
	if (product_id) 
		ret = android_switch_composition(product_id);

out:
	mutex_unlock(&dev->lock);

	return ret;
}

static int adb_enable_release(struct inode *ip, struct file *fp)
{
	struct android_dev *dev = _android_dev;
	int ret = 0;

	mutex_lock(&dev->lock);

	if (!dev->adb_enabled)
		goto out;

	pr_debug("disabling adb\n");
	dev->adb_enabled = 0;
	if (product_id)
		ret = android_switch_composition(product_id);
out:
	mutex_unlock(&dev->lock);

	return ret;
}

static struct file_operations adb_enable_fops = {
	.owner =   THIS_MODULE,
	.open =    adb_enable_open,
	.release = adb_enable_release,
};

static struct miscdevice adb_enable_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "android_adb_enable",
	.fops = &adb_enable_fops,
};

static int __init android_probe(struct platform_device *pdev)
{
	struct android_usb_platform_data *pdata = pdev->dev.platform_data;
	struct android_dev *dev = _android_dev;
	int ret;

	pr_debug("android_probe pdata: %p\n", pdata);

	if (!pdata || !pdata->vendor_id || !pdata->product_name ||
		!pdata->manufacturer_name)
		return -ENODEV;

	device_desc.idVendor =	__constant_cpu_to_le16(pdata->vendor_id);
	dev->version = pdata->version;
	strings_dev[STRING_PRODUCT_IDX].s = pdata->product_name;
	strings_dev[STRING_MANUFACTURER_IDX].s = pdata->manufacturer_name;
	strings_dev[STRING_SERIAL_IDX].s = serial_number;
	dev->nluns = pdata->nluns;
	dev->pdata = pdata;

	ret = sysfs_create_group(&pdev->dev.kobj, &android_attr_grp);
	if (ret < 0) {
		pr_err("%s: Failed to create the sysfs entry \n", __func__);
		return ret;
	}
	ret = sysfs_create_group(&pdev->dev.kobj, &android_func_attr_grp);
	if (ret < 0) {
		pr_err("%s: Failed to create the functions sysfs entry \n",
				__func__);
		sysfs_remove_group(&pdev->dev.kobj, &android_attr_grp);
	}

	return ret;
}

static struct platform_driver android_platform_driver = {
	.driver = { .name = "android_usb", },
	.probe = android_probe,
};

static int __init init(void)
{
	struct android_dev *dev;
	struct usb_composition *func;
	int ret;

	pr_debug("android init\n");

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto out;
	}


	config_ftm_from_tag();
	config_ftm_from_nv();

	_android_dev = dev;
	mutex_init(&dev->lock);

	create_usb_work_queue();

	ret = adb_function_init();
	if (ret)
		goto free_dev;

	ret = platform_driver_register(&android_platform_driver);
	if (ret)
		goto adb_exit;

	ret = misc_register(&adb_enable_device);
	if (ret)
		goto pdrv_unregister;

	/* Defer composite driver registration till product id is available */
	mutex_lock(&dev->lock);
	if (!product_id) {
		mutex_unlock(&dev->lock);
		ret = 0; /* not failure */
		goto out;
	}

	func = android_validate_product_id(product_id);
	if (!func) {
		mutex_unlock(&dev->lock);
		pr_err("%s: invalid product id\n", __func__);
		ret = -EINVAL;
		goto misc_deregister;
	}
	dev->functions = func->functions;

	ret = usb_composite_register(&android_usb_driver);
	if (ret) {
		mutex_unlock(&dev->lock);
		goto misc_deregister;
	}
	mutex_unlock(&dev->lock);

	return 0;

misc_deregister:
	misc_deregister(&adb_enable_device);
pdrv_unregister:
	platform_driver_unregister(&android_platform_driver);
adb_exit:
	adb_function_exit();
free_dev:
	kfree(dev);
out:
	return ret;
}
module_init(init);

static void __exit cleanup(void)
{
#if defined(CONFIG_USB_ANDROID_CDC_ECM) || defined(CONFIG_USB_ANDROID_RNDIS)
	gether_cleanup();
#endif

	usb_composite_unregister(&android_usb_driver);
	misc_deregister(&adb_enable_device);
	platform_driver_unregister(&android_platform_driver);
	adb_function_exit();
	kfree(_android_dev);
	_android_dev = NULL;

	destroy_usb_work_queue();

}
module_exit(cleanup);





static int create_usb_work_queue(void)
{
	struct usb_ex_work *p = &global_usbwork;
	if (p->workqueue) {
		printk(KERN_ERR"usb:workqueue has created");
		return 0;
	}
	spin_lock_init(&p->lock);
	p->enable_switch = 1;
	p->enable_linux_switch = 0;
	p->switch_pid = PRODUCT_ID_MS_ADB;
	p->linux_pid = PRODUCT_ID_MS_ADB;
	p->cur_pid = PRODUCT_ID_MS_CDROM;
	p->has_switch = 0;
	p->workqueue = create_singlethread_workqueue("usb_workqueue");
	if (NULL == p->workqueue) {
		printk(KERN_ERR"usb:workqueue created fail");
		p->enable_switch = 0;
		return -1;
	}
	INIT_DELAYED_WORK(&p->switch_work, usb_switch_work);
	INIT_DELAYED_WORK(&p->linux_switch_work, usb_switch_os_work);
	INIT_DELAYED_WORK(&p->plug_work, usb_plug_work);
	return 0;
}



static int destroy_usb_work_queue(void)
{
	struct usb_ex_work *p = &global_usbwork;
	if (NULL != p->workqueue) {
		destroy_workqueue(p->workqueue);
		p->workqueue = NULL;
	}
	memset(&global_usbwork, 0, sizeof(global_usbwork));
	return 0;
}

static void usb_plug_work(struct work_struct *w)
{
	unsigned long flags;
	int pid = 0;
	struct usb_ex_work *p = container_of(w, struct usb_ex_work, plug_work.work);

	if (!_android_dev) {

		printk(KERN_ERR"usb:%s: %d: _android_dev == NULL\n",
		       __FUNCTION__, __LINE__);
		return ;
	}
	
	spin_lock_irqsave(&p->lock, flags);
	if (!p->has_switch) {
		printk("usb:rms: %s %d: \n", __FUNCTION__, __LINE__);
		spin_unlock_irqrestore(&p->lock, flags);
		return ;
	}
	printk("usb:rms: %s %d: \n", __FUNCTION__, __LINE__);
	p->has_switch = 0;
	pid = p->cur_pid;
	spin_unlock_irqrestore(&p->lock, flags);
	//enable_cdrom(1);
	//DBG("plug work");
	printk("usb:rms %s:%d pid 0x%x cur_pid 0x%x\n",
	       __FUNCTION__, __LINE__, product_id, pid);

	mutex_lock(&_android_dev->lock);
	android_switch_composition((unsigned short)pid);
	mutex_unlock(&_android_dev->lock);

	return ;
}

static void usb_switch_work(struct work_struct *w)
{
	struct usb_ex_work *p = container_of(w, struct usb_ex_work, switch_work.work);
	unsigned long flags;
	if (!_android_dev) {

		printk(KERN_ERR"usb:%s: %d: _android_dev == NULL\n",
		       __FUNCTION__, __LINE__);
		return ;
	}
	if (!p->enable_switch) {
		return ;
	}
	if (p->has_switch) {
		printk("usb:rms:%s %d: already switch pid 0x%x switch_pid 0x%x\n",
		       __FUNCTION__, __LINE__, product_id, p->switch_pid);
		return ;
	}
	spin_lock_irqsave(&p->lock, flags);
//	p->cur_pid = ui->composition->product_id;
	p->has_switch = 1;
	spin_unlock_irqrestore(&p->lock, flags);
//	DBG("auto switch usb mode");
	printk("usb:rms:%s %d: pid 0x%x switch_pid 0x%x\n",
	       __FUNCTION__, __LINE__, product_id, p->switch_pid);
	//enable_cdrom(0);

	mutex_lock(&_android_dev->lock);
	android_switch_composition((unsigned short)p->switch_pid);
	mutex_unlock(&_android_dev->lock);

	return ;
}

static void usb_switch_os_work(struct work_struct *w)
{
	struct usb_ex_work *p =
		container_of(w, struct usb_ex_work, linux_switch_work.work);
	unsigned long flags;

	if (!_android_dev) {

		printk(KERN_ERR"usb:%s: %d: _android_dev == NULL\n",
		       __FUNCTION__, __LINE__);
		return ;
	}

	if (!p->enable_switch || !p->enable_linux_switch || p->has_switch) {
		//switch  or linux_switch are enable, or we has already switch,return direct
		printk("usb:rms:%s:%d, switch %s: linux switch %s: %s switch\n",
		       __FUNCTION__, __LINE__, p->enable_switch?"enable":"disable",
		       p->enable_linux_switch?"enable":"disable",
		       p->has_switch?"has":"has not");
		return ;
	}
	spin_lock_irqsave(&p->lock, flags);
//	p->cur_pid = ui->composition->product_id;
	p->has_switch = 1;
	spin_unlock_irqrestore(&p->lock, flags);
	printk("usb:rms:%s %d: pid 0x%x linux_pid 0x%x\n",
	       __FUNCTION__, __LINE__, product_id, p->linux_pid);

	mutex_lock(&_android_dev->lock);
	android_switch_composition((unsigned short)p->linux_pid);
	mutex_unlock(&_android_dev->lock);

	return ;
}

void schedule_cdrom_stop(void)
{
	
	if (NULL == global_usbwork.workqueue) {
		return ;
	}
	queue_delayed_work(global_usbwork.workqueue, &global_usbwork.switch_work, HZ/10);

	return;
}
EXPORT_SYMBOL(schedule_cdrom_stop);
void schedule_linux_os(void)
{
	if (NULL == global_usbwork.workqueue) {
		return ;
	}
	queue_delayed_work(global_usbwork.workqueue,
			   &global_usbwork.linux_switch_work, 0);

	return;
}
EXPORT_SYMBOL(schedule_linux_os);

void schedule_usb_plug(void)
{
	
	if (NULL == global_usbwork.workqueue) {
		return ;
	}
	printk("usb:rms: %s %d: \n", __FUNCTION__, __LINE__);
	queue_delayed_work(global_usbwork.workqueue, &global_usbwork.plug_work, 0);

	return ;
}
EXPORT_SYMBOL(schedule_usb_plug);

static void clear_switch_flag(void)
{
	unsigned long flags;
	struct usb_ex_work *p = &global_usbwork;
	spin_lock_irqsave(&p->lock, flags);
	p->has_switch = 0;
	spin_unlock_irqrestore(&p->lock, flags);

	return ;
}


static int usb_cdrom_is_enable(void)
{
	return (PRODUCT_ID_MS_CDROM == product_id) ? 1:0;
}


static int enble_cdrom_after_switch(void)
{
	return g_enable_cdrom;	
        /* int enable = 0; */
	/* unsigned short temp, i=0; */
	
        /* if (NULL == product_id) { */
        /*         printk(KERN_ERR"usb: error %s %d\n", __FUNCTION__, __LINE__); */
        /*         return 0; */
        /* } */
 
	/* temp=g_enable_cdrom;	 */
	/* if(0xFFFF==temp) { */
	/* 	enable = 1; */
	/* }else{ */
	/* 	while(temp){ */
	/* 		i = 31 - __builtin_clz(temp); */
	/* 		if(zte_usb_pid[i]==product_id){ */
	/* 			enable=1; */
	/* 			break; */
	/* 		} */
	/* 		temp&=~(1<<i); */
	/* 	} */
	/* } */
        /* printk("usb: %s: pid: 0x%x cdrom: %s\n",__FUNCTION__, */
	/*        product_id,enable ?"enable":"disable"); */

        /* return enable; */
}
EXPORT_SYMBOL(enble_cdrom_after_switch);

int os_switch_is_enable(void)
{
	struct usb_ex_work *p = &global_usbwork;
	
	return usb_cdrom_is_enable()?(p->enable_linux_switch) : 0;
}
EXPORT_SYMBOL(os_switch_is_enable);



