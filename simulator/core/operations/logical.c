/* Mulator - An extensible {ARM} {e,si}mulator
 * Copyright 2011-2012  Pat Pannuto <pat.pannuto@gmail.com>
 *
 * This file is part of Mulator.
 *
 * Mulator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Mulator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Mulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "opcodes.h"
#include "helpers.h"

#include "cpu/registers.h"
#include "cpu/misc.h"

static void and_imm(union apsr_t apsr, uint8_t setflags, uint8_t rd, uint8_t rn,
		uint32_t imm32, uint8_t carry) {
	uint32_t rn_val = CORE_reg_read(rn);

	uint32_t result = rn_val & imm32;
	if (rd == 15) {
		// ALUWritePC(result);
		CORE_ERR_not_implemented("ALUWritePC and_imm\n");
	} else {
		CORE_reg_write(rd, result);
		if (setflags) {
			apsr.bits.N = HIGH_BIT(result);
			apsr.bits.Z = result == 0;
			apsr.bits.C = carry;
			CORE_apsr_write(apsr);
		}
	}

	DBG2("and_imm done\n");
}

// arm-v7-m
static void and_imm_t1(uint32_t inst) {
	union apsr_t apsr = CORE_apsr_read();

	uint8_t imm8 = inst & 0xff;
	uint8_t rd = (inst & 0xf00) >> 8;
	uint8_t imm3 = (inst & 0x7000) >> 12;
	uint8_t rn = (inst & 0xf0000) >> 16;
	uint8_t S = !!(inst & 0x100000);
	uint8_t i = !!(inst & 0x04000000);

	if ((rd == 0xf) && (S == 1))
		CORE_ERR_not_implemented("add_imm_t1 --> TST (imm)\n");

	//d = uint(rd); n = uint(rd);
	uint8_t setflags = (S == 1);

	uint32_t imm32;
	bool carry;
	uint16_t imm12 = (i << 11) | (imm3 << 8) | imm8;
	ThumbExpandImm_C(imm12, apsr.bits.C, &imm32, &carry);

	DBG2("rd: %d, rn %d, imm12 0x%03x (%d)\n", rd, rn, imm12, imm12);

	if ( ((rd == 13) || ((rd == 15) && (S == 0))) || BadReg(rn) )
		CORE_ERR_unpredictable("Bad reg combo's in add_imm_t1\n");

	return and_imm(apsr, setflags, rd, rn, imm32, carry);
}

static void and_reg(uint8_t rd, uint8_t rn, uint8_t rm,
		bool setflags, enum SRType shift_t, uint8_t shift_n) {
	uint32_t rn_val = CORE_reg_read(rn);
	uint32_t rm_val = CORE_reg_read(rm);
	union apsr_t apsr = CORE_apsr_read();

	uint32_t shifted;
	bool carry_out;
	Shift_C(rm_val, 32, shift_t, shift_n, apsr.bits.C, &shifted, &carry_out);

	uint32_t result = rn_val & shifted;
	CORE_reg_write(rd, result);

	if (setflags) {
		apsr.bits.N = HIGH_BIT(result);
		apsr.bits.Z = result == 0;
		apsr.bits.C = carry_out;
		CORE_apsr_write(apsr);
	}
}

// arm-thumb
static void and_reg_t1(uint16_t inst) {
	uint8_t rdn = inst & 0x7;
	uint8_t rm  = (inst >> 3) & 0x7;

	uint8_t rd = rdn;
	uint8_t rn = rdn;
	bool setflags = !in_ITblock();
	enum SRType shift_t = SRType_LSL;
	uint8_t shift_n = 0;

	return and_reg(rd, rn, rm, setflags, shift_t, shift_n);
}

// arm-v7-m
static void and_reg_t2(uint32_t inst) {
	uint8_t rm = inst & 0xf;
	uint8_t type = (inst >> 4) & 0x3;
	uint8_t imm2 = (inst >> 6) & 0x3;
	uint8_t rd = (inst >> 8) & 0xf;
	uint8_t imm3 = (inst >> 12) & 0x7;
	uint8_t rn = (inst >> 16) & 0xf;
	bool S = (inst >> 20) & 0x1;

	bool setflags = S;

	enum SRType shift_t;
	uint8_t shift_n;
	uint8_t imm5 = (imm3 << 2) | imm2;
	DecodeImmShift(type, imm5, &shift_t, &shift_n);

	if ((rd == 13) || ((rd == 15) && (S == 0)) || BadReg(rn) || BadReg(rm))
		CORE_ERR_unpredictable("bad reg\n");

	return and_reg(rd, rn, rm, setflags, shift_t, shift_n);
}

static inline void bic_imm(union apsr_t apsr, uint8_t setflags,
		uint8_t rd, uint8_t rn, uint32_t imm32, uint8_t carry) {
	uint32_t result = CORE_reg_read(rn) & (~imm32);
	CORE_reg_write(rd, result);

	if (setflags) {
		apsr.bits.N = HIGH_BIT(result);
		apsr.bits.Z = result == 0;
		apsr.bits.C = carry;
		CORE_apsr_write(apsr);
	}
}

// arm-v7-m
static void bic_imm_t1(uint32_t inst) {
	uint8_t imm8 = inst & 0xff;
	uint8_t rd = (inst & 0xf00) >> 8;
	uint8_t imm3 = (inst & 0x7000) >> 12;
	uint8_t rn = (inst & 0xf0000) >> 16;
	uint8_t S = !!(inst & 0x100000);
	uint8_t i = !!(inst & 0x04000000);

	union apsr_t apsr = CORE_apsr_read();

	uint16_t imm12 = (i << 11) | (imm3 << 8) | imm8;
	uint32_t imm32;
	bool carry_out;
	ThumbExpandImm_C(imm12, apsr.bits.C, &imm32, &carry_out);

	if ((rd >= 13) || (rn >= 13))
		CORE_ERR_unpredictable("bic_imm_t1 bad reg\n");

	return bic_imm(apsr, S, rd, rn, imm32, carry_out);
}

static void bic_reg(uint8_t rd, uint8_t rn, uint8_t rm,
		bool setflags, enum SRType shift_t, uint8_t shift_n) {
	union apsr_t apsr = CORE_apsr_read();

	uint32_t shifted;
	bool carry_out;
	Shift_C(CORE_reg_read(rm), 32, shift_t, shift_n, apsr.bits.C,
			&shifted, &carry_out);

	uint32_t result = CORE_reg_read(rn) & ~shifted;
	CORE_reg_write(rd, result);

	if (setflags) {
		apsr.bits.N = HIGH_BIT(result);
		apsr.bits.Z = result == 0;
		apsr.bits.C = carry_out;
		CORE_apsr_write(apsr);
	}
}

// arm-thumb
static void bic_reg_t1(uint16_t inst) {
	uint8_t rdn = inst & 0x7;
	uint8_t rm = (inst >> 3) & 0x7;

	uint8_t rd = rdn;
	uint8_t rn = rdn;
	bool setflags = !in_ITblock();
	enum SRType shift_t = SRType_LSL;
	uint8_t shift_n = 0;

	return bic_reg(rd, rn, rm, setflags, shift_t, shift_n);
}

// arm-v7-m
static void bic_reg_t2(uint32_t inst) {
	uint8_t rm   = inst & 0xf;
	uint8_t type = (inst >> 4) & 0x3;
	uint8_t imm2 = (inst >> 6) & 0x3;
	uint8_t rd   = (inst >> 8) & 0xf;
	uint8_t imm3 = (inst >> 12) & 0x7;
	uint8_t rn   = (inst >> 16) & 0xf;
	bool    S    = (inst >> 20) & 0x1;

	bool setflags = S == 1;
	enum SRType shift_t;
	uint8_t shift_n;
	DecodeImmShift(type, imm2 | (imm3 << 2), &shift_t, &shift_n);

	if ((rd > 13) || (rn > 13) || (rm > 13))
		CORE_ERR_unpredictable("bic_reg_t2 case\n");

	return bic_reg(rd, rn, rm, setflags, shift_t, shift_n);
}

static void eor_imm(uint8_t rd, uint8_t rn,
		uint32_t imm32, bool carry, bool setflags, union apsr_t apsr) {
	uint32_t rn_val = CORE_reg_read(rn);

	uint32_t result = rn_val ^ imm32;
	CORE_reg_write(rd, result);

	if (setflags) {
		apsr.bits.N = HIGH_BIT(result);
		apsr.bits.Z = result == 0;
		apsr.bits.C = carry;
		CORE_apsr_write(apsr);
	}
}

// arm-v7-m
static void eor_imm_t1(uint32_t inst) {
	uint8_t imm8 = inst & 0xff;
	uint8_t rd = (inst >> 8) & 0xf;
	uint8_t imm3 = (inst >> 12) & 0x7;
	uint8_t rn = (inst >> 16) & 0xf;
	bool S = !!(inst & 0x100000);
	bool i = !!(inst & 0x04000000);

	bool setflags = S;

	union apsr_t apsr = CORE_apsr_read();
	uint32_t imm32;
	bool carry;
	ThumbExpandImm_C((i << 11) | (imm3 << 8) | imm8, apsr.bits.C, &imm32, &carry);

	if ((rd == 13) || ((rd == 15) && (S == 0)) || BadReg(rn))
		CORE_ERR_unpredictable("bad reg\n");

	return eor_imm(rd, rn, imm32, carry, setflags, apsr);
}

static void eor_reg(uint8_t setflags, uint8_t rd, uint8_t rn, uint8_t rm,
		enum SRType shift_t, uint8_t shift_n) {
	uint32_t result;
	bool carry_out;

	union apsr_t apsr = CORE_apsr_read();

	Shift_C(CORE_reg_read(rm), 32, shift_t, shift_n, apsr.bits.C, &result, &carry_out);

	result = CORE_reg_read(rn) ^ result;
	CORE_reg_write(rd, result);

	if (setflags) {
		apsr.bits.N = HIGH_BIT(result);
		apsr.bits.Z = result == 0;
		apsr.bits.C = carry_out;
		CORE_apsr_write(apsr);
	}
}

// arm-thumb
static void eor_reg_t1(uint16_t inst) {
	uint8_t rdn = inst & 0x7;
	uint8_t rm  = (inst >> 3) & 0x7;

	uint8_t rd = rdn;
	uint8_t rn = rdn;
	bool setflags = !in_ITblock();
	enum SRType shift_t = SRType_LSL;
	uint8_t shift_n = 0;

	return eor_reg(setflags, rd, rn, rm, shift_t, shift_n);
}

// arm-v7-m
static void eor_reg_t2(uint32_t inst) {
	uint8_t rm = (inst & 0xf);
	uint8_t type = (inst & 0x30) >> 4;
	uint8_t imm2 = (inst & 0xc0) >> 6;
	uint8_t rd = (inst & 0xf00) >> 8;
	uint8_t imm3 = (inst & 0x7000) >> 12;
	uint8_t rn = (inst & 0xf0000) >> 16;
	uint8_t S = !!(inst & 0x100000);

	if ((rd == 0xf) && (S))
		CORE_ERR_unpredictable("eor_reg_t2 -> TEQ\n");

	enum SRType shift_t;
	uint8_t shift_n;
	uint8_t imm5 = (imm3 << 2) | imm2;
	DecodeImmShift(type, imm5, &shift_t, &shift_n);

	if ((rd == 13) || ((rd == 15) && (S == 0)) || (rn >= 13) || (rm >= 13)) {
		CORE_ERR_unpredictable("eor_reg_t2 bad reg\n");
	}

	return eor_reg(S, rd, rn, rm, shift_t, shift_n);
}

static inline void mvn_imm(uint8_t rd, bool setflags, union apsr_t apsr,
		uint32_t imm32, bool carry) {
	uint32_t result = ~imm32;
	CORE_reg_write(rd, result);
	if (setflags) {
		apsr.bits.N = HIGH_BIT(result);
		apsr.bits.Z = result == 0;
		apsr.bits.C = carry;
		CORE_apsr_write(apsr);
	}
}

// arm-v7-m
static void mvn_imm_t1(uint32_t inst) {
	uint8_t imm8 = inst & 0xff;
	uint8_t rd   = (inst >> 8) & 0xf;
	uint8_t imm3 = (inst >> 12) & 0x7;
	bool    S    = (inst >> 20) & 0x1;
	bool    i    = (inst >> 26) & 0x1;

	bool setflags = S==1;

	union apsr_t apsr = CORE_apsr_read();
	uint32_t imm32;
	bool carry;
	ThumbExpandImm_C(imm8 | (imm3 << 8) | (i << 11), apsr.bits.C,
			&imm32, &carry);

	if (rd > 13)
		CORE_ERR_unpredictable("mvn_imm_t1 case\n");

	return mvn_imm(rd, setflags, apsr, imm32, carry);
}

static void mvn_reg(uint8_t setflags, uint8_t rd, uint8_t rm,
		enum SRType shift_t, uint8_t shift_n) {
	uint32_t result;
	bool carry_out;

	union apsr_t apsr = CORE_apsr_read();

	Shift_C(CORE_reg_read(rm), 32, shift_t, shift_n, apsr.bits.C, &result, &carry_out);

	result = ~result;
	CORE_reg_write(rd, result);

	if (setflags) {
		apsr.bits.N = HIGH_BIT(result);
		apsr.bits.Z = result == 0;
		apsr.bits.C = carry_out;
		CORE_apsr_write(apsr);
	}
}

// arm-thumb
static void mvn_reg_t1(uint16_t inst) {
	uint8_t rd = inst & 0x7;
	uint8_t rm = (inst >> 3) & 0x7;

	bool setflags = !in_ITblock();
	enum SRType shift_t = LSL;
	uint8_t shift_n = 0;

	return mvn_reg(setflags, rd, rm, shift_t, shift_n);
}

// arm-v7-m
static void mvn_reg_t2(uint32_t inst) {
	uint8_t rm = (inst & 0xf);
	uint8_t type = (inst & 0x30) >> 4;
	uint8_t imm2 = (inst & 0xc0) >> 6;
	uint8_t rd = (inst & 0xf00) >> 8;
	uint8_t imm3 = (inst & 0x7000) >> 12;
	uint8_t S = !!(inst & 0x100000);

	enum SRType shift_t;
	uint8_t shift_n;
	uint8_t imm5 = (imm3 << 2) | imm2;
	DecodeImmShift(type, imm5, &shift_t, &shift_n);

	if ((rd >= 13) || (rm >= 13)) {
		CORE_ERR_unpredictable("mvn_reg_t2 bad reg\n");
	}

	return mvn_reg(S, rd, rm, shift_t, shift_n);
}

static void orr(uint16_t inst) {
	uint8_t rm = (inst & 0x38) >> 3;
	uint8_t rd = (inst & 0x7) >> 0;

	uint32_t result;
	result = CORE_reg_read(rd) | CORE_reg_read(rm);
	CORE_reg_write(rd, result);

	union apsr_t apsr = CORE_apsr_read();

	if (!in_ITblock()) {
		apsr.bits.N = HIGH_BIT(result);
		apsr.bits.Z = result == 0;
		CORE_apsr_write(apsr);
	}

	DBG2("orrs r%02d, r%02d\n", rd, rm);
}

static void orr_reg(uint8_t setflags, uint8_t rd, uint8_t rn, uint8_t rm,
		enum SRType shift_t, uint8_t shift_n) {
	uint32_t result;
	bool carry_out;

	union apsr_t apsr = CORE_apsr_read();

	Shift_C(CORE_reg_read(rm), 32, shift_t, shift_n, apsr.bits.C, &result, &carry_out);

	result = CORE_reg_read(rn) | result;
	CORE_reg_write(rd, result);

	if (setflags) {
		apsr.bits.N = HIGH_BIT(result);
		apsr.bits.Z = result == 0;
		apsr.bits.C = carry_out;
		CORE_apsr_write(apsr);
	}

	DBG2("orr_reg ran\n");
}

static void orr_reg_t2(uint32_t inst) {
	uint8_t rm = (inst & 0xf);
	uint8_t type = (inst & 0x30) >> 4;
	uint8_t imm2 = (inst & 0xc0) >> 6;
	uint8_t rd = (inst & 0xf00) >> 8;
	uint8_t imm3 = (inst & 0x7000) >> 12;
	uint8_t rn = (inst & 0xf0000) >> 16;
	uint8_t S = !!(inst & 0x100000);

	if (rn == 0xf)
		CORE_ERR_unpredictable("orr_reg_t2 -> MOV\n");

	enum SRType shift_t;
	uint8_t shift_n;
	uint8_t imm5 = (imm3 << 2) | imm2;
	DecodeImmShift(type, imm5, &shift_t, &shift_n);

	if ((rd >= 13) || (rn == 13) || (rm >= 13))
		CORE_ERR_unpredictable("orr_reg_t2 bad reg\n");

	return orr_reg(S, rd, rn, rm, shift_t, shift_n);
}

static void tst_imm(union apsr_t apsr, uint8_t rn, uint32_t imm32, bool carry) {
	uint32_t rn_val = CORE_reg_read(rn);

	uint32_t result = rn_val & imm32;
	apsr.bits.N = HIGH_BIT(result);
	apsr.bits.Z = result == 0;
	apsr.bits.C = carry;
	CORE_apsr_write(apsr);
}

static void tst_imm_t1(uint32_t inst) {
	uint8_t imm8 = inst & 0xff;
	uint8_t imm3 = (inst >> 12) & 0x7;
	uint8_t rn = (inst >> 16) & 0xf;
	bool i = !!(inst & 0x04000000);

	uint16_t imm12 = (i << 11) | (imm3 << 8) | (imm8);
	uint32_t imm32;
	bool carry;

	union apsr_t apsr = CORE_apsr_read();

	ThumbExpandImm_C(imm12, apsr.bits.C, &imm32, &carry);

	if ((rn == 13) || (rn == 15))
		CORE_ERR_unpredictable("bad reg\n");

	tst_imm(apsr, rn, imm32, carry);
}

__attribute__ ((constructor))
void register_opcodes_logical(void) {
	// and_imm_t1: 1111 0x00 000x xxxx 0<x's>
	register_opcode_mask_32_ex(0xf0000000, 0x0be08000, and_imm_t1,
			0x00100f00, 0x0,
			0, 0);

	// and_reg_t1: 0100 0000 00xx xxxx
	register_opcode_mask_16(0x4000, 0xbfc0, and_reg_t1);

	// and_reg_t2: 1110 1010 000x xxxx 0xxx xxxx xxxx xxxx
	register_opcode_mask_32_ex(0xea000000, 0x15e08000, and_reg_t2,
			0x00100f00, 0x0,
			0, 0);

	// bic_imm_t1: 1111 0x00 001x xxxx 0<x's>
	register_opcode_mask_32(0xf0200000, 0x0bc08000, bic_imm_t1);

	// bic_reg_t1: 0100 0011 10<x's>
	register_opcode_mask_16(0x4380, 0xbc40, bic_reg_t1);

	// bic_reg_t2: 1110 1010 001x xxxx 0<x's>
	register_opcode_mask_32(0xea200000, 0x15c08000, bic_reg_t2);

	// eor_imm_t1: 1111 0x00 100x xxxx 0<x's>
	register_opcode_mask_32_ex(0xf0800000, 0x0b608000, eor_imm_t1,
			0x100f00, 0x0,
			0, 0);

	// eor_reg_t1: 0100 0000 01xx xxxx
	register_opcode_mask_16(0x4040, 0xbf80, eor_reg_t1);

	// eor_reg_t2: 1110 1010 100x xxxx 0<x's>
	register_opcode_mask_32(0xea800000, 0x15608000, eor_reg_t2);

	// mvn_imm_t1: 1111 0x00 011x 1111 0<x's>
	register_opcode_mask_32(0xf06f0000, 0x0b808000, mvn_imm_t1);

	// mvn_reg_t1: 0100 0011 11xx xxxx
	register_opcode_mask_16(0x43c0, 0xbc00, mvn_reg_t1);

	// mvn_reg_t2: 1110 1010 011x 1111 0<x's>
	register_opcode_mask_32(0xea6f0000, 0x15808000, mvn_reg_t2);

	// orr: 0100 0011 00<x's>
	register_opcode_mask_16(0x4300, 0xbcc0, orr);

	// Cannot allow               1111 case
	// orr_reg_t2: 1110 1010 010x xxxx 0<x's>
	register_opcode_mask_32_ex(0xea400000, 0x15a08000, orr_reg_t2,
			0x000f0000, 0x00000000,
			0, 0);

	// tst_imm_t1: 1111 0x00 0001 xxxx 0xxx 1111 xxxx xxxx
	register_opcode_mask_32(0xf0100f00, 0x0be08000, tst_imm_t1);
}
