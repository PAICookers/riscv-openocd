# SPDX-License-Identifier: GPL-2.0-or-later

proc get_register {reg} {
	set reg_info [get_reg $reg]
	return [lindex $reg_info 1]
}

proc get_memory {addr} {
	return [read_memory $addr 32 1]
}

proc extract_field {val which} {
	set part_a [expr $val&$which]
	set part_b [expr $which&[expr ~[expr $which-1]]]
	return [expr $part_a/$part_b]
}

proc pow_of_two {n} {
	return [expr 1<<$n]
}

proc linesz {n} {
	switch $n {
		"0" {
			return 0
		}
		default {
			pow_of_two [expr $n-1]
		}
	}
}

proc show_safety_mechanism {safetyMode} {
	switch $safetyMode {
		"0" {
			puts -nonewline " No-Safety-Mechanism"
		}
		"1" {
			puts -nonewline " Lockstep"
		}
		"2" {
			puts -nonewline " Lockstep+SplitMode"
		}
		"3" {
			puts -nonewline " ASIL-B"
		}
	}
}

proc show_vpu_degree {degree} {
	switch $degree {
		"0" {
			puts -nonewline " DLEN=VLEN/2"
		}
		"1" {
			puts -nonewline " DLEN=VLEN"
		}
	}
}

proc format_display {bytes} {
	if [expr $bytes/[expr 1024*1024*1024]] {
		puts -nonewline [format " %dGB" [expr $bytes/[expr 1024*1024*1024]]]
	} elseif [expr $bytes/[expr 1024*1024]] {
		puts -nonewline [format " %dMB" [expr $bytes/[expr 1024*1024]]]
	} elseif [expr $bytes/[expr 1024]] {
		puts -nonewline [format " %dKB" [expr $bytes/[expr 1024]]]
	} else {
		puts -nonewline [format " %dByte" $bytes]
	}
}

proc show_cache_info {set way lsize ecc} {
	format_display [expr $set*$way*$lsize]
	puts -nonewline [format "(set=%d," $set]
	puts -nonewline [format "way=%d," $way]
	puts -nonewline [format "lsize=%d," $lsize]
	puts -nonewline [format "ecc=%d)" [expr !!$ecc]]
}

proc nuclei_cpuinfo {} {

	set riscv_info [riscv info]
	dict for {key value} $riscv_info {
		if {$key == "hart.xlen"} {
			set xlen $value
		}
	}

	puts "-----Nuclei RISC-V CPU Configuration Information-----"

	set marchid [get_register marchid]
	if {$marchid != 0} {
		puts [format "         MARCHID: %lx" $marchid]
	}

	set mimpid [get_register mimpid]
	if {$mimpid != 0} {
		puts [format "          MIMPID: %lx" $mimpid]
	}

	set misa [get_register misa]
	if {$misa != 0} {
		puts -nonewline "             ISA:"
		if {$xlen == 32} {
			puts -nonewline " RV32"
		} else {
			puts -nonewline " RV64"
		}
		for {set i 0}  {$i < 26} {incr i} {
			if [expr 1&[expr $misa>>$i]] {
				if {88 == [expr 65+$i]} {
					puts -nonewline " NICE"
				} else {
					puts -nonewline [format " %c" [expr 65+$i]]
				}
			}
		}
	}

	set mcfg_info [get_register mcfg_info]
	if [expr $mcfg_info&[expr 1<<12]] {
		puts -nonewline " Xxldspn1x"
	}
	if [expr $mcfg_info&[expr 1<<13]] {
		puts -nonewline " Xxldspn2x"
	}
	if [expr $mcfg_info&[expr 1<<14]] {
		puts -nonewline " Xxldspn3x"
	}
	if [expr $mcfg_info&[expr 1<<15]] {
		puts -nonewline " Zc Xxlcz"
	}
	if [expr $mcfg_info&[expr 1<<19]] {
		puts -nonewline " Smwg"
	}
	puts ""

	if {$mcfg_info != 0} {
		puts -nonewline "            MCFG:"
		if [expr $mcfg_info&[expr 1<<0]] {
			puts -nonewline " TEE"
		}
		if [expr $mcfg_info&[expr 1<<1]] {
			puts -nonewline " ECC"
		}
		if [expr $mcfg_info&[expr 1<<2]] {
			puts -nonewline " ECLIC"
		}
		if [expr $mcfg_info&[expr 1<<3]] {
			puts -nonewline " PLIC"
		}
		if [expr $mcfg_info&[expr 1<<4]] {
			puts -nonewline " FIO"
		}
		if [expr $mcfg_info&[expr 1<<5]] {
			puts -nonewline " PPI"
		}
		if [expr $mcfg_info&[expr 1<<6]] {
			puts -nonewline " NICE"
		}
		if [expr $mcfg_info&[expr 1<<7]] {
			puts -nonewline " ILM"
		}
		if [expr $mcfg_info&[expr 1<<8]] {
			puts -nonewline " DLM"
		}
		if [expr $mcfg_info&[expr 1<<9]] {
			puts -nonewline " ICACHE"
		}
		if [expr $mcfg_info&[expr 1<<10]] {
			puts -nonewline " DCACHE"
		}
		if [expr $mcfg_info&[expr 1<<11]] {
			puts -nonewline " SMP"
		}
		if [expr $mcfg_info&[expr 1<<12]] {
			puts -nonewline " DSP-N1"
		}
		if [expr $mcfg_info&[expr 1<<13]] {
			puts -nonewline " DSP-N2"
		}
		if [expr $mcfg_info&[expr 1<<14]] {
			puts -nonewline " DSP-N3"
		}
		if [expr $mcfg_info&[expr 1<<15]] {
			puts -nonewline " ZC_XLCZ_EXT"
		}
		if [expr $mcfg_info&[expr 1<<16]] {
			puts -nonewline " IREGION"
		}
		if [expr $mcfg_info&[expr 1<<19]] {
			puts -nonewline " SEC_MODE"
		}
		if [expr $mcfg_info&[expr 1<<20]] {
			puts -nonewline " ETRACE"
		}
		if [expr $mcfg_info&[expr 1<<23]] {
			puts -nonewline " VNICE"
		}
		show_safety_mechanism [extract_field $mcfg_info [expr 0x3<<21]]
		show_vpu_degree [extract_field $mcfg_info [expr 0x3<<17]]
		puts ""

		if [expr $mcfg_info&[expr 1<<7]] {
			set micfg_info [get_register micfg_info]
			if {$micfg_info != 0} {
				puts -nonewline "             ILM:"
				format_display [expr [pow_of_two [expr [extract_field $micfg_info [expr 0x1F<<16]]-1]]*256]
				if [expr $micfg_info&[expr 1<<21]] {
					puts -nonewline " execute-only"
				}
				if [expr $micfg_info&[expr 1<<22]] {
					puts -nonewline " has-ecc"
				}
				puts ""
			}
		}

		if [expr $mcfg_info&[expr 1<<8]] {
			set mdcfg_info [get_register mdcfg_info]
			if {$mdcfg_info != 0} {
				puts -nonewline "             DLM:"
				format_display [expr [pow_of_two [expr [extract_field $mdcfg_info [expr 0x1F<<16]]-1]]*256]
				if [expr $mdcfg_info&[expr 1<<21]] {
					puts -nonewline " has-ecc"
				}
				puts ""
			}
		}

		if [expr $mcfg_info&[expr 1<<9]] {
			set micfg_info [get_register micfg_info]
			if {$micfg_info != 0} {
				puts -nonewline "          ICACHE:"
				show_cache_info [pow_of_two [expr [extract_field $micfg_info 0xF]+3]] \
								[expr [extract_field $micfg_info [expr 7<<4]]+1] \
								[pow_of_two [expr [extract_field $micfg_info [expr 7<<7]]+2]] \
								[extract_field $mcfg_info [expr 0x1<<1]]
				puts ""
			}
		}

		if [expr $mcfg_info&[expr 1<<10]] {
			set mdcfg_info [get_register mdcfg_info]
			if {$mdcfg_info != 0} {
				puts -nonewline "          DCACHE:"
				show_cache_info [pow_of_two [expr [extract_field $mdcfg_info 15]+3]] \
								[expr [extract_field $mdcfg_info [expr 7<<4]]+1] \
								[pow_of_two [expr [extract_field $mdcfg_info [expr 7<<7]]+2]] \
								[extract_field $mcfg_info [expr 0x1<<1]]
				puts ""
			}
		}

		if [expr $mcfg_info&[expr 1<<3]] {
			set mtlbcfg_info [get_register mtlbcfg_info]
			puts -nonewline "             TLB:"
			puts -nonewline [format " MainTLB(set=%lu" [pow_of_two [expr [extract_field $mtlbcfg_info 0xF]+3]]]
			puts -nonewline [format " way=%lu" [expr [extract_field $mtlbcfg_info [expr 0x7<<4]]+1]]
			puts -nonewline [format " entry=%lu" [linesz [extract_field $mtlbcfg_info [expr 0x7<<7]]]]
			puts -nonewline [format " ecc=%lu)" [extract_field $mtlbcfg_info [expr 0x1<<10]]]
			puts -nonewline [format " ITLB(entry=%lu)" [linesz [extract_field $mtlbcfg_info [expr 0x7<<16]]]]
			puts -nonewline [format " DTLB(entry=%lu)" [linesz [extract_field $mtlbcfg_info [expr 0x7<<19]]]]
			puts ""
		}

		if [expr $mcfg_info&[expr 1<<16]] {
			set mirgb_info [get_register mirgb_info]
			puts -nonewline "         IREGION:"
			set iregion_base [expr $mirgb_info&[expr ~0x3FF]]
			puts -nonewline [format " 0x%lx" $iregion_base]
			format_display [expr [pow_of_two [expr [extract_field $mirgb_info [expr 0x1F<<1]]-1]]*1024]
			puts ""
			puts "                  Unit        Size        Address"
			puts [format "                  INFO        64KB        0x%lx" $iregion_base]
			puts [format "                  DEBUG       64KB        0x%lx" [expr $iregion_base+0x10000]]
			if [expr $mcfg_info&[expr 1<<2]] {
				puts [format "                  ECLIC       64KB        0x%lx" [expr $iregion_base+0x20000]]
			}
			puts [format "                  TIMER       64KB        0x%lx" [expr $iregion_base+0x30000]]
			if [expr $mcfg_info&[expr 1<<11]] {
				puts [format "                  SMP         64KB        0x%lx" [expr $iregion_base+0x40000]]
			}
			set smp_cfg [get_memory [expr $iregion_base+0x40004]]
			if [expr $mcfg_info&[expr 1<<2]] {
				if {[extract_field $smp_cfg [expr 31<<1]] >= 2} {
					puts [format "                  CIDU        64KB        0x%lx" [expr $iregion_base+0x50000]]
				}
			}
			if [expr $mcfg_info&[expr 1<<3]] {
				puts [format "                  PLIC        64MB        0x%lx" [expr $iregion_base+0x4000000]]
			}

			if [expr $mcfg_info&[expr 1<<11]] {
				puts -nonewline "         SMP_CFG:"
				puts -nonewline [format " CC_PRESENT=%ld" [extract_field $smp_cfg 0x1]]
				puts -nonewline [format " SMP_CORE_NUM=%ld" [extract_field $smp_cfg [expr 0x3F<<1]]]
				puts -nonewline [format " IOCP_NUM=%ld" [extract_field $smp_cfg [expr 0x3F<<7]]]
				puts [format " PMON_NUM=%ld" [extract_field $smp_cfg [expr 0x3F<<13]]]
			}

			if [expr $mcfg_info&[expr 1<<2]] {
				set clic_base [expr $iregion_base+0x20000]
				set clic_cfg [expr $clic_base+0x0]
				set clic_info [expr $clic_base+0x4]
				set clic_mth [expr $clic_base+0x8]
				set clic_cfg_value [get_memory $clic_cfg]
				set clic_info_value [get_memory $clic_info]
				set clic_mth_value [get_memory $clic_mth]
				puts -nonewline "         ECLIC:"
				puts -nonewline [format " VERSION=0x%x" [extract_field $clic_info_value [expr 0xFF<<13]]]
				puts -nonewline [format " NUM_INTERRUPT=%x" [extract_field $clic_info_value [expr 0xFFF<<0]]]
				puts -nonewline [format " CLICINTCTLBITS=%x" [extract_field $clic_info_value [expr 0xF<<21]]]
				puts -nonewline [format " MTH=%x" [extract_field $clic_mth_value [expr 0xFF<<24]]]
				puts [format " NLBITS=%x" [extract_field $clic_cfg_value [expr 0xF<<1]]]
			}

			if [expr $smp_cfg&1] {
				puts -nonewline "         L2CACHE:"
				set cc_cfg [get_memory [expr $iregion_base+0x40008]]
				show_cache_info [pow_of_two [extract_field $cc_cfg 0xF]] \
								[expr [extract_field $cc_cfg [expr 0xF<<4]]+1] \
								[pow_of_two [expr [extract_field $cc_cfg [expr 0x7<<8]]+2]] \
								[extract_field $cc_cfg [expr 0x1<<11]]
				puts ""
			}

			puts "     INFO-Detail:"
			set mpasize [get_memory $iregion_base]
			puts [format "                  mpasize : %ld" $mpasize]
			set cmo_info [get_memory [expr $iregion_base+4]]
			if [expr $cmo_info&1] {
				puts [format "                  cbozero : %dByte" \
					[pow_of_two [expr [extract_field $cmo_info [expr 0xF<<6]]+2]]]
				puts [format "                  cmo     : %dByte" \
					[pow_of_two [expr [extract_field $cmo_info [expr 0xF<<2]]+2]]]
				if [expr $cmo_info&2] {
					puts "                  has_prefecth"
				}
			}
			set mcppi_cfg_lo [get_memory [expr $iregion_base+0x80]]
			set mcppi_cfg_hi [get_memory [expr $iregion_base+0x84]]
			if [expr $mcppi_cfg_lo&1] {
				if {$xlen == 32} {
					puts -nonewline [format "                  cppi    : 0x%lx" \
						[expr $mcppi_cfg_lo&[expr ~0x3FF]]]
				} else {
					puts -nonewline [format "                  cppi    : 0x%lx" \
						[expr [expr $mcppi_cfg_hi<<32]|[expr $mcppi_cfg_lo&[expr ~0x3FF]]]]
				}
				format_display [expr [pow_of_two [expr [extract_field $mcppi_cfg_lo [expr 0x1F<<1]-1]]]*1024]
				puts " "
			}
			puts ""
		}

		if [expr $mcfg_info&[expr 1<<4]] {
			set mfiocfg_info [get_register mfiocfg_info]
			puts -nonewline "             FIO:"
			puts -nonewline [format " 0x%lx" [expr $mfiocfg_info&[expr ~0x3FF]]]
			format_display [expr [pow_of_two [expr [extract_field $mfiocfg_info [expr 0x1F<<1]]-1]]*1024]
			puts ""
		}

		if [expr $mcfg_info&[expr 1<<5]] {
			set mppicfg_info [get_register mppicfg_info]
			puts -nonewline "             PPI:"
			puts -nonewline [format " 0x%lx" [expr $mppicfg_info&[expr ~0x3FF]]]
			format_display [expr [pow_of_two [expr [extract_field $mppicfg_info [expr 0x1F<<1]]-1]]*1024]
			puts ""
		}
	}

	puts "----End of cpuinfo-----"
}
