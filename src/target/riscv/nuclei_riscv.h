/* SPDX-License-Identifier: GPL-2.0-or-later */

/***************************************************************************
 *   Copyright (C) 2023 by Nuclei wangyanwen                               *
 *   wangyanwen@nucleisys.com                                              *
 ***************************************************************************/

#ifndef TARGET__RISCV__NUCLEI_RISCV_H
#define TARGET__RISCV__NUCLEI_RISCV_H

#include <stdint.h>
#include "cpuinfo.h"

extern const struct command_registration nuclei_command_group_handlers[];
extern uint64_t nuclei_get_dmcustom(struct target *target, uint32_t type, uint32_t hart_id, uint32_t index);
extern void nuclei_get_cpuinfo(struct target *target, CPU_CSR_Group *csrs);

#endif /* TARGET__RISCV__NUCLEI_RISCV_H */
