#ifndef SI468X_FIRMWARE_CATALOG_H
#define SI468X_FIRMWARE_CATALOG_H

/* Metadata only; including this file does NOT pull firmware byte arrays into the build. */
#include <stdint.h>
#include <stddef.h>

namespace si468x_firmware {
struct ImageMetadata { const char* name; const char* version; const char* kind; uint32_t size; uint32_t crc32; const char* sha256; };

static const ImageMetadata ROM0_MINI_003 = {"rom00_patch_mini.003.bin", "ROM0.MINI.003", "Bootloader MiniPatch", 940UL, 0x8944B5AEUL, "84ae1a5b82f984b671e785f4ccb281e414998ce02c9fc897ab9484ef500ee0eb"};
static const ImageMetadata ROM0_FULL_016 = {"rom00_patch.016.bin", "ROM0.016", "Bootloader FullPatch", 5796UL, 0xA93227B5UL, "15c5196132b921755ed5c3b00b875494c73e39a2013771a594957cebe04221f2"};
static const ImageMetadata FMHD_5_3_3 = {"fmhd_radio_5_3_3.bin", "5.3.3", "FM/FMHD firmware", 530856UL, 0xA174D4ABUL, "807b93a5693f8ac63698ecbac51f72a3f17aaa930e74f08fdaedf226cef22be9"};
static const ImageMetadata DAB_6_0_9 = {"dab_radio_6_0_9.bin", "6.0.9", "DAB/DAB+ firmware", 499516UL, 0x4BB3271DUL, "48692a0c3c2430c5c49cfb35a3d06c1853246b752293715f38c1027281c98fa6"};
static const ImageMetadata AMHD_3_0_6 = {"amhd_radio_3_0_6.bin", "3.0.6", "AM/AMHD firmware", 533164UL, 0x2168E5C7UL, "9906d7cc5556f0eddb74faa1abb039fb07056af7e1d5f70ad791925addfdf2a1"};

} // namespace si468x_firmware

#endif
