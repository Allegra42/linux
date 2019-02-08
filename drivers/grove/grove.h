#ifndef __GROVE_H__
#define __GROVE_H__

#define RED 0x04
#define GREEN 0x03
#define BLUE 0x02

#define LCD_CMD 0x80
#define TXT_CMD 0x40

#define LINE_SIZE 16

struct i2c_cmd_t {
    uint8_t cmd;
    uint8_t val;
};

struct color_t {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct string_t {
    uint8_t position;
    char data[(LINE_SIZE*2) + 2];
};

#endif /* __GROVE_H__ */
