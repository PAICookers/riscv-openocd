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
#include "riscv_reg.h"
#include "rtos/rtos.h"
#include "debug_defines.h"
#include <helper/bits.h>

#define get_field(reg, mask) (((reg) & (mask)) / ((mask) & ~((mask) << 1)))
#define set_field(reg, mask, val) (((reg) & ~(mask)) | (((val) * ((mask) & ~((mask) << 1))) & (mask)))
#define field_value(mask, val) set_field((riscv_reg_t)0, mask, val)

volatile struct command_invocation *PCMD = NULL;

void cif_printf(const char *format, ...)
{
	char *string;

	va_list ap;
	va_start(ap, format);

	string = alloc_vprintf(format, ap);
	if (string && PCMD) {
		/* we want this collected in the log + we also want to pick it up as a tcl return
		 * value.
		 *
		 * The latter bit isn't precisely neat, but will do for now.
		 */
		Jim_AppendString(PCMD->ctx->interp, PCMD->output, string, -1);
		/* We already printed it above
		 * command_output_text(context, string); */
		free(string);
	}

	va_end(ap);
}

void set_print_interface(struct command_invocation *cmd)
{
	PCMD = cmd;
}

#include "cpuinfo.h"

int nuclei_get_cpuinfo(struct target *target, NUCLEI_CPUINFO *cpu)
{
    riscv_reg_t regval;
    CPU_INFO_Group *cpuinfo;

    if (cpu == NULL || target == NULL) {
        return -1;
    }
    memset(cpu, 0, sizeof(NUCLEI_CPUINFO)); // clear the struct
    cpuinfo = &cpu->cpuinfo;
	cpuinfo->iinfo = &cpu->iinfo;
    cpuinfo->eclic = &cpu->eclic;
	cpuinfo->xlen = riscv_xlen(target);

	if (riscv_reg_get(target, &regval, GDB_REGNO_CSR0 + CSR_MARCHID) == ERROR_OK)
        cpuinfo->marchid.d = (uint32_t)regval;
	if (riscv_reg_get(target, &regval, GDB_REGNO_CSR0 + CSR_MHARTID) == ERROR_OK)
        cpuinfo->mhartid = (uint32_t)regval;
	if (riscv_reg_get(target, &regval, GDB_REGNO_CSR0 + CSR_MIMPID) == ERROR_OK)
        cpuinfo->mimpid.d = (uint32_t)regval;
	if (riscv_reg_get(target, &regval, GDB_REGNO_CSR0 + CSR_MISA) == ERROR_OK)
        cpuinfo->misa.d = (uint32_t)regval;

    if (riscv_reg_get(target, &regval, GDB_REGNO_CSR0 + CSR_MCFG_INFO) == ERROR_OK) {
        cpuinfo->mcfginfo.d = (uint32_t)regval;
		cpuinfo->mcfg_exist = 1;
    }
    // workaround for gd32vf103, it have mcfg_info csr but the bitfields are different from latest version
    // just mark mcfg_exist = 0
    if (cpuinfo->marchid.d == 0x80000022U && cpuinfo->mimpid.d == 0x100U) {
		cpuinfo->mcfg_exist = 0;
        cpuinfo->mcfginfo.d = 0;
    }
	/**
	 * mtlbcfginfo has a `mapping` field at the highest bit.
	 * For RV64, move the bit 63 to bit 31 to use the common
	 * struct as RV32.
	 */
	if (riscv_xlen(target) == 32) {
		if (cpuinfo->mcfginfo.b.plic)
            if (riscv_reg_get(target, &regval, GDB_REGNO_CSR0 + CSR_MTLBCFG_INFO) == ERROR_OK) {
                cpuinfo->mtlbcfginfo.d = (uint32_t)regval;
            }
	} else {
		if (cpuinfo->mcfginfo.b.plic) {
            if (riscv_reg_get(target, &regval, GDB_REGNO_CSR0 + CSR_MTLBCFG_INFO) == ERROR_OK) {
			    cpuinfo->mtlbcfginfo.d = (uint32_t)regval | (uint32_t)((regval >> 63) << 31);
            }
		}
	}

    if (cpuinfo->mcfginfo.b.icache || cpuinfo->mcfginfo.b.ilm) {
        if (riscv_reg_get(target, &regval, GDB_REGNO_CSR0 + CSR_MICFG_INFO) == ERROR_OK) {
            cpuinfo->micfginfo.d = (uint32_t)regval;
        }
    }
    if (cpuinfo->mcfginfo.b.dcache || cpuinfo->mcfginfo.b.dlm) {
        if (riscv_reg_get(target, &regval, GDB_REGNO_CSR0 + CSR_MDCFG_INFO) == ERROR_OK) {
            cpuinfo->mdcfginfo.d = (uint32_t)regval;
        }
    }
	if (cpuinfo->mcfginfo.b.iregion) {
        if (riscv_reg_get(target, &regval, GDB_REGNO_CSR0 + CSR_MIRGB_INFO) == ERROR_OK) {
            cpuinfo->mirgbinfo.d = (uint64_t)regval;
        }
		uint64_t iregion_base = cpuinfo->mirgbinfo.d & (~0x3FFULL);
        memset(&cpu->iinfo, 0 ,sizeof(IINFO_Type));
		target_read_memory(target, iregion_base, 4, sizeof(IINFO_Type) / 4, (uint8_t *)&cpu->iinfo);
        cpuinfo->iregion_base = iregion_base;
        if (cpuinfo->mcfginfo.b.smp) {
			target_read_memory(target, (iregion_base + CPUINFO_IRG_SMP_OFS + 0x4), 4, 1, (uint8_t *)&cpuinfo->smpcfg.d);
        }
        if (cpuinfo->smpcfg.b.cc) {
			target_read_memory(target, (iregion_base + CPUINFO_IRG_SMP_OFS + 0x8), 4, 1, (uint8_t *)&cpuinfo->cccfg.d);
        }
        if (cpuinfo->mcfginfo.b.eclic) {
            memset(&cpu->eclic, 0, sizeof(ECLIC_Type));
			target_read_memory(target, (iregion_base + CPUINFO_IRG_ECLIC_OFS), 4, sizeof(ECLIC_Type) / 4, (uint8_t *)&cpu->eclic);
	    }
    }
    if (cpuinfo->mcfginfo.b.ppi) {
        if ( riscv_reg_get(target, &regval, GDB_REGNO_CSR0 + CSR_MPPICFG_INFO) == ERROR_OK) {
            cpuinfo->mppicfginfo.d = (uint64_t)regval;
        }
    }
    if (cpuinfo->mcfginfo.b.fio) {
        if (riscv_reg_get(target, &regval, GDB_REGNO_CSR0 + CSR_MFIOCFG_INFO) == ERROR_OK) {
            cpuinfo->mfiocfginfo.d = (uint64_t)regval;
        }
    }
    return 0;
}

#define EXTRACT_MFG(X)  (((X) & 0xffe) >> 1)
void nuclei_display_cpuinfo(struct target *target, void *cmd)
{
    if (target == NULL)
        return;
    // Nuclei Manufacture ID is 0x536
    if (EXTRACT_MFG(target->tap->idcode) != 0x536) {
        if (cmd == NULL) { // Just basic info
            return;
        }
    }
	NUCLEI_CPUINFO xlcpu;
    if (nuclei_get_cpuinfo(target, &xlcpu) != 0) {
        LOG_ERROR("Unable to get any Nuclei CPU INFO!");
        return;
    }

	char strbuf[8192];
    if (cmd != NULL) {
	    set_print_interface((struct command_invocation *)cmd);
    }
    if (get_basic_cpuinfo(&xlcpu.cpuinfo, strbuf, 8192) > 0) {
        if (cmd == NULL) {
	        LOG_TARGET_INFO(target, "%s", strbuf);
        } else {
            cif_printf("%s: %s", target->cmd_name, strbuf);
        }
    }
    if (CMD != NULL) {
	    show_cpuinfo(&xlcpu.cpuinfo);
    }
}

COMMAND_HANDLER(handle_nuclei_cpuinfo)
{
	struct target *target = NULL;

    target = get_current_target(CMD_CTX);
    if (CMD_CTX->current_target_override == NULL)
		target = get_target(target->current_target_name);
    if (target == NULL)
        target = get_current_target(CMD_CTX);

    nuclei_display_cpuinfo(target, (void *)CMD);

	return ERROR_OK;
}

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
		out_data |= (uint64_t)value << 32;
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

COMMAND_HANDLER(handle_trace_enable_command)
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
	if (target_real->trace_trigger_index == 0xFFFFFFFF) {
		for (unsigned int i = 0; i < r->trigger_count; i++) {
			if (r->trigger_unique_id[i] != -1) {
				LOG_DEBUG("Trigger %d is in use, could't be reserved for trace usage.\n", i);
				continue;
			}
			target_real->trace_trigger_index = i;
			r->trigger_unique_id[i] = 0xFE;
			r->reserved_triggers[i] = true;
			LOG_DEBUG("Trigger %d is reserved for trace usage.\n", i);
			break;
		}
	}
	if (target_real->trace_trigger_index == 0xFFFFFFFF) {
		LOG_ERROR("No available trigger could be reserved for trace usage.\n");
		return ERROR_OK;
	}
	tselect = target_real->trace_trigger_index;
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
	LOG_ERROR("Unable to configure selected trace trigger %u.\n", (uint32_t)tselect);
	return ERROR_OK;
}

COMMAND_HANDLER(handle_trace_disable_command)
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
	if (target_real->trace_trigger_index == 0xFFFFFFFF)
		return ERROR_OK;
	if (riscv_enumerate_triggers(target_real) != ERROR_OK) {
		LOG_ERROR("Unable to enumerate trigger for target %s.\n", target->current_target_name);
		return ERROR_OK;
	}
	tselect = target_real->trace_trigger_index;
	if (riscv_reg_set(target_real, GDB_REGNO_TSELECT, tselect) != ERROR_OK)
		goto error;
	if (riscv_reg_set(target_real, GDB_REGNO_TDATA1, tdata1) != ERROR_OK)
		goto error;
	if (riscv_reg_get(target_real, &dpc_rb, GDB_REGNO_DPC) != ERROR_OK)
		goto error;
	if (riscv_reg_set(target_real, GDB_REGNO_TDATA2, dpc_rb) != ERROR_OK)
		goto error;

	LOG_DEBUG("Trace free up trigger %d.\n", target_real->trace_trigger_index);
	r->reserved_triggers[target_real->trace_trigger_index] = false;
	r->trigger_unique_id[target_real->trace_trigger_index] = -1;
	target_real->trace_trigger_index = 0xFFFFFFFF;

	return ERROR_OK;
error:
	LOG_ERROR("Unable to configure selected trace trigger %u.\n", (uint32_t)tselect);
	return ERROR_OK;
}

static const struct command_registration trace_command_handlers[] = {
	{
		.name = "enable",
		.handler = handle_trace_enable_command,
		.mode = COMMAND_EXEC,
		.help = "enable trace within a core",
		.usage = "",
	},
	{
		.name = "disable",
		.handler = handle_trace_disable_command,
		.mode = COMMAND_EXEC,
		.help = "disable trace within a core",
		.usage = "",
	},
	COMMAND_REGISTRATION_DONE
};

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
		LOG_ERROR("Failed to read etrace atb2axi register at %#lx", (uint64_t)(atb2axi_config_addr + offset));
		return result;
	}
	return ERROR_OK;
}

static int etrace_write_reg(struct target *target, uint32_t offset, uint32_t value)
{
	CHECK_ETRACE_CONFIGURED();
	int result = target_write_u32(target, atb2axi_config_addr + offset, value);
	if (result != ERROR_OK) {
		LOG_ERROR("Failed to write etrace atb2axi register %#lx with %#x", (uint64_t)(atb2axi_config_addr + offset), value);
		return result;
	}
	return ERROR_OK;
}

static int etrace_stop(struct target *target)
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
    return ERROR_OK;
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
		.usage = "atb2axi-addr buffer-addr buffer-size wrap",
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

//FUNNEL Registers
#define BTRACE_FUNNEL		(0x0)
#define BTRACE_FCTRL		(0x0)
#define BTRACE_FIMPL		(0x4)
#define BTRACE_FDISIN		(0x8)
#define BTRACE_TSCTRL		(0x40)
#define BTRACE_TSCNTLOW		(0x48)
#define BTRACE_TSCNTHI		(0x4C)
#define BTRACE_FCSTM_CS		(0xE00)
#define BTRACE_FOVF		(0xE04)
#define BTRACE_FINT		(0xE08)
#define BTRACE_FTIMEOUT		(0xE0C)
//AXI_SINK Registers
#define BTRACE_AXI_SINK		(0x1000)
#define BTRACE_RSCTRL		(0x0)
#define BTRACE_RSIMPL		(0x4)
#define BTRACE_RSSTARTL		(0x10)
#define BTRACE_RSSTARTH		(0x14)
#define BTRACE_RSLIMITL		(0x18)
#define BTRACE_RSLIMITH		(0x1C)
#define BTRACE_RSWPL		(0x20)
#define BTRACE_RSWPH		(0x24)
#define BTRACE_RCSTM_CS		(0xE00)
//PIB_SINK Registers
#define BTRACE_PIB_SINK		(0x2000)
#define BTRACE_PSCTRL		(0x0)
#define BTRACE_PSIMPL		(0x4)
#define BTRACE_PSSYNC		(0xE00)
#define BTRACE_PSTIMEOUT	(0xE04)
//ATB_BRIDGE Registers
#define BTRACE_ATB_BRIDGE	(0x3000)
#define BTRACE_ABCTRL		(0x0)
#define BTRACE_ABIMPL		(0x4)
#define BTRACE_AB_STATUS	(0xE00)
//Encoder Registers
#define BTRACE_CORE0_TECTRL	(0x4000)
#define BTRACE_CORE0_TEINSFEA	(0x4008)
#define BTRACE_CORE0_TETSCTRL	(0x4040)

static target_addr_t btrace_config_addr = 0xa5a5a5a5;
static target_addr_t btrace_buffer_addr = 0xa5a5a5a5;
static target_addr_t btrace_buffer_size;

#define CHECK_BTRACE_CONFIGURED()	{ \
		if (btrace_config_addr == 0xa5a5a5a5) { \
			LOG_ERROR("Btrace is not configured, please configure it first!"); \
			LOG_INFO("eg. nuclei btrace config btrace-addr buffer-addr buffer-size wrap(0|1) system-timestamp(0|1) system-timestamp-counter-div(1|4|16|64)"); \
			return ERROR_OK; \
		} \
	}

static int btrace_read_reg(struct target *target, uint32_t offset, uint32_t *value)
{
	CHECK_BTRACE_CONFIGURED();
	int result = target_read_u32(target, btrace_config_addr + offset, value);
	if (result != ERROR_OK) {
		LOG_ERROR("Failed to read btrace register at %#lx", (uint64_t)(btrace_config_addr + offset));
		return result;
	}
	return ERROR_OK;
}

static int btrace_write_reg(struct target *target, uint32_t offset, uint32_t value)
{
	CHECK_BTRACE_CONFIGURED();
	int result = target_write_u32(target, btrace_config_addr + offset, value);
	if (result != ERROR_OK) {
		LOG_ERROR("Failed to write btrace register %#lx with %#x", (uint64_t)(btrace_config_addr + offset), value);
		return result;
	}
	return ERROR_OK;
}

static int btrace_stop(struct target *target)
{
        uint32_t tmp;
        uint32_t wait_idle = 0x100;

        CHECK_BTRACE_CONFIGURED();
        btrace_write_reg(target, BTRACE_FUNNEL + BTRACE_FDISIN, 0xFFFF);
	uint32_t core_num;
	btrace_read_reg(target, BTRACE_FUNNEL + BTRACE_FCSTM_CS, &core_num);
	core_num = (core_num >> 1) & 0x1F;
	for (int i = 0; i < core_num;i++) {
		do {
			btrace_read_reg(target, BTRACE_CORE0_TECTRL + i * 0x1000, &tmp);
			wait_idle -= 1;
			if (wait_idle == 0) {
				LOG_ERROR("Timeout to wait core%d encoder empty!", i);
				break;
			}
		}while (!(tmp & 0x8));
		btrace_write_reg(target, BTRACE_CORE0_TECTRL + i * 0x1000, tmp & (~0x2));
		wait_idle = 0x100;
	}
        /*do {
                btrace_read_reg(target, BTRACE_AXI_SINK + BTRACE_RSCTRL, &tmp);
                wait_idle -= 1;
                if (wait_idle == 0) {
			LOG_ERROR("Timeout to wait axi sink empty");
                        break;
		}
        } while (!(tmp & 0x8));
	*/
        wait_idle = 0x100;
	btrace_read_reg(target, BTRACE_AXI_SINK + BTRACE_RSCTRL, &tmp);
        btrace_write_reg(target, BTRACE_AXI_SINK + BTRACE_RSCTRL, tmp & (~0x2));
        do {
                btrace_read_reg(target, BTRACE_AXI_SINK + BTRACE_RCSTM_CS, &tmp);
                wait_idle -= 1;
                if (wait_idle == 0) {
			LOG_ERROR("Timeout to wait axi sink idle");
                        break;
		}
        } while (!(tmp & 0x4));
        return ERROR_OK;
}

COMMAND_HANDLER(handle_btrace_config_command)
{
	uint32_t wrap, system_timestamp, system_timestamp_counter_div, tmp;

	if (CMD_ARGC != 6)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct target *target = get_current_target(CMD_CTX);

	COMMAND_PARSE_NUMBER(target_addr, CMD_ARGV[0], btrace_config_addr);
	COMMAND_PARSE_NUMBER(target_addr, CMD_ARGV[1], btrace_buffer_addr);
	COMMAND_PARSE_NUMBER(target_addr, CMD_ARGV[2], btrace_buffer_size);
	COMMAND_PARSE_NUMBER(u32, CMD_ARGV[3], wrap);
	COMMAND_PARSE_NUMBER(u32, CMD_ARGV[4], system_timestamp);
	COMMAND_PARSE_NUMBER(u32, CMD_ARGV[5], system_timestamp_counter_div);

	// Read Core Number
	uint32_t core_num;
	if (btrace_read_reg(target, BTRACE_FUNNEL + BTRACE_FCSTM_CS, &core_num) != ERROR_OK) {
		/* Only check first register write, if ok, assume configured correct */
		LOG_ERROR("Unable to write btrace register, please check!");
		return ERROR_OK;
	}
	core_num = (core_num >> 1) & 0x1F;
	// Clear All Active And Enable
	btrace_read_reg(target, BTRACE_FUNNEL + BTRACE_FCTRL, &tmp);
	btrace_write_reg(target, BTRACE_FUNNEL + BTRACE_FCTRL, tmp & (~0x3));
	btrace_read_reg(target, BTRACE_FUNNEL + BTRACE_TSCTRL, &tmp);
	btrace_write_reg(target, BTRACE_FUNNEL + BTRACE_TSCTRL, tmp & (~0x3));
	btrace_read_reg(target, BTRACE_AXI_SINK + BTRACE_RSCTRL, &tmp);
	btrace_write_reg(target, BTRACE_AXI_SINK + BTRACE_RSCTRL, tmp & (~0x3));
	// Clear wrap flag
	btrace_write_reg(target, BTRACE_AXI_SINK + BTRACE_RSWPL, 0x00);
	for (int i = 0;i < core_num;i++) {
		btrace_read_reg(target, BTRACE_CORE0_TECTRL + i * 0x1000, &tmp);
		btrace_write_reg(target, BTRACE_CORE0_TECTRL + i * 0x1000, tmp & (~0x3));
	}
	// Config Btrace Funnel
	btrace_read_reg(target, BTRACE_FUNNEL + BTRACE_FCTRL, &tmp);
	btrace_write_reg(target, BTRACE_FUNNEL + BTRACE_FCTRL, tmp | 0x3);
	btrace_read_reg(target, BTRACE_FUNNEL + BTRACE_TSCTRL, &tmp);
	if (system_timestamp) { tmp |= 0x27; } else { tmp |= 0x37;}
	switch (system_timestamp_counter_div) {
		case  1: tmp |= 0x000; break;
		case  4: tmp |= 0x100; break;
		case 16: tmp |= 0x200; break;
		case 64: tmp |= 0x300; break;
		default: LOG_ERROR("Invaild system_timestamp_counter_div config"); return ERROR_OK;
	}
	btrace_write_reg(target, BTRACE_FUNNEL + BTRACE_TSCTRL, tmp);
	btrace_write_reg(target, BTRACE_FUNNEL + BTRACE_TSCTRL, 0x3);
	// Config Btrace AXI_SINK
	btrace_read_reg(target, BTRACE_AXI_SINK + BTRACE_RSCTRL, &tmp);
	tmp &= ~0x100;
	if (wrap) { tmp |= 0x1; } else { tmp |= 0x101; };
	btrace_write_reg(target, BTRACE_AXI_SINK + BTRACE_RSCTRL, tmp);
	btrace_write_reg(target, BTRACE_AXI_SINK + BTRACE_RSSTARTL, (uint32_t)btrace_buffer_addr);
	btrace_write_reg(target, BTRACE_AXI_SINK + BTRACE_RSSTARTH, (uint32_t)(btrace_buffer_addr >> 32));
	btrace_write_reg(target, BTRACE_AXI_SINK + BTRACE_RSLIMITL, (uint32_t)(btrace_buffer_addr + btrace_buffer_size));
	btrace_write_reg(target, BTRACE_AXI_SINK + BTRACE_RSLIMITH, (uint32_t)((btrace_buffer_addr + btrace_buffer_size) >> 32));

	for (int i = 0;i < core_num;i++) {
		btrace_read_reg(target, BTRACE_CORE0_TECTRL + i * 0x1000, &tmp);
		btrace_write_reg(target, BTRACE_CORE0_TECTRL + i * 0x1000, tmp | 0x803);
		btrace_read_reg(target, BTRACE_CORE0_TEINSFEA + i * 0x1000, &tmp);
		btrace_write_reg(target, BTRACE_CORE0_TEINSFEA + i * 0x1000, tmp | 0x8);
		btrace_write_reg(target, BTRACE_CORE0_TETSCTRL + i * 0x1000, 0x8033);
	}
        btrace_write_reg(target, BTRACE_FUNNEL + BTRACE_FDISIN, 0x0);

	return ERROR_OK;
}

COMMAND_HANDLER(handle_btrace_start_command)
{
	if (CMD_ARGC > 0)
		return ERROR_COMMAND_SYNTAX_ERROR;

	CHECK_BTRACE_CONFIGURED();
	struct target *target = get_current_target(CMD_CTX);

	uint32_t tmp;
	btrace_read_reg(target, BTRACE_AXI_SINK + BTRACE_RSCTRL, &tmp);
	btrace_write_reg(target, BTRACE_AXI_SINK + BTRACE_RSCTRL, tmp | 0x2);

	return ERROR_OK;
}

COMMAND_HANDLER(handle_btrace_stop_command)
{
	if (CMD_ARGC > 0)
		return ERROR_COMMAND_SYNTAX_ERROR;

	CHECK_BTRACE_CONFIGURED();
	struct target *target = get_current_target(CMD_CTX);

	btrace_stop(target);

	return ERROR_OK;
}

COMMAND_HANDLER(handle_btrace_dump_command)
{
	target_addr_t end_buffer_addr;
	uint32_t wrap_flag;
	target_addr_t address;
	uint32_t size;
	uint8_t *temp = NULL;
	struct fileio *fileio = NULL;
	int retval = ERROR_OK;
	struct duration bench;

	if (CMD_ARGC != 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	CHECK_BTRACE_CONFIGURED();
	struct target *target = get_current_target(CMD_CTX);

	uint32_t tmp = 0;

	btrace_read_reg(target, BTRACE_AXI_SINK + BTRACE_RSCTRL, &tmp);
	if (tmp & 0x2) {
		LOG_ERROR("Btrace is still running, please use nuclei btrace stop to stop it first!");
		return ERROR_OK;
	}

	btrace_read_reg(target, BTRACE_AXI_SINK + BTRACE_RSWPH, &tmp);
	end_buffer_addr = tmp;
	btrace_read_reg(target, BTRACE_AXI_SINK + BTRACE_RSWPL, &tmp);
	wrap_flag = tmp & 0x1;
	end_buffer_addr = (end_buffer_addr << 32) + (tmp & (~0x1));
	if (wrap_flag) {
		address = end_buffer_addr;
		size = btrace_buffer_size;
	} else {
		address = btrace_buffer_addr;
		size = end_buffer_addr - btrace_buffer_addr;
	}

	if (size == 0) {
		command_print(CMD, "No btrace could be dumped!");
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
		if ((this_run_size + address) > (btrace_buffer_addr + btrace_buffer_size))
			this_run_size = (btrace_buffer_addr + btrace_buffer_size) - address;

		retval = target_read_buffer(target, address, this_run_size, temp);
		if (retval != ERROR_OK)
			break;

		retval = fileio_write(fileio, this_run_size, temp, &size_written);
		if (retval != ERROR_OK)
			break;

		size -= this_run_size;
		if ((this_run_size + address) == (btrace_buffer_addr + btrace_buffer_size))
			address = btrace_buffer_addr;
		else
			address += this_run_size;
	}

	if (retval == ERROR_OK && (duration_measure(&bench) == ERROR_OK)) {
		size_t filesize;
		fileio_size(fileio, &filesize);
		command_print(CMD,
				"Dumped btrace %zu bytes in %fs (%0.3f KiB/s)", filesize,
				duration_elapsed(&bench), duration_kbps(&bench, filesize));
	} else {
		goto fail;
	}

	goto ok;
fail:
	LOG_ERROR("Unexpected error happened during dumping btrace!");
	retval = ERROR_OK;
ok:
	if (fileio)
		fileio_close(fileio);
	if (temp)
		free(temp);

	return retval;
}

COMMAND_HANDLER(handle_btrace_clear_command)
{
	if (CMD_ARGC > 0)
		return ERROR_COMMAND_SYNTAX_ERROR;

	CHECK_BTRACE_CONFIGURED();
	struct target *target = get_current_target(CMD_CTX);

	btrace_stop(target);

	btrace_write_reg(target, BTRACE_AXI_SINK + BTRACE_RSWPH, 0);
	btrace_write_reg(target, BTRACE_AXI_SINK + BTRACE_RSWPL, 0);

	return ERROR_OK;
}

COMMAND_HANDLER(handle_btrace_info_command)
{
	target_addr_t end_buffer_addr;
	uint32_t wrap_flag, tmp;

	CHECK_BTRACE_CONFIGURED();
	if (CMD_ARGC > 0)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct target *target = get_current_target(CMD_CTX);

	btrace_read_reg(target, BTRACE_AXI_SINK + BTRACE_RSWPH, &tmp);
	end_buffer_addr = tmp;
	btrace_read_reg(target, BTRACE_AXI_SINK + BTRACE_RSWPL, &tmp);
	wrap_flag = tmp & 0x1;
	end_buffer_addr = (end_buffer_addr << 32) + (tmp & (~0x1));

	command_print(CMD, "Btrace Addr: %#lx", btrace_config_addr);
	command_print(CMD, "Buffer Addr: %#lx", btrace_buffer_addr);
	command_print(CMD, "Buffer Size: %#lx", btrace_buffer_size);
	command_print_sameline(CMD, "Buffer Status: ");
	if (wrap_flag)
		command_print(CMD, "used 100%% from %#lx [wraped]", end_buffer_addr);
	else
		command_print(CMD, "used %d%% from %#lx to %#lx", \
		(uint32_t)(((end_buffer_addr - btrace_buffer_addr) * 100) / btrace_buffer_size), btrace_buffer_addr, end_buffer_addr);

	return ERROR_OK;
}

static const struct command_registration btrace_command_handlers[] = {
	{
		.name = "config",
		.handler = handle_btrace_config_command,
		.mode = COMMAND_EXEC,
		.help = "Configuration btrace.",
		.usage = "btrace-addr buffer-addr buffer-size wrap(0|1) system-timestamp(0|1) system-timestamp-counter-div(1|4|16|64)",
	},
	{
		.name = "start",
		.handler = handle_btrace_start_command,
		.mode = COMMAND_EXEC,
		.help = "start btrace collection",
		.usage = "",
	},
	{
		.name = "stop",
		.handler = handle_btrace_stop_command,
		.mode = COMMAND_EXEC,
		.help = "stop btrace collection",
		.usage = "",
	},
	{
		.name = "dump",
		.handler = handle_btrace_dump_command,
		.mode = COMMAND_EXEC,
		.help = "dump btrace data",
		.usage = "filename",
	},
	{
		.name = "clear",
		.handler = handle_btrace_clear_command,
		.mode = COMMAND_EXEC,
		.help = "clear btrace data",
		.usage = "",
	},
	{
		.name = "info",
		.handler = handle_btrace_info_command,
		.mode = COMMAND_EXEC,
		.help = "display btrace info",
		.usage = "",
	},
	COMMAND_REGISTRATION_DONE
};

COMMAND_HANDLER(handle_halt_group_command)
{
	if (CMD_ARGC < 2) {
		LOG_ERROR("This command expects at least 2 parameters");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	bool on_off;
	COMMAND_PARSE_BOOL(CMD_ARGV[0], on_off, "on", "off");

	struct target *target = get_current_target(CMD_CTX);
	int retval = ERROR_OK;
	for (int i = 1; i < CMD_ARGC; i++) {
		target = get_target(CMD_ARGV[i]);
		RISCV_INFO(r);
		if (r->dmi_write) {
			if (on_off)
				retval = r->dmi_write(target, DM_DMCS2, 0x7);
			else
				retval = r->dmi_write(target, DM_DMCS2, 0x0);
		}
	}

	return retval;
}

COMMAND_HANDLER(handle_resume_group_command)
{
	if (CMD_ARGC < 2) {
		LOG_ERROR("This command expects at least 2 parameters");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	bool on_off;
	COMMAND_PARSE_BOOL(CMD_ARGV[0], on_off, "on", "off");

	struct target *target = get_current_target(CMD_CTX);
	int retval = ERROR_OK;
	for (int i = 1; i < CMD_ARGC; i++) {
		target = get_target(CMD_ARGV[i]);
		RISCV_INFO(r);
		if (r->dmi_write) {
			if (on_off)
				retval = r->dmi_write(target, DM_DMCS2, 0x887);
			else
				retval = r->dmi_write(target, DM_DMCS2, 0x0);
		}
	}

	return retval;
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
		.name = "trace",
		.mode = COMMAND_ANY,
		.help = "Trace command group",
		.usage = "",
		.chain = trace_command_handlers,
	},
	{
		.name = "etrace",
		.mode = COMMAND_ANY,
		.help = "Etrace command group",
		.usage = "",
		.chain = etrace_command_handlers,
	},
	{
		.name = "btrace",
		.mode = COMMAND_ANY,
		.help = "Btrace command group",
		.usage = "",
		.chain = btrace_command_handlers,
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
