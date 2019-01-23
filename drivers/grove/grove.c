#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#define RED 0x04
#define GREEN 0x03
#define BLUE 0x02

struct i2c_cmd_t {
    uint8_t cmd;
    uint8_t val;
};

struct color_t {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct grove_t {
   dev_t devnum;
   struct i2c_client *rgb_client; 
   struct i2c_client *lcd_client;
   struct color_t color;
};

static struct cdev *grove_cdev = NULL;
static struct class *grove_class = NULL;


static int grove_init_lcd(struct grove_t *grove) {
    // TODO write initialization
    // check if the client exists!
    return 0;
}

static int grove_init_rgb(struct grove_t *grove) {
    int i = 0;
    int ret = 0;

    struct i2c_cmd_t cmds[] = {
        {0x00, 0x00},
        {0x01, 0x00},
        {0x08, 0xaa},
        {RED, grove->color.red},
        {GREEN, grove->color.green},
        {BLUE, grove->color.blue},
    };

    grove->color.green = 0xff;

    dev_info(&grove->rgb_client->dev, "%s\n", __func__);

    for(i = 0; i < (int)(sizeof(cmds) / sizeof(*cmds)); i++) {
        ret = i2c_smbus_write_byte_data(grove->rgb_client, cmds[i].cmd, cmds[i].val);
        if (ret) {
            dev_err(&grove->rgb_client->dev, "failed to initialize the RGB backlight\n");
            return ret;
        }
    }
    return 0;
}

static int grove_probe(struct i2c_client *client, const struct i2c_device_id *id) {
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

    grove_cdev = cdev_alloc();
    if (IS_ERR(grove_cdev)) {
        dev_err(dev, "failed to allocate cdev\n");
        goto free_devnum;
    }

    device = device_create(grove_class, NULL, grove->devnum, "%s", "grove");
    if (IS_ERR(device)) {
        dev_err(dev, "failed to create dev entry\n");
        goto free_cdev;
    }

    grove->rgb_client = client;
    i2c_set_clientdata(client, grove);
    ret = grove_init_rgb(grove);
    if (ret) {
        dev_err(dev, "failed to init RGB, free resources\n");
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
    grove->lcd_client = i2c_new_secondary_device(grove->rgb_client, "lcd", 0x3e);
    if (IS_ERR(grove->lcd_client)) {
        // return -EINVAL;
        dev_info(dev, "can not fetch secondary I2C device\n");
    } 
    // If we are sure the second device is / should be available, the else is not needed
    // Instead, uncomment the return above, make dev_info to dev_err
    else {
        i2c_set_clientdata(grove->lcd_client, grove);
        ret = grove_init_lcd(grove);
        if (ret) {
            dev_err(dev, "failed to init LCD, free resources\n");
            goto free_device;
        }
    }

    dev_info(dev, "%s finished\n", __func__);

    return 0;

free_device:
    device_destroy(grove_class, grove->devnum);
free_cdev:
    cdev_del(grove_cdev);
free_devnum:
    unregister_chrdev_region(grove->devnum, 1);
free_class:
    class_destroy(grove_class);
    kfree(grove);
    return -EIO;
}

static int grove_remove(struct i2c_client *client) {
    int i = 0;
    int ret = 0;
    struct grove_t *grove = i2c_get_clientdata(client);
    
    struct i2c_cmd_t cmds[] = {
        {RED, grove->color.red},
        {GREEN, grove->color.green},
        {BLUE, grove->color.blue},
    };

    grove->color.red = 0x00;
    grove->color.green = 0x00;
    grove->color.blue = 0x00;

    dev_info(&client->dev, "%s\n", __func__);

    for(i = 0; i < (int)(sizeof(cmds) / sizeof(*cmds)); i++) {
        ret = i2c_smbus_write_byte_data(grove->rgb_client, cmds[i].cmd, cmds[i].val);
        if (ret) {
            dev_err(&client->dev, "failed to initialize the RGB backlight\n");
            return ret;
        }
    }


    /* if (grove->lcd_client) {
     *     i2c_unregister_device(grove->lcd_client);
     * }
     * i2c_unregister_device(grove->rgb_client); */

    kfree(grove);

    return 0;
}

static struct of_device_id grove_of_idtable[] = {
    {.compatible = "grove,rgb"},
    { }
};
MODULE_DEVICE_TABLE(of, grove_of_idtable);

/* static struct i2c_device_id grove_i2c_idtable[] = {
 *     {"rgb", 0},
 *     { }
 * };
 * MODULE_DEVICE_TABLE(i2c, grove_i2c_idtable); */

static struct i2c_driver grove_driver = {
    .driver = {
        .name = "grove",
        /* .pm */
        .of_match_table = grove_of_idtable
    },
    /* .id_table = grove_i2c_idtable, */
    .probe = grove_probe,
    .remove = grove_remove,
};

module_i2c_driver(grove_driver);

MODULE_AUTHOR("Anna-Lena Marx <anna-lena.marx@inovex.de>");
MODULE_DESCRIPTION("Grove-LCD RGB backlight v4.0 Driver");
MODULE_LICENSE("GPL");
