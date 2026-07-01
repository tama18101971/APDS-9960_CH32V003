#ifndef APDS9960_REGS_H
#define APDS9960_REGS_H

#include <stdint.h>

#define APDS9960_I2C_ADDR       0x39

/* Core registers */
#define REG_ENABLE              0x80
#define REG_ATIME               0x81
#define REG_WTIME               0x83
#define REG_AILTL               0x84
#define REG_AILTH               0x85
#define REG_AIHTL               0x86
#define REG_AIHTH               0x87
#define REG_PILT                0x89
#define REG_PIHT                0x8B
#define REG_PERS                0x8C
#define REG_CONFIG1             0x8D
#define REG_PPULSE              0x8E
#define REG_CONTROL             0x8F
#define REG_CONFIG2             0x90
#define REG_ID                  0x92
#define REG_STATUS              0x93

/* Data registers */
#define REG_CDATAL              0x94
#define REG_RDATAL              0x96
#define REG_GDATAL              0x98
#define REG_BDATAL              0x9A
#define REG_PDATA               0x9C

/* Proximity offset */
#define REG_POFFSET_UR          0x9D
#define REG_POFFSET_DL          0x9E
#define REG_CONFIG3             0x9F

/* Gesture registers */
#define REG_GPENTH              0xA0
#define REG_GEXTH               0xA1
#define REG_GCONF1              0xA2
#define REG_GCONF2              0xA3
#define REG_GOFFSET_U           0xA4
#define REG_GOFFSET_D           0xA5
#define REG_GPULSE              0xA6
#define REG_GOFFSET_L           0xA7
#define REG_GOFFSET_R           0xA9
#define REG_GCONF3              0xAA
#define REG_GCONF4              0xAB

/* Gesture FIFO */
#define REG_GFLVL               0xAE
#define REG_GSTATUS             0xAF
#define REG_GFIFO_U             0xFC

/* ENABLE register bits (0x80) */
#define EN_PON                  0x01
#define EN_AEN                  0x02
#define EN_PEN                  0x04
#define EN_WEN                  0x08
#define EN_AIEN                 0x10
#define EN_PIEN                 0x20
#define EN_GEN                  0x40

/* STATUS register bits (0x93) */
#define ST_AVALID               0x01
#define ST_PVALID               0x02
#define ST_GINT                 0x04
#define ST_PGSAT                0x08
#define ST_CPSAT                0x80

/* GSTATUS register bits (0xAF) */
#define GST_GVALID              0x01
#define GST_GFOV                0x02

/* GCONF4 register bits (0xAB) */
#define GC4_GMODE               0x01
#define GC4_GIEN                0x02

/* CONTROL register (0x8F) layout:
 * [7:6] LEDDRIVE (prox/ALS LED): 0=100mA, 1=50mA, 2=25mA, 3=12.5mA
 * [5:4] reserved
 * [3:2] PGAIN: 0=1x, 1=2x, 2=4x, 3=8x
 * [1:0] AGAIN: 0=1x, 1=4x, 2=16x, 3=64x */

#define CTRL_LED_SHIFT          6
#define CTRL_LED_MASK           0xC0
#define CTRL_PGAIN_SHIFT        2
#define CTRL_PGAIN_MASK         0x0C
#define CTRL_AGAIN_MASK         0x03

/* GCONF2 register (0xA3) layout:
 * [6:5] GGLDRIVE: gesture LED drive
 * [4:3] GGAIN: gesture gain
 * [2:0] GWTIME: gesture wait time */

#define GCONF2_GLDRIVE_SHIFT    5
#define GCONF2_GLDRIVE_MASK     0x60
#define GCONF2_GGAIN_SHIFT      3
#define GCONF2_GGAIN_MASK       0x18
#define GCONF2_GWTIME_MASK      0x07

/* CONFIG2 register (0x90) */
#define CFG2_LED_BOOST_SHIFT    4
#define CFG2_LED_BOOST_MASK     0x30

/* Known device IDs */
#define APDS9960_ID_1           0xAB
#define APDS9960_ID_2           0x9C

/* LED Boost values */
#define LED_BOOST_100           0
#define LED_BOOST_150           1
#define LED_BOOST_200           2
#define LED_BOOST_300           3

/* Proximity gain */
#define PGAIN_1X                0
#define PGAIN_2X                1
#define PGAIN_4X                2
#define PGAIN_8X                3

/* ALS gain */
#define AGAIN_1X                0
#define AGAIN_4X                1
#define AGAIN_16X               2
#define AGAIN_64X               3

/* LED drive */
#define LED_DRIVE_100MA         0
#define LED_DRIVE_50MA          1
#define LED_DRIVE_25MA          2
#define LED_DRIVE_12_5MA        3

/* Gesture gain */
#define GGAIN_1X                0
#define GGAIN_2X                1
#define GGAIN_4X                2
#define GGAIN_8X                3

/* Gesture LED drive */
#define GLED_DRIVE_100MA        0
#define GLED_DRIVE_50MA         1
#define GLED_DRIVE_25MA         2
#define GLED_DRIVE_12_5MA       3

/* Gesture wait time values */
#define GWTIME_0MS              0
#define GWTIME_2_8MS            1
#define GWTIME_5_6MS            2
#define GWTIME_8_4MS            3
#define GWTIME_14_0MS           4
#define GWTIME_22_4MS           5
#define GWTIME_30_8MS           6
#define GWTIME_39_2MS           7

#endif /* APDS9960_REGS_H */
