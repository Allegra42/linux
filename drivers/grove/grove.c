#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/string.h>

#include "grove_ioctl.h"


struct grove_t {
   dev_t devnum;
   struct cdev cdev;
   struct i2c_client *rgb_client;
   struct i2c_client *lcd_client;
   struct color_t color;
   char line_one[LINE_SIZE + 1];
   char line_two[LINE_SIZE + 1];
};

static struct class *grove_class;
static DEFINE_MUTEX(grove_mutex);


ssize_t grove_read(struct file *file, char __user *buf, size_t size, loff_t *off)
{
    struct grove_t *grove;
    char send[100];
    int actual, not_copied = 0;

    grove = file->private_data;

    actual = sprintf(send, "Grove LCD RGB Status:\nRed: 0x%x\nGreen: 0x%x\nBlue: 0x%x\nDisplay Text:\n%s\n%s\n",
	    grove->color.red, grove->color.green, grove->color.blue, grove->line_one, grove->line_two);

    if ((int)*off > actual) {
	return 0;
    }
    actual = min(actual+1, (int)size);

    not_copied = copy_to_user(buf, send, actual);
    if (not_copied) {
	return -EFAULT;
    }
    *off += actual;

    return actual;
}

ssize_t grove_write(struct file *file, const char __user *buf, size_t size, loff_t *off)
{
    // Especially write is more meaningful for the LCD part, but as these is not working at the moment
    // we one for RGB as an example and for testing.

    struct grove_t *grove;
    int ret = 0;
    int i = 0;
    int actual = 0;
    int not_copied = 0;
    char kbuf[15]; // assumed for the rgb string, change for real lcd impl.
    char *tmp;
    char *ptr;

    grove = file->private_data;

    if (*off > 15) {
	return -EINVAL;
    }

    actual = min(15, (int)size);
    not_copied = copy_from_user(kbuf, buf, actual);
    if (not_copied) {
	return -EFAULT;
    }

    *off += size;
    tmp = kbuf;

    mutex_lock(&grove_mutex);

    while ((ptr = strsep(&tmp, " ")) !=  NULL) {
	if (i == 0 && ptr[0] == 'r') {
		ret = kstrtou8(++ptr, 10, &grove->color.red);
	} else if (i == 1 && ptr[0] == 'g') {
		ret = kstrtou8(++ptr, 10, &grove->color.green);
	} else if (i == 2 && ptr[0] == 'b') {
		ret = kstrtou8(++ptr, 10, &grove->color.blue);
	} else {
		pr_err("Wrong input format!\n Use r<0-255> g<> b<>\n");
		goto fail;
	}
	    i++;
    }

    struct i2c_cmd_t cmds[] = {
	    {RED, grove->color.red},
	    {GREEN, grove->color.green},
	    {BLUE, grove->color.blue},
    };

    for (i = 0; i < (int)(sizeof(cmds) / sizeof(*cmds)); i++) {
	    ret = i2c_smbus_write_byte_data(grove->rgb_client, cmds[i].cmd, cmds[i].val);
	if (ret) {
		dev_err(&grove->rgb_client->dev, "failed to set the RGB backlight\n");
		goto fail;
	}
    }

    mutex_unlock(&grove_mutex);
    return size;

fail:
    mutex_unlock(&grove_mutex);
    return ret;
}

static long grove_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct grove_t *grove;
    struct device *dev;
    struct color_t *color;
    struct string_t *string;
    int ret = 0;
    int i = 0;

    grove = file->private_data;
    dev = &grove->lcd_client->dev;
    color = kmalloc(sizeof(struct color_t), GFP_KERNEL);
    string = kmalloc(sizeof(struct string_t), GFP_KERNEL);

    dev_info(dev, "%s", __func__);

    switch (cmd) {

	case GROVE_SET_COLOR:
	    dev_info(dev, " set color\n");
	    mutex_lock(&grove_mutex);
	    if (copy_from_user(color, (const void *)arg, sizeof(struct color_t))) {
		goto inval;
	    }
	    grove->color = *color;
	    dev_info(dev, "new color: r: 0x%x, g: 0x%x, b: 0x%x\n", color->red, color->green, color->blue);

	    struct i2c_cmd_t cmds[] = {
		{RED, color->red},
		{GREEN, color->green},
		{BLUE, color->blue},
	    };
	    for (i = 0; i < (int)(sizeof(cmds) / sizeof(*cmds)); i++) {
	       ret = i2c_smbus_write_byte_data(grove->rgb_client, cmds[i].cmd, cmds[i].val);
	       if (ret) {
		    dev_err(dev, "set new color failed\n");
		    goto i2c_fail;
	       }
	    }
	    break;

	case GROVE_GET_COLOR:
	    dev_info(dev, " get color\n");
	    mutex_lock(&grove_mutex);
	    if (copy_to_user((void *)arg, (const void *)&grove->color, sizeof(struct color_t))) {
		goto inval;
	    }
	    break;

	case GROVE_CLEAR_LCD:
	    dev_info(dev, " clear lcd\n");
	    mutex_lock(&grove_mutex);
	    memset(grove->line_one, ' ', LINE_SIZE + 1);
	    memset(grove->line_two, ' ', LINE_SIZE + 1);

	    ret = i2c_smbus_write_byte_data(grove->lcd_client, LCD_CMD, 0x01);
	    if (ret) {
		dev_err(dev, "clear display failed\n");
		goto i2c_fail;
	    }
	    break;

	case GROVE_WRITE_FIRST_LINE:
	    dev_info(dev, " write first line\n");
	    mutex_lock(&grove_mutex);
	    if (copy_from_user(string, (const void *)arg, sizeof(struct string_t))) {
		goto inval;
	    }
	    snprintf(grove->line_one, LINE_SIZE + 1, "%s", string->data);
	    dev_info(dev, "first line is %s,  %s\n", string->data, grove->line_one);

	    break;

	case GROVE_WRITE_SECOND_LINE:
	    dev_info(dev, " write second line\n");
	    mutex_lock(&grove_mutex);
	    if (copy_from_user(string, (const void *)arg, sizeof(struct string_t))) {
		goto inval;
	    }
	    snprintf(grove->line_two, LINE_SIZE + 1, "%s", string->data);
	    dev_info(dev, "second line is %s\n", string->data);

	    break;

	case GROVE_READ_LCD:
	    dev_info(dev, " read lcd\n");
	    mutex_lock(&grove_mutex);
	    snprintf(string->data, (LINE_SIZE*2) + 2, "%s\n%s\n", grove->line_one, grove->line_two);
	    string->size = sizeof(string->data);
	    if (copy_to_user((void *)arg, (const void *)string, sizeof(struct string_t))) {
		goto inval;
	    }
	    break;

	case GROVE_GET_LINE_SIZE:
	    dev_info(dev, " get line size\n");
	    mutex_lock(&grove_mutex);
	    if (put_user(LINE_SIZE, (unsigned long *)arg)) {
		goto inval;
	    }
	    break;

	default:
	    return -EINVAL;
    }

    mutex_unlock(&grove_mutex);
    return 0;

i2c_fail:
    mutex_unlock(&grove_mutex);
    return -EFAULT;

inval:
    mutex_unlock(&grove_mutex);
    return -EINVAL;
}

static int grove_open(struct inode *inode, struct file *file)
{
    struct grove_t *grove;

    grove = container_of(inode->i_cdev, struct grove_t, cdev);
    file->private_data = grove;

    return 0;
}

static int grove_release(struct inode *inode, struct file *file)
{
    file->private_data = NULL;
    return 0;
}

static int grove_init_lcd(struct grove_t *grove)
{
    int i = 0;
    int ret = 0;

    struct i2c_cmd_t cmds[] = {
	    {LCD_CMD, 0x01},
	    {LCD_CMD, 0x02},
	    {LCD_CMD, 0x0c},
	    {LCD_CMD, 0x28},
	    {TXT_CMD, 'T'},
	    {TXT_CMD, 'E'},
	    {TXT_CMD, 'S'},
	    {TXT_CMD, 'T'},
    };

    dev_info(&grove->lcd_client->dev, "%s\n", __func__);

    mutex_lock(&grove_mutex);
    for (i = 0; i < (int) (sizeof(cmds) / sizeof(*cmds)); i++) {
	// This log message delays the i2c writes that much, the test is set correctly
	dev_info(&grove->lcd_client->dev, "%s cmd 0x%x val 0x%x\n", __func__, cmds[i].cmd, cmds[i].val);
	    ret = i2c_smbus_write_byte_data(grove->lcd_client, cmds[i].cmd, cmds[i].val);
	if (ret) {
		dev_err(&grove->lcd_client->dev, "failed to initialize the LCD\n");
		goto fail;
	}
    }
fail:
    mutex_unlock(&grove_mutex);
    return ret;
}

static int grove_init_rgb(struct grove_t *grove)
{
    int i = 0;
    int ret = 0;

    grove->color.green = 0xff;

    struct i2c_cmd_t cmds[] = {
	    {0x00, 0x00},
	    {0x01, 0x00},
	    {0x08, 0xaa},
	    {RED, grove->color.red},
	    {GREEN, grove->color.green},
	    {BLUE, grove->color.blue},
    };

    dev_info(&grove->rgb_client->dev, "%s\n", __func__);

    mutex_lock(&grove_mutex);
    for (i = 0; i < (int)(sizeof(cmds) / sizeof(*cmds)); i++) {
	    ret = i2c_smbus_write_byte_data(grove->rgb_client, cmds[i].cmd, cmds[i].val);
	if (ret) {
		dev_err(&grove->rgb_client->dev, "failed to initialize the RGB backlight\n");
		goto fail;
	}
    }
fail:
    mutex_unlock(&grove_mutex);
    return ret;
}

static struct file_operations grove_fops = {
    .owner          = THIS_MODULE,
    .open           = grove_open,
    .release        = grove_release,
    .read           = grove_read,
    .write          = grove_write,
    .unlocked_ioctl = grove_ioctl,
};

// new API? we do not need i2c_device_id...
static int grove_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret = 0;
    struct grove_t *grove;
    struct device *dev = &client->dev;
    struct device *device;

    grove = kzalloc(sizeof(struct grove_t), GFP_KERNEL);
    if (IS_ERR(grove)) {
	    dev_err(dev, "failed to allocate a private memory area for the device\n");
	return -ENOMEM;
    }

    grove_class = class_create(THIS_MODULE, "grove");
    if (IS_ERR(grove_class)) {
	    dev_err(dev, "failed to create sysfs class\n");
	return -ENOMEM;
    }

    if (alloc_chrdev_region(&grove->devnum, 0, 1, "grove") < 0) {
	    dev_err(dev, "failed to allocate char dev region\n");
	goto free_class;
    }

    cdev_init(&grove->cdev, &grove_fops);
    grove->cdev.owner = THIS_MODULE;

    if (cdev_add(&grove->cdev, grove->devnum, 1)) {
	goto free_cdev;
    }

    device = device_create(grove_class, NULL, grove->devnum, "%s", "grove");
    if (IS_ERR(device)) {
	    dev_err(dev, "failed to create dev entry\n");
	goto free_cdev;
    }

    grove->lcd_client = client;
    i2c_set_clientdata(client, grove);
    ret = grove_init_lcd(grove);
    if (ret) {
	    dev_err(dev, "failed to init LCD, free resources\n");
	goto free_device;
    }

    // The Grove-LCD RGB backlight v4.0 is a composed device made from two individual I2C controllers.
    // Each controller has two addresses, but only one each controller are of interest.
    // To control such a device with only one driver, we use the new (v4.8) API
    // "i2c_new_secondary_device".
    // Unfortunately, the HiKey has some issues detecting all attached slave addresses, just the one
    // used to controll the RGB part is reliabily available. That's the reason why it is picked as
    // main device (matching).
    // Henc the following handling is not 100% as it should be. It is a fail-safe, variant where the
    // second I2C slave is optional.
    grove->rgb_client = i2c_new_secondary_device(grove->lcd_client, "grovergb", 0x62);
    if (grove->rgb_client == NULL) {
	return -ENODEV;
	    dev_info(dev, "can not fetch secondary I2C device\n");
    }
    // If we are sure the second device is / should be available, the else is not needed
    // Instead, uncomment the return above, make dev_info to dev_err
    i2c_set_clientdata(grove->rgb_client, grove);
    ret = grove_init_rgb(grove);
    if (ret) {
	    dev_err(dev, "failed to init RGB, free resources\n");
	goto free_device;
    }

    dev_info(dev, "%s finished\n", __func__);

    return 0;

free_device:
    device_destroy(grove_class, grove->devnum);
free_cdev:
    cdev_del(&grove->cdev);
    unregister_chrdev_region(grove->devnum, 1);
free_class:
    class_destroy(grove_class);
    kfree(grove);
    return -EIO;
}

static int grove_remove(struct i2c_client *client)
{
    int i = 0;
    int ret = 0;
    struct grove_t *grove = i2c_get_clientdata(client);
    struct i2c_client *lcd_client = grove->lcd_client;
    struct i2c_client *rgb_client = grove->rgb_client;

    mutex_lock(&grove_mutex);

    grove->color.red = 0x00;
    grove->color.green = 0x00;
    grove->color.blue = 0x00;

    struct i2c_cmd_t rgb_cmds[] = {
	{RED, grove->color.red},
	{GREEN, grove->color.green},
	{BLUE, grove->color.blue},
    };

    dev_info(&client->dev, "%s\n", __func__);

    for (i = 0; i < (int)(sizeof(rgb_cmds) / sizeof(*rgb_cmds)); i++) {
	    ret = i2c_smbus_write_byte_data(rgb_client, rgb_cmds[i].cmd, rgb_cmds[i].val);
	if (ret) {
		dev_err(&client->dev, "failed to deinitialize the RGB backlight\n");
		return ret;
	}
    }

    ret = i2c_smbus_write_byte_data(lcd_client, LCD_CMD, 0x01);

    i2c_unregister_device(rgb_client);
    device_destroy(grove_class, grove->devnum);
    cdev_del(&grove->cdev);
    unregister_chrdev_region(grove->devnum, 1);
    class_destroy(grove_class);
    kfree(grove);
    mutex_unlock(&grove_mutex);

    return 0;
}

static struct of_device_id grove_of_idtable[] = {
    {.compatible = "grove,lcd"},
    { }
};
MODULE_DEVICE_TABLE(of, grove_of_idtable);


static struct i2c_driver grove_driver = {
    .driver = {
	    .name = "grove",
	    /* .pm */
	    .of_match_table = grove_of_idtable
    },
    .probe = grove_probe,
    .remove = grove_remove,
};

module_i2c_driver(grove_driver);

MODULE_AUTHOR("Anna-Lena Marx <anna-lena.marx@inovex.de>");
MODULE_DESCRIPTION("Grove-LCD RGB backlight v4.0 Driver");
MODULE_LICENSE("GPL");
