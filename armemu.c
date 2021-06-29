#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>

#include "armemu.h"

void armemu_init(struct arm_state *asp, uint32_t *fp,
                 uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    int i;
    
    /* Zero out registers */
    for (i = 0; i < NREGS; i += 1) {
        asp->regs[i] = 0;
    }

    /* Zero out the cpsr */
    asp->cpsr = 0;

    /* Zero out the stack */
    for (i = 0; i < STACK_SIZE; i += 1) {
        asp->stack[i] = 0;
    }

    /* Initialize the PC */
    asp->regs[PC] = (uint32_t) fp;

    /* Initialize LR */
    asp->regs[LR] = 0;

    /* Initialize SP */
    asp->regs[SP] = (uint32_t) &asp->stack[STACK_SIZE];

    /* Initialize the first 4 arguments */
    asp->regs[0] = a0;
    asp->regs[1] = a1;
    asp->regs[2] = a2;
    asp->regs[3] = a3;
}


bool armemu_is_bx(uint32_t iw) {
    uint32_t bxcode;

    bxcode = (iw >> 4) & 0xFFFFFF;

    /* 0x12fff1 = 0b000100101111111111110001 */
    return bxcode == 0x12fff1;   
}

void armemu_bx(struct arm_state *asp, uint32_t iw) {
    uint32_t rn;

    rn = iw & 0b1111;

    asp->regs[PC] = asp->regs[rn];
}

void armemu_add(struct arm_state *asp, uint32_t rd, uint32_t oper1, uint32_t oper2) {
	asp->regs[rd] = oper1 + oper2;
}

void armemu_sub(struct arm_state *asp, uint32_t rd, uint32_t oper1, uint32_t oper2) {
	asp->regs[rd] = oper1 - oper2;
}

void armemu_mov(struct arm_state *asp, uint32_t rd, uint32_t oper2) {
    asp->regs[rd] = oper2;
}

void armemu_cmp(struct arm_state *asp, uint32_t oper1, uint32_t oper2) {
	int result = oper1 - oper2;
	uint32_t z = 0;
	uint32_t n = 0;
	uint32_t c = 0;
	uint32_t v = 0;

	int si_oper1 = (int) oper1;
	int si_oper2 = (int) oper2;
	int si_r = si_oper1 - si_oper2;
	
    uint32_t si_oper1_neg = si_oper1 < 0;
    uint32_t si_oper2_neg = si_oper2 < 0;
    uint32_t si_r_neg = si_r < 0;
    
    if(result == 0) z = 1;
    if(result < 0) n = 1;
    if(oper2 > oper1) c = 1;
    if((si_oper1_neg != si_oper2_neg) && ((si_oper2_neg && si_r_neg) || (!si_oper2_neg && !si_r_neg))) v = 1;
    
    uint32_t flag = (z << 3) + (n << 2) + (c << 1) + v;
    
    asp->cpsr = flag;
}

bool armemu_is_data_processing(uint32_t iw) {
    uint32_t dp_bits;

    dp_bits = (iw >> 26) & 0b11;

    return (dp_bits == 0b00);
}

void armemu_data_processing(struct arm_state *asp, uint32_t iw) {
    uint32_t i_bit;
    uint32_t rn;
    uint32_t rd;
    uint32_t rm;
    uint32_t imm;
    uint32_t oper2;
    uint32_t opcode;
    
    i_bit = (iw >> 25) & 0b1;
    rn = (iw >> 16) & 0b1111;
    rd = (iw >> 12) & 0b1111;
    rm = iw & 0b1111;
    imm = iw & 0b11111111;
    opcode = (iw >> 21) & 0b1111;

    if (!i_bit) {
        oper2 = asp->regs[rm];
    } else {
        oper2 = imm;
    }

    if(opcode == 0b0100) armemu_add(asp, rd, asp->regs[rn], oper2);
    if(opcode == 0b0010) armemu_sub(asp, rd, asp->regs[rn], oper2);
    if(opcode == 0b1010) armemu_cmp(asp, asp->regs[rn], oper2);
    if(opcode == 0b1101) armemu_mov(asp, rd, oper2);
    
    if (rd != PC) {
        asp->regs[PC] = asp->regs[PC] + 4;
    }
}

bool armemu_is_b(uint32_t iw) {
    uint32_t dp_bits;

    dp_bits = (iw >> 25) & 0b111;
    
    return dp_bits == 0b101;
}

void armemu_b(struct arm_state *asp, uint32_t iw) {
    uint32_t offset;
    uint32_t l_bit;
    uint32_t cond;

    cond = (iw >> 28) & 0b1111;
    offset = iw & 0b111111111111111111111111;
    uint32_t sign = (offset >> 23) & 0b1;
    
    l_bit = (iw >> 24) & 0b1;
    offset = offset << 2;
    if(sign) {
    	offset = offset | 0xFF000000;
    }
    offset += 8;

    if(l_bit) {
    	asp->regs[LR] = asp->regs[PC] + 4;
    }

    uint32_t flag = asp->cpsr;
    uint32_t z = flag >> 3 & 0b1;
    uint32_t n = flag >> 2 & 0b1;
    uint32_t c = flag >> 1 & 0b1;
    uint32_t v = flag & 0b1;

    if((cond == 0b0000 && z)
        || (cond == 0b0001 && !z)
        || (cond == 0b1010 && n == v)
        || (cond == 0b1011 && n != v)
        || (cond == 0b1100 && !z && n == v)
        || (cond == 0b1101 && (z || n != v))
        || (cond == 0b1110)) {
    	asp->regs[PC] = asp->regs[PC] + offset;
    } else {
    	asp->regs[PC] = asp->regs[PC] + 4;
    }
}

bool armemu_is_single_data_transfer(uint32_t iw) {
    uint32_t dp_bits;

    dp_bits = (iw >> 26) & 0b11;
    
    return (dp_bits == 0b01);
}

void armemu_single_data_transfer(struct arm_state *asp, uint32_t iw) {
    uint32_t i_bit;
    uint32_t rn;
    uint32_t rd;
    uint32_t rm;
    uint32_t imm;
    uint32_t oper2;
    uint32_t ls_bit;
    uint32_t p_index_bit;
    uint32_t ud_bit;
    uint32_t bw_bit;
    uint32_t wb_bit;
    uint32_t shift;
    uint32_t offset;
    
    i_bit = (iw >> 25) & 0b1;
    rn = (iw >> 16) & 0b1111;
    rd = (iw >> 12) & 0b1111;
    rm = iw & 0b1111;
    shift = (iw >> 7) & 0b11111;
    imm = iw & 0b11111111;
    ls_bit = (iw >> 20) & 0b1;
    p_index_bit = (iw >> 24) & 0b1;
    ud_bit = (iw >> 23) & 0b1;
    bw_bit = (iw >> 22) & 0b1;
    wb_bit = (iw >> 21) & 0b1;

    if(i_bit) {
    	offset = asp->regs[rm] << shift;	
    } else {
    	offset = imm;
    }

    if(ls_bit) {
    	asp->regs[rd] = *(uint32_t *)(asp->regs[rn] + offset);
    } else {
    	*(uint32_t *)(asp->regs[rn] + offset) = asp->regs[rd];
    }
    
    if (rd != PC) {
        asp->regs[PC] = asp->regs[PC] + 4;
    }
}

void armemu_one(struct arm_state *asp) {
    uint32_t iw;

    /*
    uint32_t *pc;
    pc = (uint32_t *) asp->regs[PC];
    iw = *pc;
    */
    
    iw = *((uint32_t *) asp->regs[PC]);

    /* Order matters: more constrained to least constrained */
    if (armemu_is_bx(iw)) {
        armemu_bx(asp, iw);
    }else if (armemu_is_b(iw)) {
        armemu_b(asp, iw);
    } else if (armemu_is_data_processing(iw)) {
        armemu_data_processing(asp, iw);
    } else if (armemu_is_single_data_transfer(iw)) {
        armemu_single_data_transfer(asp, iw);
    } else {
        printf("armemu_one() invalid instruction\n");
        exit(-1);
    }
}

int armemu(struct arm_state *asp) {
    while (asp->regs[PC] != 0) {
        armemu_one(asp);
    }
    
    return (int) asp->regs[0];
}
