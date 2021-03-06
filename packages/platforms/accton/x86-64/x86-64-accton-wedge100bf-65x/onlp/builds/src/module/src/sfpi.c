/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *           Copyright 2016 Accton Technology Corporation.
 *
 * Licensed under the Eclipse Public License, Version 1.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *        http://www.eclipse.org/legal/epl-v10.html
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the
 * License.
 *
 * </bsn.cl>
 ************************************************************
 *
 *
 *
 ***********************************************************/
#include <onlplib/i2c.h>
#include <onlp/platformi/sfpi.h>
#include <onlplib/file.h>
#include "platform_lib.h"

#include "x86_64_accton_wedge100bf_65x_log.h"

#define BIT(i)          (1 << (i))
#define NUM_OF_SFP_PORT 64
static const int sfp_bus_index[] = {
  4,  3,  6,  5,  8,  7,  10, 9,
 12,  11, 14, 13, 16, 15, 18, 17,
 20, 19, 22, 21, 24, 23, 26, 25,
 28, 27, 30, 29, 32, 31, 34, 33,
 44, 43, 46, 45, 48, 47, 50, 49,
 52, 51, 54, 53, 56, 55, 58, 57,
 60, 59, 62, 61, 64, 63, 66, 65,
 68, 67, 70, 69, 72, 71, 74, 73, 
};

/************************************************************
 *
 * SFPI Entry Points
 *
 ***********************************************************/
int
onlp_sfpi_init(void)
{
    /* Called at initialization time */    
    return ONLP_STATUS_OK;
}

int
onlp_sfpi_bitmap_get(onlp_sfp_bitmap_t* bmap)
{
    /*
     * Ports {0, 32}
     */
    int p;
    AIM_BITMAP_CLR_ALL(bmap);

    for(p = 0; p < NUM_OF_SFP_PORT; p++) {
        AIM_BITMAP_SET(bmap, p);
    }

    return ONLP_STATUS_OK;
}

static uint8_t
onlp_sfpi_reg_val_to_port_sequence(uint8_t value, int revert)
{
    int i;
    uint8_t ret = 0;

    for (i = 0; i < 8; i++) {
        if (i % 2) {
            ret |= (value & BIT(i)) >> 1;
        }
        else {
            ret |= (value & BIT(i)) << 1;
        }
    }

    return revert ? ~ret : ret;
}

int
onlp_sfpi_is_present(int port)
{
    /*
     * Return 1 if present.
     * Return 0 if not present.
     * Return < 0 if error.
     */
    int present;
    int bus;  
    int addr = ((port < 16) || (port >= 32 && port <= 47)) ? 0x22 : 0x23; /* pca9535 slave address */
    int offset;

    if(port < 16) {
        bus = 37;
    }else if(port >=16 && port < 32){
        bus = 38;
    }else if(port >=32 && port < 48){
        bus = 77;
    }else if(port >=48 && port <= 63){
        bus = 78;
    }
        
    if ((port < 8) || (port >= 16 && port <= 23) || (port >= 32 && port <= 39) \
            || (port >=48 && port <=55)) {
        offset = 0;
    }
    else {
        offset = 1;
    }

    present = onlp_i2c_readb(bus, addr, offset, ONLP_I2C_F_FORCE);
    if (present < 0) {
        return ONLP_STATUS_E_INTERNAL;
    }

    present = onlp_sfpi_reg_val_to_port_sequence(present, 0);
    return !(present & BIT(port % 8));
}

int
onlp_sfpi_presence_bitmap_get(onlp_sfp_bitmap_t* dst)
{
    int i;
    uint8_t bytes[8] = {0};

    for (i = 0; i < AIM_ARRAYSIZE(bytes); i++) {
        int bus;  
        int addr = ((i < 2)|| (i > 3 && i < 6)) ? 0x22 : 0x23; /* pca9535 slave address */
        int offset = (i % 2);
        /* Adding bus number for upper board QSFP ports */
        switch(i)
        {
            case 0: 
            case 1: 
                    bus = 37;
                    break;
            case 2:
            case 3: 
                    bus = 38;
                    break;
            case 4:
            case 5: 
                    bus = 77;
                    break;
            case 6:
            case 7: 
                    bus = 78;
                    break;
            default:
                    break;
        }

        bytes[i] = onlp_i2c_readb(bus, addr, offset, ONLP_I2C_F_FORCE);
        if (bytes[i] < 0) {
            return ONLP_STATUS_E_INTERNAL;
        }

        bytes[i] = onlp_sfpi_reg_val_to_port_sequence(bytes[i], 1);
    }

    /* Convert to 64 bit integer in port order */
    i = 0;
    uint64_t presence_all = 0;
    for(i = AIM_ARRAYSIZE(bytes)-1; i >= 0; i--) {
        presence_all <<= 8;
        presence_all |= bytes[i];
    }

    /* Populate bitmap */
    for(i = 0; presence_all; i++) {
        AIM_BITMAP_MOD(dst, i, (presence_all & 1));
        presence_all >>= 1;
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_rx_los_bitmap_get(onlp_sfp_bitmap_t* dst)
{
    return ONLP_STATUS_OK;
}

static int
sfpi_eeprom_read(int port, uint8_t devaddr, uint8_t data[256])
{
    int i;

    /*
     * Read the SFP eeprom into data[]
     *
     * Return MISSING if SFP is missing.
     * Return OK if eeprom is read
     */
    memset(data, 0, 256);

    for (i = 0; i < 128; i++) {
        int bus = sfp_bus_index[port];
        int val = onlp_i2c_readw(bus, devaddr, i*2, ONLP_I2C_F_FORCE);

        if (val < 0) {
            return ONLP_STATUS_E_INTERNAL;
        }

        data[i*2]   = val & 0xff;
        data[(i*2)+1] = (val >> 8) & 0xff;
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_eeprom_read(int port, uint8_t data[256])
{
    return sfpi_eeprom_read(port, 0x50, data);
}

int
onlp_sfpi_dom_read(int port, uint8_t data[256])
{
    return sfpi_eeprom_read(port, 0x51, data);
}

int
onlp_sfpi_control_set(int port, onlp_sfp_control_t control, int value)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

int
onlp_sfpi_control_get(int port, onlp_sfp_control_t control, int* value)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

int
onlp_sfpi_denit(void)
{
    return ONLP_STATUS_OK;
}

