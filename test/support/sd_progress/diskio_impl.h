#pragma once
#include <cstdint>
using BYTE = uint8_t;
using DWORD = uint32_t;
using UINT = unsigned;
using DSTATUS = uint8_t;
enum DRESULT { RES_OK, RES_ERROR, RES_WRPRT, RES_NOTRDY, RES_PARERR };
struct ff_diskio_impl_t {
    DSTATUS (*init)(BYTE);
    DSTATUS (*status)(BYTE);
    DRESULT (*read)(BYTE, BYTE*, DWORD, UINT);
    DRESULT (*write)(BYTE, const BYTE*, DWORD, UINT);
    DRESULT (*ioctl)(BYTE, BYTE, void*);
};
