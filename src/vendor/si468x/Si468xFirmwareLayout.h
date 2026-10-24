#ifndef SI468X_FIRMWARE_LAYOUT_H
#define SI468X_FIRMWARE_LAYOUT_H

/*
 * Stand-alone NVSPI layout constants. No dependency on Si468x.h.
 *
 * These addresses follow the later SDK-style 560 KiB image spacing found with
 * the supplied modern firmware set. The older AN649 Rev.1.9 example uses
 * 512 KiB image slots (0x006000 / 0x086000), which are too small for some
 * firmware images in this package.
 */
#include <stdint.h>

namespace si468x_firmware {
static const uint32_t NVSPI_CUSTOMER_AREA       = 0x00000000UL;
static const uint32_t NVSPI_CUSTOMER_AREA_SIZE  = 0x00002000UL; // 8 KiB
static const uint32_t NVSPI_FULL_PATCH_PRIMARY  = 0x00002000UL;
static const uint32_t NVSPI_FULL_PATCH_BACKUP   = 0x00004000UL;
static const uint32_t NVSPI_PATCH_SLOT_SIZE     = 0x00002000UL; // 8 KiB
static const uint32_t NVSPI_FMHD_IMAGE          = 0x00006000UL;
static const uint32_t NVSPI_DAB_IMAGE           = 0x00092000UL;
static const uint32_t NVSPI_AMHD_IMAGE          = 0x0011E000UL;
static const uint32_t NVSPI_IMAGE_SLOT_SIZE     = 0x0008C000UL; // 560 KiB
static const uint32_t NVSPI_MIN_FLASH_SIZE      = 0x00200000UL; // 2 MiB / 16 Mbit
}

#endif
