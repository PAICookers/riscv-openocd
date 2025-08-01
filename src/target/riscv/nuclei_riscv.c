// SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 *   Copyright (C) 2023 by Nuclei wangyanwen                               *
 *   wangyanwen@nucleisys.com                                              *
 ***************************************************************************/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "target/target.h"
#include "target/target_type.h"
#include "helper/log.h"
#include "helper/fileio.h"
#include "helper/time_support.h"
#include "riscv.h"
#include "rtos/rtos.h"
#include "debug_defines.h"
#include <helper/bits.h>

#define KB                        (1024)
#define MB                        (KB * 1024)
#define GB                        (MB * 1024)
#define EXTENSION_NUM             (26)
#define POWER_FOR_TWO(n)          (1 << (n))
#define LINESZ(n)                 ((n) > 0 ? POWER_FOR_TWO((n) - 1) : 0)
#define EXTRACT_FIELD(val, which) (((val) & (which)) / ((which) & ~((which) - 1)))

#define show_safety_mechanism(safetyMode) do { \
	switch (safetyMode) { \
		case 0b00: \
			command_print_sameline(CMD, " No-Safety-Mechanism"); \
			break; \
		case 0b01: \
			command_print_sameline(CMD, " Lockstep"); \
			break; \
		case 0b10: \
			command_print_sameline(CMD, " Lockstep+SplitMode"); \
			break; \
		case 0b11: \
			command_print_sameline(CMD, " ASIL-B"); \
			break; \
	} \
} while (0)


#define show_vpu_degree(degree) do { \
	switch (degree) { \
		case 0b00: \
			command_print_sameline(CMD, " DLEN=VLEN/2"); \
			break; \
		case 0b01: \
			command_print_sameline(CMD, " DLEN=VLEN"); \
			break; \
	} \
} while (0)

#define print_size(bytes) do { \
	if ((bytes) / GB) { \
		command_print_sameline(CMD, " %d GB", (int)((bytes) / GB)); \
	} else if ((bytes) / MB) { \
		command_print_sameline(CMD, " %d MB", (int)((bytes) / MB)); \
	} else if ((bytes) / KB) { \
		command_print_sameline(CMD, " %d KB", (int)((bytes) / KB)); \
	} else { \
		command_print_sameline(CMD, " %d Byte", (int)(bytes)); \
	} \
} while (0)

#define show_cache_info(set, way, lsize, ecc) do { \
	print_size((set) * (way) * (lsize)); \
	command_print_sameline(CMD, "(set=%d,", (int)(set)); \
	command_print_sameline(CMD, "way=%d,", (int)(way)); \
	command_print_sameline(CMD, "lsize=%d,", (int)(lsize)); \
	command_print(CMD, "ecc=%d)", !!(ecc)); \
} while (0)

COMMAND_HANDLER(handle_nuclei_cpuinfo)
{
	riscv_reg_t csr_mcfg = 0;
	riscv_reg_t csr_micfg = 0;
	riscv_reg_t csr_mdcfg = 0;
	riscv_reg_t iregion_base = 0;
	riscv_reg_t csr_marchid = 0;
	riscv_reg_t csr_mimpid = 0;
	riscv_reg_t csr_misa = 0;
	riscv_reg_t csr_mirgb = 0;
	riscv_reg_t csr_mfiocfg = 0;
	riscv_reg_t csr_mppicfg = 0;
	riscv_reg_t csr_mtlbcfg = 0;

	struct target *target = get_current_target(CMD_CTX);

	command_print(CMD, "-----Nuclei RISC-V CPU Configuration Information-----");

	/* ID and version */
	if (riscv_reg_get(target, &csr_marchid, CSR_MARCHID + GDB_REGNO_CSR0) == ERROR_OK)
		command_print(CMD, "         MARCHID: %lx", csr_marchid);
	if (riscv_reg_get(target, &csr_mimpid, CSR_MIMPID + GDB_REGNO_CSR0) == ERROR_OK)
		command_print(CMD, "          MIMPID: %lx", csr_mimpid);

	/* ISA */
	if (riscv_reg_get(target, &csr_misa, CSR_MISA + GDB_REGNO_CSR0) == ERROR_OK) {
		command_print_sameline(CMD, "             ISA:");
		if (riscv_xlen(target) == 32)
			command_print_sameline(CMD, " RV32");
		else
			command_print_sameline(CMD, " RV64");
		for (int i = 0; i < EXTENSION_NUM; i++) {
			if (csr_misa & BIT(i)) {
				if ('X' == ('A' + i))
					command_print_sameline(CMD, " NICE");
				else
					command_print_sameline(CMD, " %c", 'A' + i);
			}
		}
		if (riscv_reg_get(target, &csr_mcfg, CSR_MCFG_INFO + GDB_REGNO_CSR0) == ERROR_OK) {
			if (csr_mcfg & BIT(12))
				command_print_sameline(CMD, " Xxldspn1x");
			if (csr_mcfg & BIT(13))
				command_print_sameline(CMD, " Xxldspn2x");
			if (csr_mcfg & BIT(14))
				command_print_sameline(CMD, " Xxldspn3x");
			if (csr_mcfg & BIT(15))
				command_print_sameline(CMD, " Zc Xxlcz");
			if (csr_mcfg & BIT(19))
				command_print_sameline(CMD, " Smwg");
		}
		command_print(CMD, " ");
	}

	/* Support */
	if (riscv_reg_get(target, &csr_mcfg, CSR_MCFG_INFO + GDB_REGNO_CSR0) == ERROR_OK) {
		command_print_sameline(CMD, "            MCFG:");
		if (csr_mcfg & BIT(0))
			command_print_sameline(CMD, " TEE");
		if (csr_mcfg & BIT(1))
			command_print_sameline(CMD, " ECC");
		if (csr_mcfg & BIT(2))
			command_print_sameline(CMD, " ECLIC");
		if (csr_mcfg & BIT(3))
			command_print_sameline(CMD, " PLIC");
		if (csr_mcfg & BIT(4))
			command_print_sameline(CMD, " FIO");
		if (csr_mcfg & BIT(5))
			command_print_sameline(CMD, " PPI");
		if (csr_mcfg & BIT(6))
			command_print_sameline(CMD, " NICE");
		if (csr_mcfg & BIT(7))
			command_print_sameline(CMD, " ILM");
		if (csr_mcfg & BIT(8))
			command_print_sameline(CMD, " DLM");
		if (csr_mcfg & BIT(9))
			command_print_sameline(CMD, " ICACHE");
		if (csr_mcfg & BIT(10))
			command_print_sameline(CMD, " DCACHE");
		if (csr_mcfg & BIT(11))
			command_print_sameline(CMD, " SMP");
		if (csr_mcfg & BIT(12))
			command_print_sameline(CMD, " DSP_N1");
		if (csr_mcfg & BIT(13))
			command_print_sameline(CMD, " DSP_N2");
		if (csr_mcfg & BIT(14))
			command_print_sameline(CMD, " DSP_N3");
		if (csr_mcfg & BIT(15))
			command_print_sameline(CMD, " ZC_XLCZ_EXT");
		if (csr_mcfg & BIT(16))
			command_print_sameline(CMD, " IREGION");
		if (csr_mcfg & BIT(19))
			command_print_sameline(CMD, " SEC_MODE");
		if (csr_mcfg & BIT(20))
			command_print_sameline(CMD, " ETRACE");
		if (csr_mcfg & BIT(23))
			command_print_sameline(CMD, " VNICE");
		show_safety_mechanism(EXTRACT_FIELD(csr_mcfg, 0x3 << 21));
		if (csr_misa & BIT(21))
			show_vpu_degree(EXTRACT_FIELD(csr_mcfg, 0x3 << 17));
		command_print(CMD, " ");
	}

	/* ILM */
	if (csr_mcfg & BIT(7)) {
		if (riscv_reg_get(target, &csr_micfg, CSR_MICFG_INFO + GDB_REGNO_CSR0) == ERROR_OK) {
			command_print_sameline(CMD, "             ILM:");
			print_size(POWER_FOR_TWO(EXTRACT_FIELD(csr_micfg, 0x1F << 16) - 1) * 256);
			if (csr_micfg & BIT(21))
				command_print_sameline(CMD, " execute-only");
			if (csr_micfg & BIT(22))
				command_print_sameline(CMD, " has-ecc");
			command_print(CMD, " ");
		}
	}

	/* DLM */
	if (csr_mcfg & BIT(8)) {
		if (riscv_reg_get(target, &csr_mdcfg, CSR_MDCFG_INFO + GDB_REGNO_CSR0) == ERROR_OK) {
			command_print_sameline(CMD, "             DLM:");
			print_size(POWER_FOR_TWO(EXTRACT_FIELD(csr_mdcfg, 0x1F << 16) - 1) * 256);
			if (csr_mdcfg & BIT(21))
				command_print_sameline(CMD, " has-ecc");
			command_print(CMD, " ");
		}
	}

	/* ICACHE */
	if (csr_mcfg & BIT(9)) {
		if (riscv_reg_get(target, &csr_micfg, CSR_MICFG_INFO + GDB_REGNO_CSR0) == ERROR_OK) {
			command_print_sameline(CMD, "          ICACHE:");
			show_cache_info(POWER_FOR_TWO(EXTRACT_FIELD(csr_micfg, 0xF) + 3),
							EXTRACT_FIELD(csr_micfg, 0x7 << 4) + 1,
							POWER_FOR_TWO(EXTRACT_FIELD(csr_micfg, 0x7 << 7) + 2),
							csr_mcfg & BIT(1));
		}
	}

	/* DCACHE */
	if (csr_mcfg & BIT(10)) {
		if (riscv_reg_get(target, &csr_mdcfg, CSR_MDCFG_INFO + GDB_REGNO_CSR0) == ERROR_OK) {
			command_print_sameline(CMD, "          DCACHE:");
			show_cache_info(POWER_FOR_TWO(EXTRACT_FIELD(csr_mdcfg, 0xF) + 3),
							EXTRACT_FIELD(csr_mdcfg, 0x7 << 4) + 1,
							POWER_FOR_TWO(EXTRACT_FIELD(csr_mdcfg, 0x7 << 7) + 2),
							csr_mcfg & BIT(1));
		}
	}

	/* TLB only present with MMU, when PLIC present MMU will present */
	if (csr_mcfg & BIT(3)) {
		if (riscv_reg_get(target, &csr_mtlbcfg, CSR_MTLBCFG_INFO + GDB_REGNO_CSR0) == ERROR_OK) {
			command_print_sameline(CMD, "             TLB:");
			command_print(CMD, " MainTLB(set=%lu way=%lu entry=%lu ecc=%lu) ITLB(entry=%lu) DTLB(entry=%lu)",
						POWER_FOR_TWO(EXTRACT_FIELD(csr_mtlbcfg, 0xF) + 3),
						EXTRACT_FIELD(csr_mtlbcfg, 0x7 << 4) + 1,
						LINESZ(EXTRACT_FIELD(csr_mtlbcfg, 0x7 << 7)),
						EXTRACT_FIELD(csr_mtlbcfg, 0x1 << 10),
						LINESZ(EXTRACT_FIELD(csr_mtlbcfg, 0x7 << 16)),
						LINESZ(EXTRACT_FIELD(csr_mtlbcfg, 0x7 << 19)));
		}
	}

	/* IREGION */
	if (csr_mcfg & BIT(16)) {
		if (riscv_reg_get(target, &csr_mirgb, CSR_MIRGB_INFO + GDB_REGNO_CSR0) == ERROR_OK) {
			command_print_sameline(CMD, "         IREGION:");
			iregion_base = csr_mirgb & (~0x3FF);
			command_print_sameline(CMD, " %#lx", iregion_base);
			print_size(POWER_FOR_TWO(EXTRACT_FIELD(csr_mirgb, 0x1F << 1) - 1) * KB);
			command_print(CMD, " ");
			command_print(CMD, "                  Unit        Size        Address");
			command_print(CMD, "                  INFO        64KB        %#lx", iregion_base);
			command_print(CMD, "                  DEBUG       64KB        %#lx", iregion_base + 0x10000);
			if (csr_mcfg & BIT(2))
				command_print(CMD, "                  ECLIC       64KB        %#lx", iregion_base + 0x20000);
			command_print(CMD, "                  TIMER       64KB        %#lx", iregion_base + 0x30000);
			if (csr_mcfg & BIT(11))
				command_print(CMD, "                  SMP&CC      64KB        %#lx", iregion_base + 0x40000);
			riscv_reg_t smp_cfg = 0;
			target_read_memory(target, iregion_base + 0x40004, 4, 1, (uint8_t *)&smp_cfg);
			if ((csr_mcfg & BIT(2)) && (EXTRACT_FIELD(smp_cfg, 0x3F << 1) >= 1))
				command_print(CMD, "                  CIDU        64KB        %#lx", iregion_base + 0x50000);
			if (csr_mcfg & BIT(3))
				command_print(CMD, "                  PLIC        64MB        %#lx", iregion_base + 0x4000000);
			/* SMP */
			if (csr_mcfg & BIT(11)) {
				command_print_sameline(CMD, "         SMP_CFG:");
				command_print_sameline(CMD, " CC_PRESENT=%ld", EXTRACT_FIELD(smp_cfg, 0x1));
				command_print_sameline(CMD, " SMP_CORE_NUM=%ld", EXTRACT_FIELD(smp_cfg, 0x3F << 1) + 1);
				command_print_sameline(CMD, " IOCP_NUM=%ld", EXTRACT_FIELD(smp_cfg, 0x3F << 7));
				command_print(CMD, " PMON_NUM=%ld", EXTRACT_FIELD(smp_cfg, 0x3F << 13));
			}
			/* ECLIC */
			if (csr_mcfg & BIT(2)) {
				riscv_reg_t clic_cfg = 0;
				target_read_memory(target, iregion_base + 0x20000, 4, 1, (uint8_t *)&clic_cfg);
				riscv_reg_t clic_info = 0;
				target_read_memory(target, iregion_base + 0x20004, 4, 1, (uint8_t *)&clic_info);
				riscv_reg_t clic_mth = 0;
				target_read_memory(target, iregion_base + 0x20008, 4, 1, (uint8_t *)&clic_mth);
				command_print_sameline(CMD, "           ECLIC:");
				command_print_sameline(CMD, " VERSION=0x%x", EXTRACT_FIELD(clic_info, 0xFF << 13));
				command_print_sameline(CMD, " NUM_INTERRUPT=%x", EXTRACT_FIELD(clic_info, 0xFFF));
				command_print_sameline(CMD, " CLICINTCTLBITS=%x", EXTRACT_FIELD(clic_info, 0xF << 21));
				command_print_sameline(CMD, " MTH=%x", EXTRACT_FIELD(clic_mth, 0xFF << 24));
				command_print(CMD, " NLBITS=%x", EXTRACT_FIELD(clic_cfg, 0xF << 1));
			}
			/* L2CACHE */
			if (smp_cfg & 0x1) {
				command_print_sameline(CMD, "         L2CACHE:");
				riscv_reg_t cc_cfg = 0;
				target_read_memory(target, iregion_base + 0x40008, 4, 1, (uint8_t *)&cc_cfg);
				show_cache_info(POWER_FOR_TWO(EXTRACT_FIELD(cc_cfg, 0xF)), EXTRACT_FIELD(cc_cfg, 0xF << 4) + 1,
								POWER_FOR_TWO(EXTRACT_FIELD(cc_cfg, 0x7 << 8) + 2), cc_cfg & BIT(11));
			}
			/* INFO */
			command_print(CMD, "     INFO-Detail:");
			riscv_reg_t mpasize = 0;
			target_read_memory(target, iregion_base, 4, 1, (uint8_t *)&mpasize);
			command_print(CMD, "                  mpasize : %ld", mpasize);
			riscv_reg_t cmo_info = 0;
			target_read_memory(target, iregion_base + 4, 4, 1, (uint8_t *)&cmo_info);
			if (cmo_info & 0x1) {
				command_print(CMD, "                  cbozero : %dByte",
					POWER_FOR_TWO(EXTRACT_FIELD(cmo_info, 0xF << 6) + 2));
				command_print(CMD, "                  cmo     : %dByte",
					POWER_FOR_TWO(EXTRACT_FIELD(cmo_info, 0xF << 2) + 2));
				if (cmo_info & 0x2)
					command_print(CMD, "                  has_prefecth");
			}
			riscv_reg_t mcppi_cfg_lo = 0;
			target_read_memory(target, iregion_base + 0x80, 4, 1, (uint8_t *)&mcppi_cfg_lo);
			riscv_reg_t mcppi_cfg_hi = 0;
			target_read_memory(target, iregion_base + 0x84, 4, 1, (uint8_t *)&mcppi_cfg_hi);
			if (mcppi_cfg_lo & 0x1) {
				if (riscv_xlen(target) == 32)
					command_print_sameline(CMD, "                  cppi    : %#lx", mcppi_cfg_lo & (~0x3FF));
				else
					command_print_sameline(CMD, "                  cppi    : %#lx",
						(mcppi_cfg_hi << 32) | (mcppi_cfg_lo & (~0x3FF)));
				print_size(POWER_FOR_TWO(EXTRACT_FIELD(mcppi_cfg_lo, 0x1F << 1) - 1) * KB);
				command_print(CMD, " ");
			}
		}
	}

	/* TLB */
	if (csr_mcfg & BIT(3)) {
		if (riscv_reg_get(target, &csr_mtlbcfg, CSR_MTLBCFG_INFO + GDB_REGNO_CSR0) == ERROR_OK) {
			command_print(CMD, "            DTLB: %ld entry",
				POWER_FOR_TWO(EXTRACT_FIELD(csr_mtlbcfg, 0x7 << 19) - 1));
			command_print(CMD, "            ITLB: %ld entry",
				POWER_FOR_TWO(EXTRACT_FIELD(csr_mtlbcfg, 0x7 << 16) - 1));
			command_print_sameline(CMD, "            MTLB:");
			command_print_sameline(CMD, " %ld entry", POWER_FOR_TWO(EXTRACT_FIELD(csr_mtlbcfg, 0xF) + 3) *
														(EXTRACT_FIELD(csr_mtlbcfg, 0x7 << 4) + 1));
			if (csr_mtlbcfg & BIT(10))
				command_print_sameline(CMD, " has_ecc");
			command_print(CMD, " ");
		}
	}

	/* FIO */
	if (csr_mcfg & BIT(4)) {
		if (riscv_reg_get(target, &csr_mfiocfg, CSR_MFIOCFG_INFO + GDB_REGNO_CSR0) == ERROR_OK) {
			command_print_sameline(CMD, "             FIO:");
			command_print_sameline(CMD, " %#lx", csr_mfiocfg & (~0x3FF));
			print_size(POWER_FOR_TWO(EXTRACT_FIELD(csr_mfiocfg, 0x1F << 1) - 1) * KB);
			command_print(CMD, " ");
		}
	}

	/* PPI */
	if (csr_mcfg & BIT(5)) {
		if (riscv_reg_get(target, &csr_mppicfg, CSR_MPPICFG_INFO + GDB_REGNO_CSR0) == ERROR_OK) {
			command_print_sameline(CMD, "             PPI:");
			command_print_sameline(CMD, " %#lx", csr_mppicfg & (~0x3FF));
			print_size(POWER_FOR_TWO(EXTRACT_FIELD(csr_mppicfg, 0x1F << 1) - 1) * KB);
			command_print(CMD, " ");
		}
	}

	command_print(CMD, "----End of cpuinfo");
	return ERROR_OK;
}

#define get_field(reg, mask) (((reg) & (mask)) / ((mask) & ~((mask) << 1)))
#define set_field(reg, mask, val) (((reg) & ~(mask)) | (((val) * ((mask) & ~((mask) << 1))) & (mask)))
#define field_value(mask, val) set_field((riscv_reg_t)0, mask, val)

#define ETRACE_BASE_HI		(0x00)
#define ETRACE_BASE_LO		(0x04)
#define ETRACE_WLEN		(0x08)
#define ETRACE_ENA		(0x0c)
#define ETRACE_INTERRUPT	(0x10)
#define ETRACE_MAXTIME		(0x14)
#define ETRACE_EARLY		(0x18)
#define ETRACE_ATOVF		(0x1c)
#define ETRACE_ENDOFFSET	(0x20)
#define ETRACE_FLG		(0x24)
#define ETRACE_ONGOING		(0x28)
#define ETRACE_TIMEOUT		(0x2c)
#define ETRACE_IDLE		(0x30)
#define ETRACE_FIFO		(0x34)
#define ETRACE_ATBDW		(0x38)
#define ETRACE_WRAP		(0x3c)
#define ETRACE_COMPACT		(0x40)

static target_addr_t atb2axi_config_addr = 0xa5a5a5a5;
static target_addr_t buffer_addr = 0xa5a5a5a5;
static uint32_t buffer_size;

#define CHECK_ETRACE_CONFIGURED()	{ \
		if (atb2axi_config_addr == 0xa5a5a5a5) { \
			LOG_ERROR("ETrace is not configured, please configure it first!"); \
			LOG_INFO("eg. nuclei etrace config <atb2axi-addr> <buffer-addr> <buffer-size> <wrap>"); \
			return ERROR_OK; \
		} \
	}

static int etrace_read_reg(struct target *target, uint32_t offset, uint32_t *value)
{
	CHECK_ETRACE_CONFIGURED();
	int result = target_read_u32(target, atb2axi_config_addr + offset, value);
	if (result != ERROR_OK) {
		LOG_ERROR("Failed to read etrace atb2axi register at %#x", atb2axi_config_addr + offset);
		return result;
	}
	return ERROR_OK;
}

static int etrace_write_reg(struct target *target, uint32_t offset, uint32_t value)
{
	CHECK_ETRACE_CONFIGURED();
	int result = target_write_u32(target, atb2axi_config_addr + offset, value);
	if (result != ERROR_OK) {
		LOG_ERROR("Failed to write etrace atb2axi register %#x with %#x", value, atb2axi_config_addr + offset);
		return result;
	}
	return ERROR_OK;
}

void etrace_stop(struct target *target)
{
	uint32_t tmp;
	uint32_t wait_idle = 0x100;

	CHECK_ETRACE_CONFIGURED();
	etrace_write_reg(target, ETRACE_ENA, 0);
	do {
		etrace_read_reg(target, ETRACE_IDLE, &tmp);
		wait_idle -= 1;
		if (wait_idle == 0)
			break;
	} while (tmp != 1);
}

COMMAND_HANDLER(handle_etrace_config_command)
{
	uint32_t wrap;

	if (CMD_ARGC != 4)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct target *target = get_current_target(CMD_CTX);

	uint32_t tmp = 0;
	if (atb2axi_config_addr != 0xa5a5a5a5)
		etrace_read_reg(target, ETRACE_ENA, &tmp);
	if (tmp) {
		LOG_ERROR("ETrace is still running, please run nuclei etrace stop to stop it first!");
		return ERROR_OK;
	}

	COMMAND_PARSE_NUMBER(target_addr, CMD_ARGV[0], atb2axi_config_addr);
	COMMAND_PARSE_NUMBER(target_addr, CMD_ARGV[1], buffer_addr);
	COMMAND_PARSE_NUMBER(u32, CMD_ARGV[2], buffer_size);
	COMMAND_PARSE_NUMBER(u32, CMD_ARGV[3], wrap);

	if (etrace_write_reg(target, ETRACE_ENDOFFSET, 0) != ERROR_OK) {
		/* Only check first register write, if ok, assume configured correct */
		LOG_ERROR("Unable to write etrace atb2axi register, please check!");
		return ERROR_OK;
	}
	etrace_write_reg(target, ETRACE_FLG, 0);
	etrace_write_reg(target, ETRACE_BASE_HI, (uint32_t)(buffer_addr >> 32));
	etrace_write_reg(target, ETRACE_BASE_LO, (uint32_t)buffer_addr);
	etrace_write_reg(target, ETRACE_WLEN, buffer_size);
	if (wrap) {
		etrace_write_reg(target, ETRACE_WRAP, 1);
		etrace_write_reg(target, ETRACE_COMPACT, 0);
	} else {
		etrace_write_reg(target, ETRACE_WRAP, 0);
		etrace_write_reg(target, ETRACE_COMPACT, 1);
	}

	return ERROR_OK;
}

COMMAND_HANDLER(handle_etrace_enable_command)
{
	if (CMD_ARGC > 0)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct target *target = get_current_target(CMD_CTX);
	struct target *target_real;
	if (target->smp)
		target_real = get_target(target->current_target_name);
	else
		target_real = target;

	riscv_reg_t dpc_rb;
	riscv_reg_t tselect;
	riscv_reg_t tdata1 = field_value(CSR_MCONTROL_TYPE(riscv_xlen(target_real)), CSR_TDATA1_TYPE_MCONTROL) |
							field_value(CSR_MCONTROL_ACTION, CSR_MCONTROL_ACTION_TRACE_ON) |
							field_value(CSR_MCONTROL_M, 1) |
							field_value(CSR_MCONTROL_S, 1) |
							field_value(CSR_MCONTROL_U, 1) |
							field_value(CSR_MCONTROL_EXECUTE, 1);
	RISCV_INFO(r);
	if (riscv_enumerate_triggers(target_real) != ERROR_OK) {
		LOG_ERROR("Unable to enumerate trigger for target %s.\n", target->current_target_name);
		return ERROR_OK;
	}
	if (target_real->etrace_trigger_index == 0xFFFFFFFF) {
		for (unsigned int i = 0; i < r->trigger_count; i++) {
			if (r->trigger_unique_id[i] != -1) {
				LOG_DEBUG("Trigger %d is in use, could't be reserved for etrace usage.\n", i);
				continue;
			}
			target_real->etrace_trigger_index = i;
			r->trigger_unique_id[i] = 0xFE;
			r->reserved_triggers[i] = true;
			LOG_DEBUG("Trigger %d is reserved for etrace usage.\n", i);
			break;
		}
	}
	if (target_real->etrace_trigger_index == 0xFFFFFFFF) {
		LOG_ERROR("No available trigger could be reserved for etrace usage.\n");
		return ERROR_OK;
	}
	tselect = target_real->etrace_trigger_index;
	if (riscv_reg_set(target_real, GDB_REGNO_TSELECT, tselect) != ERROR_OK)
		goto error;
	if (riscv_reg_set(target_real, GDB_REGNO_TDATA1, tdata1) != ERROR_OK)
		goto error;
	if (riscv_reg_get(target_real, &dpc_rb, GDB_REGNO_DPC) != ERROR_OK)
		goto error;
	if (riscv_reg_set(target_real, GDB_REGNO_TDATA2, dpc_rb) != ERROR_OK)
		goto error;

	return ERROR_OK;
error:
	LOG_ERROR("Unable to configure selected etrace trigger %u.\n", (uint32_t)tselect);
	return ERROR_OK;
}

COMMAND_HANDLER(handle_etrace_disable_command)
{
	if (CMD_ARGC > 0)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct target *target = get_current_target(CMD_CTX);
	struct target *target_real;
	if (target->smp)
		target_real = get_target(target->current_target_name);
	else
		target_real = target;

	riscv_reg_t dpc_rb;
	riscv_reg_t tselect;
	riscv_reg_t tdata1 = field_value(CSR_MCONTROL_TYPE(riscv_xlen(target_real)), CSR_TDATA1_TYPE_MCONTROL) |
							field_value(CSR_MCONTROL_ACTION, CSR_MCONTROL_ACTION_TRACE_OFF) |
							field_value(CSR_MCONTROL_M, 1) |
							field_value(CSR_MCONTROL_S, 1) |
							field_value(CSR_MCONTROL_U, 1) |
							field_value(CSR_MCONTROL_EXECUTE, 1);
	RISCV_INFO(r);
	if (target_real->etrace_trigger_index == 0xFFFFFFFF)
		return ERROR_OK;
	if (riscv_enumerate_triggers(target_real) != ERROR_OK) {
		LOG_ERROR("Unable to enumerate trigger for target %s.\n", target->current_target_name);
		return ERROR_OK;
	}
	tselect = target_real->etrace_trigger_index;
	if (riscv_reg_set(target_real, GDB_REGNO_TSELECT, tselect) != ERROR_OK)
		goto error;
	if (riscv_reg_set(target_real, GDB_REGNO_TDATA1, tdata1) != ERROR_OK)
		goto error;
	if (riscv_reg_get(target_real, &dpc_rb, GDB_REGNO_DPC) != ERROR_OK)
		goto error;
	if (riscv_reg_set(target_real, GDB_REGNO_TDATA2, dpc_rb) != ERROR_OK)
		goto error;

	LOG_DEBUG("Etrace free up trigger %d.\n", target_real->etrace_trigger_index);
	r->reserved_triggers[target_real->etrace_trigger_index] = false;
	r->trigger_unique_id[target_real->etrace_trigger_index] = -1;
	target_real->etrace_trigger_index = 0xFFFFFFFF;

	return ERROR_OK;
error:
	LOG_ERROR("Unable to configure selected etrace trigger %u.\n", (uint32_t)tselect);
	return ERROR_OK;
}

COMMAND_HANDLER(handle_etrace_start_command)
{
	if (CMD_ARGC > 0)
		return ERROR_COMMAND_SYNTAX_ERROR;

	CHECK_ETRACE_CONFIGURED();
	struct target *target = get_current_target(CMD_CTX);

	etrace_write_reg(target, ETRACE_ENA, 1);

	return ERROR_OK;
}

COMMAND_HANDLER(handle_etrace_stop_command)
{
	if (CMD_ARGC > 0)
		return ERROR_COMMAND_SYNTAX_ERROR;

	CHECK_ETRACE_CONFIGURED();
	struct target *target = get_current_target(CMD_CTX);

	etrace_stop(target);

	return ERROR_OK;
}

COMMAND_HANDLER(handle_etrace_dump_command)
{
	uint32_t end_offset;
	uint32_t full_flag;
	target_addr_t address;
	uint32_t size;
	uint8_t *temp = NULL;
	struct fileio *fileio = NULL;
	int retval = ERROR_OK;
	struct duration bench;

	if (CMD_ARGC != 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	CHECK_ETRACE_CONFIGURED();
	struct target *target = get_current_target(CMD_CTX);

	uint32_t tmp = 0;

	etrace_read_reg(target, ETRACE_ENA, &tmp);
	if (tmp) {
		LOG_ERROR("ETrace is still running, please use nuclei etrace stop to stop it first!");
		return ERROR_OK;
	}

	etrace_read_reg(target, ETRACE_ENDOFFSET, &end_offset);
	etrace_read_reg(target, ETRACE_FLG, &full_flag);
	if (full_flag) {
		address = buffer_addr + end_offset;
		size = buffer_size;
	} else {
		address = buffer_addr;
		size = end_offset;
	}

	if (size == 0) {
		command_print(CMD, "No etrace could be dumped!");
		goto ok;
	}
	uint32_t temp_size = (size > 4096) ? 4096 : size;
	temp = malloc(temp_size);
	if (!temp)
		goto fail;

	if (fileio_open(&fileio, CMD_ARGV[0], FILEIO_WRITE, FILEIO_BINARY) != ERROR_OK)
		goto fail;

	duration_start(&bench);

	while (size > 0) {
		size_t size_written;
		uint32_t this_run_size = (size > temp_size) ? temp_size : size;
		if ((this_run_size + address) > (buffer_addr + buffer_size))
			this_run_size = (buffer_addr + buffer_size) - address;

		retval = target_read_buffer(target, address, this_run_size, temp);
		if (retval != ERROR_OK)
			break;

		retval = fileio_write(fileio, this_run_size, temp, &size_written);
		if (retval != ERROR_OK)
			break;

		size -= this_run_size;
		if ((this_run_size + address) == (buffer_addr + buffer_size))
			address = buffer_addr;
		else
			address += this_run_size;
	}

	if (retval == ERROR_OK && (duration_measure(&bench) == ERROR_OK)) {
		size_t filesize;
		fileio_size(fileio, &filesize);
		command_print(CMD,
				"Dumped etrace %zu bytes in %fs (%0.3f KiB/s)", filesize,
				duration_elapsed(&bench), duration_kbps(&bench, filesize));
	} else {
		goto fail;
	}

	goto ok;
fail:
	LOG_ERROR("Unexpected error happened during dumping etrace!");
	retval = ERROR_OK;
ok:
	if (fileio)
		fileio_close(fileio);
	if (temp)
		free(temp);

	return retval;
}

COMMAND_HANDLER(handle_etrace_clear_command)
{
	if (CMD_ARGC > 0)
		return ERROR_COMMAND_SYNTAX_ERROR;

	CHECK_ETRACE_CONFIGURED();
	struct target *target = get_current_target(CMD_CTX);

	etrace_stop(target);

	etrace_write_reg(target, ETRACE_ENDOFFSET, 0);
	etrace_write_reg(target, ETRACE_FLG, 0);

	return ERROR_OK;
}

COMMAND_HANDLER(handle_etrace_info_command)
{
	uint32_t end_offset;
	uint32_t full_flag;

	CHECK_ETRACE_CONFIGURED();
	if (CMD_ARGC > 0)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct target *target = get_current_target(CMD_CTX);

	etrace_read_reg(target, ETRACE_ENDOFFSET, &end_offset);
	etrace_read_reg(target, ETRACE_FLG, &full_flag);

	command_print(CMD, "ATB2AXI Addr: %#lx", atb2axi_config_addr);
	command_print(CMD, "Buffer  Addr: %#lx", buffer_addr);
	command_print(CMD, "Buffer  Size: %#x", buffer_size);
	command_print_sameline(CMD, "Buffer Status: ");
	if (full_flag)
		command_print(CMD, "used 100%% from %#lx [wraped]", buffer_addr + end_offset);
	else
		command_print(CMD, "used %d%% from %#lx to %#lx", (end_offset * 100) / buffer_size, \
			buffer_addr, buffer_addr + end_offset);

	return ERROR_OK;
}

static const struct command_registration etrace_command_handlers[] = {
	{
		.name = "config",
		.handler = handle_etrace_config_command,
		.mode = COMMAND_EXEC,
		.help = "Configuration etrace.",
		.usage = "atb2axi--addr buffer-addr buffer-size wrap",
	},
	{
		.name = "enable",
		.handler = handle_etrace_enable_command,
		.mode = COMMAND_EXEC,
		.help = "enable etrace",
		.usage = "",
	},
	{
		.name = "disable",
		.handler = handle_etrace_disable_command,
		.mode = COMMAND_EXEC,
		.help = "disable etrace",
		.usage = "",
	},
	{
		.name = "start",
		.handler = handle_etrace_start_command,
		.mode = COMMAND_EXEC,
		.help = "start etrace collection",
		.usage = "",
	},
	{
		.name = "stop",
		.handler = handle_etrace_stop_command,
		.mode = COMMAND_EXEC,
		.help = "stop etrace collection",
		.usage = "",
	},
	{
		.name = "dump",
		.handler = handle_etrace_dump_command,
		.mode = COMMAND_EXEC,
		.help = "dump etrace data",
		.usage = "filename",
	},
	{
		.name = "clear",
		.handler = handle_etrace_clear_command,
		.mode = COMMAND_EXEC,
		.help = "clear etrace data",
		.usage = "",
	},
	{
		.name = "info",
		.handler = handle_etrace_info_command,
		.mode = COMMAND_EXEC,
		.help = "display etrace info",
		.usage = "",
	},
	COMMAND_REGISTRATION_DONE
};

uint64_t nuclei_get_dmcustom(struct target *target, uint32_t type, uint32_t hart_id, uint32_t index)
{
	uint64_t out_data;
	uint32_t address, value;
	RISCV_INFO(r);
	if (!r) {
		LOG_ERROR("riscv_info is NULL!");
		return ERROR_FAIL;
	}

	if (r->dmi_write) {
		address = 0x70;
		value = (type << 28) | (hart_id << 8) | index;
		if (r->dmi_write(target, address, value) != ERROR_OK)
			return ERROR_FAIL;
	} else {
		LOG_ERROR("dmi_write is not implemented for this target.");
		return ERROR_FAIL;
	}

	if (r->dmi_read) {
		address = 0x71;
		if (r->dmi_read(target, &value, address) != ERROR_OK)
			return ERROR_FAIL;
		out_data = value;
		address = 0x72;
		if (r->dmi_read(target, &value, address) != ERROR_OK)
			return ERROR_FAIL;
		out_data |= value << 32;
	} else {
		LOG_ERROR("dmi_read is not implemented for this target.");
		return ERROR_FAIL;
	}
	return out_data;
}

COMMAND_HANDLER(nuclei_set_expose_cpu_core)
{
	if (CMD_ARGC == 0) {
		LOG_ERROR("This command expects at least 1 parameters");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(info);
	int ret = ERROR_OK;

	for (unsigned int i = 0; i < CMD_ARGC; i++) {
		ret = parse_reg_ranges(&info->expose_nuclei_cpu_core, CMD_ARGV[i], "nuclei_cpu_core", 0xFF);
		if (ret != ERROR_OK)
			break;
	}

	return ret;
}

COMMAND_HANDLER(nuclei_set_examine_cpu_core)
{
	if (CMD_ARGC == 0) {
		LOG_ERROR("This command expects at least 1 parameters");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	uint64_t value;
	uint32_t halt_id = 0;
	uint32_t index = 0;
	struct target *target = get_current_target(CMD_CTX);
	RISCV_INFO(r);
	if (!r) {
		LOG_ERROR("riscv_info is NULL!");
		return ERROR_FAIL;
	}

	if (CMD_ARGC == 1) {
		COMMAND_PARSE_NUMBER(uint, CMD_ARGV[0], halt_id);
		range_list_t *entry;
		list_for_each_entry(entry, &r->expose_nuclei_cpu_core, list) {
			value = nuclei_get_dmcustom(target, 0, halt_id, entry->high);
			command_print_sameline(CMD, "%u: ", entry->high);
			command_print(CMD, "0x%016" PRIx64, value);
		}
	} else if (CMD_ARGC == 2) {
		COMMAND_PARSE_NUMBER(uint, CMD_ARGV[0], halt_id);
		COMMAND_PARSE_NUMBER(uint, CMD_ARGV[1], index);
		value = nuclei_get_dmcustom(target, 0, halt_id, index);
		command_print_sameline(CMD, "%u: ", index);
		command_print(CMD, "0x%016" PRIx64, value);
	}

	return ERROR_OK;
}

COMMAND_HANDLER(handle_halt_group_command)
{
	if (CMD_ARGC < 2) {
		LOG_ERROR("This command expects at least 2 parameters");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	bool on_off;
	COMMAND_PARSE_BOOL(CMD_ARGV[0], on_off, "on", "off");

	struct target *target;
	RISCV_INFO(r);
	if (r->dmi_write) {
		int retval = ERROR_OK;
		for (int i = 1; i < CMD_ARGC; i++) {
			target = get_target(CMD_ARGV[i]);
			if (on_off)
				retval = r->dmi_write(target, DM_DMCS2, 0x7);
			else
				retval = r->dmi_write(target, DM_DMCS2, 0x0);
		}
		return retval;
	}

	return ERROR_OK;
}

COMMAND_HANDLER(handle_resume_group_command)
{
	if (CMD_ARGC < 2) {
		LOG_ERROR("This command expects at least 2 parameters");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	bool on_off;
	COMMAND_PARSE_BOOL(CMD_ARGV[0], on_off, "on", "off");

	struct target *target;
	RISCV_INFO(r);
	if (r->dmi_write) {
		int retval = ERROR_OK;
		for (int i = 1; i < CMD_ARGC; i++) {
			target = get_target(CMD_ARGV[i]);
			if (on_off)
				retval = r->dmi_write(target, DM_DMCS2, 0x887);
			else
				retval = r->dmi_write(target, DM_DMCS2, 0x0);
		}
		return retval;
	}

	return ERROR_OK;
}

static const struct command_registration cti_command_handlers[] = {
	{
		.name = "halt_group",
		.handler = handle_halt_group_command,
		.mode = COMMAND_EXEC,
		.help = "Configure and clear corss-trigger halt group",
		.usage = "halt_group on/off target_name0 target_name1",
	},
	{
		.name = "resume_group",
		.handler = handle_resume_group_command,
		.mode = COMMAND_EXEC,
		.help = "Configure and clear corss-trigger resume group",
		.usage = "resume_group on/off target_name0 target_name1",
	},
	COMMAND_REGISTRATION_DONE
};

const struct command_registration nuclei_command_group_handlers[] = {
	{
		.name = "cpuinfo",
		.handler = handle_nuclei_cpuinfo,
		.mode = COMMAND_ANY,
		.usage = "",
		.help = "Displays some information about the target"
	},
	{
		.name = "etrace",
		.mode = COMMAND_ANY,
		.help = "Embedded Trace command group",
		.usage = "",
		.chain = etrace_command_handlers,
	},
	{
		.name = "expose_cpu_core",
		.handler = nuclei_set_expose_cpu_core,
		.mode = COMMAND_CONFIG,
		.usage = "<index0> ... <index255>",
		.help = "Configure a list of index for `nuclei_examine_cpu_core` to expose in "
				"This must be executed before `init`."
	},
	{
		.name = "examine_cpu_core",
		.handler = nuclei_set_examine_cpu_core,
		.mode = COMMAND_ANY,
		.usage = "<hartid> [index]",
		.help = "Return the 64-bit value read from dm-custom1 and dm-custom2 "
				"value = dm-custom2 << 32 + dm-custom1 "
	},
	{
		.name = "cti",
		.mode = COMMAND_ANY,
		.help = "Cross trigger command group",
		.usage = "",
		.chain = cti_command_handlers,
	},
	COMMAND_REGISTRATION_DONE
};
