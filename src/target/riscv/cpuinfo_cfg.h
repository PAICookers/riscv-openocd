#ifndef __CPUINFO_CFG_H__
#define __CPUINFO_CFG_H__

/* NOTE: You can customize your own printf function here */
#include <stdio.h>
extern void cif_printf(const char *format, ...);
#define CIF_PRINTF(fmt, ...) cif_printf(fmt, ##__VA_ARGS__)

#endif

