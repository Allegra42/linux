#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#define RED 0x04
#define GREEN 0x03
#define BLUE 0x02

static struct i2c_cmd {
    uint8_t cmd;
    uint8_t val;
};

static struct color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

static struct grove_device {
   struct i2c_client *rgb_client; 
   struct i2c_client *lcd_client;
   struct color color;
};

static int grove_init_lcd(struct grove_device *grove) {
    // TODO write initialization
    // check if the client exists!
    return 0;
}

static int grove_init_rgb(struct grove_device *grove) {
    int i = 0;
    int ret = 0;

    grove->color.green = 0xff;

    struct i2c_cmd cmds[] = {
        {0x00, 0x00},
        {0x01, 0x00},
        {0x08, 0xaa},
        {RED, grove->color.red},
        {GREEN, grove->color.green},
        {BLUE, grove->color.blue},
    };

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
    struct grove_device *grove_device;
    struct device *dev = &client->dev;

    grove_device = kzalloc(sizeof(struct grove_device), GFP_KERNEL);
    if (grove_device == NULL) {
        dev_err(dev, "failed to allocate a private memory area for the device\n");
        return -ENOMEM;
    }

    grove_device->rgb_client = client;
    i2c_set_clientdata(client, grove_device);
    ret = grove_init_rgb(grove_device);
    if (ret) {
        dev_err(dev, "failed to init RGB, free resources\n");
        goto free_grove;
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
    grove_device->lcd_client = i2c_new_secondary_device(grove_device->rgb_client, "lcd", 0x3e);
    if (!grove_device->lcd_client) {
        // return -EINVAL;
        dev_info(dev, "can not fetch secondary I2C device\n");
    } 
    // If we are sure the second device is / should be available, the else is not needed
    // Instead, uncomment the return above, make dev_info to dev_err
    else {
        i2c_set_clientdata(grove_device->lcd_client, grove_device);
        ret = grove_init_lcd(grove_device);
        if (ret) {
            dev_err(dev, "failed to init LCD, free resources\n");
            goto free_grove;
        }
    }

    dev_info(dev, "%s finished\n", __func__);

    return 0;

unregister_lcd:
    /* if(grove_device->lcd_client) {
     *     i2c_unregister_device(grove_device->lcd_client);
     * }
     * i2c_unregister_device(grove_device->rgb_client); */
free_grove:
    kfree(grove_device);
    return ret;
}

static int grove_remove(struct i2c_client *client) {
    int i = 0;
    int ret = 0;
    struct grove_device *grove_device = i2c_get_clientdata(client);
    
    grove_device->color.red = 0x00;
    grove_device->color.green = 0x00;
    grove_device->color.blue = 0x00;

    struct i2c_cmd cmds[] = {
        {RED, grove_device->color.red},
        {GREEN, grove_device->color.green},
        {BLUE, grove_device->color.blue},
    };

    dev_info(&client->dev, "%s\n", __func__);

    for(i = 0; i < (int)(sizeof(cmds) / sizeof(*cmds)); i++) {
        ret = i2c_smbus_write_byte_data(grove_device->rgb_client, cmds[i].cmd, cmds[i].val);
        if (ret) {
            dev_err(&client->dev, "failed to initialize the RGB backlight\n");
            return ret;
        }
    }


    /* if (grove_device->lcd_client) {
     *     i2c_unregister_device(grove_device->lcd_client);
     * }
     * i2c_unregister_device(grove_device->rgb_client); */

    kfree(grove_device);

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
