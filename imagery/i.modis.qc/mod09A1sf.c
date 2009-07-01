/* Internal Cloud Algorithm Flag unsigned int bits[10]
 * 0 -> class 0: Cloud 
 * 1 -> class 1: No cloud
 */  

#include <grass/raster.h>

CELL mod09A1sf(CELL pixel) 
{
    CELL qctemp;

    pixel >>= 10;
    qctemp = pixel & 0x01;

    return qctemp;
}


