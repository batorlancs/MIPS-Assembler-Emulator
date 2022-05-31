#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

#include "emulator.h"

#define XSTR(x) STR(x)		//can be used for MAX_ARG_LEN in sscanf
#define STR(x) #x

#define ADDR_TEXT    0x00400000 //where the .text area starts in which the program lives
#define TEXT_POS(a)  ((a==ADDR_TEXT)?(0):(a - ADDR_TEXT)/4) //can be used to access text[]
#define ADDR_POS(j)  (j*4 + ADDR_TEXT)                      //convert text index to address


const char *register_str[] = {"$zero",
                              "$at", "$v0", "$v1",
                              "$a0", "$a1", "$a2", "$a3",
                              "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
                              "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
                              "$t8", "$t9",
                              "$k0", "$k1",
                              "$gp",
                              "$sp", "$fp", "$ra"};

/* Space for the assembler program */
char prog[MAX_PROG_LEN][MAX_LINE_LEN];
int prog_len = 0;

/* Elements for running the emulator */
unsigned int registers[MAX_REGISTER] = {0}; // the registers
unsigned int pc = 0;                        // the program counter
unsigned int text[MAX_PROG_LEN] = {0}; // the text memory with our instructions

/* function to create bytecode for instruction nop
   conversion result is passed in bytecode
   function always returns 0 (conversion OK) */
typedef int (*opcode_function)(unsigned int, unsigned int*, char*, char*, char*, char*);

int add_imi(unsigned int *bytecode, int imi){
	if (imi<-32768 || imi>32767) return (-1);
	*bytecode|= (0xFFFF & imi);
	return(0);
}

int add_sht(unsigned int *bytecode, int sht){
	if (sht<0 || sht>31) return(-1);
	*bytecode|= (0x1F & sht) << 6;
	return(0);
}

int add_reg(unsigned int *bytecode, char *reg, int pos){
	int i;
	for(i=0;i<MAX_REGISTER;i++){
		if(!strcmp(reg,register_str[i])){
		*bytecode |= (i << pos);
			return(0);
		}
	}
	return(-1);
}

int add_addr(unsigned int *bytecode, int addr){
    *bytecode |= ((addr>>2) & 0x3FFFFF);
    return 0;
}

int add_lbl(unsigned int offset, unsigned int *bytecode, char *label){
	char l[MAX_ARG_LEN+1];
	int j=0;
	while(j<prog_len){
		memset(l,0,MAX_ARG_LEN+1);
		sscanf(&prog[j][0],"%" XSTR(MAX_ARG_LEN) "[^:]:", l);
		if (label!=NULL && !strcmp(l, label)) return(add_imi( bytecode, j-(offset+1)) );
		j++;
	}
	return (-1);
}

int add_text_addr(unsigned int *bytecode, char *label){
	char l[MAX_ARG_LEN+1];
	int j=0;
	while(j<prog_len){
		memset(l,0,MAX_ARG_LEN+1);
		sscanf(&prog[j][0],"%" XSTR(MAX_ARG_LEN) "[^:]:", l);
		if (label!=NULL && !strcmp(l, label)) return(add_addr( bytecode, ADDR_POS(j)));
		j++;
	}
	return (-1);
}

int opcode_nop(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0;
	return (0);
}

int opcode_add(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x20; 				// op,shamt,funct
	if (add_reg(bytecode,arg1,11)<0) return (-1); 	// destination register
	if (add_reg(bytecode,arg2,21)<0) return (-1);	// source1 register
	if (add_reg(bytecode,arg3,16)<0) return (-1);	// source2 register
	return (0);
}

int opcode_addi(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x20000000; 				// op
	if (add_reg(bytecode,arg1,16)<0) return (-1);	// destination register
	if (add_reg(bytecode,arg2,21)<0) return (-1);	// source1 register
	if (add_imi(bytecode,atoi(arg3))) return (-1);	// constant
	return (0);
}

int opcode_andi(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x30000000; 				// op
	if (add_reg(bytecode,arg1,16)<0) return (-1); 	// destination register
	if (add_reg(bytecode,arg2,21)<0) return (-1);	// source1 register
	if (add_imi(bytecode,atoi(arg3))) return (-1);	// constant
	return (0);
}

int opcode_blez(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x18000000; 				// op
	if (add_reg(bytecode,arg1,21)<0) return (-1);	// register1
	if (add_lbl(offset,bytecode,arg2)) return (-1); // jump
	return (0);
}

int opcode_bne(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x14000000; 				// op
	if (add_reg(bytecode,arg1,21)<0) return (-1); 	// register1
	if (add_reg(bytecode,arg2,16)<0) return (-1);	// register2
	if (add_lbl(offset,bytecode,arg3)) return (-1); // jump
	return (0);
}

int opcode_srl(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x2; 					// op
	if (add_reg(bytecode,arg1,11)<0) return (-1);   // destination register
	if (add_reg(bytecode,arg2,16)<0) return (-1);   // source1 register
	if (add_sht(bytecode,atoi(arg3))<0) return (-1);// shift
	return(0);
}

int opcode_sll(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0; 					// op
	if (add_reg(bytecode,arg1,11)<0) return (-1);	// destination register
	if (add_reg(bytecode,arg2,16)<0) return (-1); 	// source1 register
	if (add_sht(bytecode,atoi(arg3))<0) return (-1);// shift
	return(0);
}

int opcode_jr(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x8; 					// op
	if (add_reg(bytecode,arg1,21)<0) return (-1);	// source register
	return(0);
}

int opcode_jal(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x0C000000; 					// op
	if (add_text_addr(bytecode, arg1)<0) return (-1);// find and add address
	return(0);
}

const char *opcode_str[] = {"nop", "add", "addi", "andi", "blez", "bne", "srl", "sll", "jal", "jr"};
opcode_function opcode_func[] = {&opcode_nop, &opcode_add, &opcode_addi, &opcode_andi, &opcode_blez, &opcode_bne, &opcode_srl, &opcode_sll, &opcode_jal, &opcode_jr};

/* a function to print the state of the machine */
int print_registers() {
  int i;
  printf("registers:\n");
  for (i = 0; i < MAX_REGISTER; i++) {
    printf(" %d: %d\n", i, registers[i]);
  }
  printf(" Program Counter: 0x%08x\n", pc);
  return (0);
}

// calculates the twos complement for 16 bit number
int calcTwosCom(unsigned int num) {
    if (((num & 0x8000) >> 15) == 1) {
      return -((num ^ 0xFFFF) + 1);
    }
    return num;
}

/* function to execute bytecode */
int exec_bytecode() {
  printf("EXECUTING PROGRAM ...\n");

  pc = ADDR_TEXT; // set program counter to the start of our program
  int i = 0;

  //printf("%0x08x\n", text[pc]);

  while (true) {

    pc = ADDR_TEXT + (i*4); // update program counter

    // NOP // exit executing if found 0x00000000
    if (text[i] == 0) {
      break;
    }

    printf("executing 0x%08x 0x%08x\n", pc, text[i]); // show current instruction

    // ------------------------------------------------------
    // FIRST 6 BITS IS 0 (opcode is 0)
    // ------------------------------------------------------
    if (((text[i] & 0xFC000000) >> 26) == 0) {

      // read in values for opcode 0
      int rs = ((text[i] & 0x03E00000) >> 21); // 26-21 bits
      int rt = ((text[i] & 0x001F0000) >> 16); // 21-16 bits
      int rd = ((text[i] & 0x0000F800) >> 11); // 16-11 bits
      int sa = ((text[i] & 0x000007C0) >> 6);  // 11-6 bits

      // ADD // rd = rs + rt
      if ((text[i] & 0x0000003F) == 32) {
        registers[rd] = registers[rs] + registers[rt];
      }
      // SLL // rd = rt << sa
      else if (((text[i] & 0x0000003F) == 0)) {
        registers[rd] = registers[rt] << sa;
      }
      // SRL // rd = rt >> sa
      else if (((text[i] & 0x0000003F) == 2)) {
        registers[rd] = registers[rt] >> sa;
      }
      // JR // pc = rs;
      else if (((text[i] & 0x0000003F) == 8)) {
        i = ((registers[rs] - ADDR_TEXT) / 4) - 1; // -1 because i++ at the end of while loop
      }
    
    }

    // ------------------------------------------------------
    // FIRST 6 BITS IS NOT 0 (opcode is not 0)
    // ------------------------------------------------------

    // ADDI -------------------------------------------
    else if ((((text[i] & 0xFC000000) >> 26) == 8)) {
      // read in values for addi
      int rs = ((text[i] & 0x03E00000) >> 21); // 26-21 bits
      int rt = ((text[i] & 0x001F0000) >> 16); // 21-16 bits
      int imm = (text[i] & 0x0000FFFF); // 16-0 bits
      // execute addi
      registers[rt] = registers[rs] + calcTwosCom(imm); // rt = rs + imm
    }

    // ANDI -------------------------------------------
    else if ((((text[i] & 0xFC000000) >> 26) == 12)) {
      // read in values for andi
      int rs = ((text[i] & 0x03E00000) >> 21); // 26-21 bits
      int rt = ((text[i] & 0x001F0000) >> 16); // 21-16 bits
      int imm = (text[i] & 0x0000FFFF); // 16-0 bits
      // execute andi
      registers[rt] = registers[rs] & imm; // rt = rs & imm
    }

    // BLEZ -------------------------------------------
    else if ((((text[i] & 0xFC000000) >> 26) == 6)) {
      // read in values for blez
      int rs = ((text[i] & 0x03E00000) >> 21);
      int offset = (text[i] & 0x0000FFFF);
      // execute blez
      if (registers[rs] <= 0) {
        i += calcTwosCom(offset); // jump by offset value
      }
    }

    // BNE -------------------------------------------
    else if ((((text[i] & 0xFC000000) >> 26) == 5)) {
      // read in values for bne
      int rs = ((text[i] & 0x03E00000) >> 21);
      int rt = ((text[i] & 0x001F0000) >> 16);
      int offset = (text[i] & 0x0000FFFF);
      // execute bne
      if (registers[rs] != registers[rt]) {
        i += calcTwosCom(offset); // jump by offset value
      }
    }

    // JAL -------------------------------------------
    else if ((((text[i] & 0xFC000000) >> 26) == 3)) {
      // read in value for jal
      int target = ((text[i] & 0x03FFFFFF) << 2);
      // execute jal
      registers[31] = pc + 4;
      i = ((target - ADDR_TEXT) / 4) - 1;
    }
    
    i++; // increase pc counter

  }

  print_registers(); // print out the state of registers at the end of execution

  printf("... DONE!\n");
  return (0);
}

/* function to create bytecode */
int make_bytecode() {
  unsigned int
      bytecode; // holds the bytecode for each converted program instruction
  int i, j = 0;    // instruction counter (equivalent to program line)

  char label[MAX_ARG_LEN + 1];
  char opcode[MAX_ARG_LEN + 1];
  char arg1[MAX_ARG_LEN + 1];
  char arg2[MAX_ARG_LEN + 1];
  char arg3[MAX_ARG_LEN + 1];

  printf("ASSEMBLING PROGRAM ...\n");
  while (j < prog_len) {
    memset(label, 0, sizeof(label));
    memset(opcode, 0, sizeof(opcode));
    memset(arg1, 0, sizeof(arg1));
    memset(arg2, 0, sizeof(arg2));
    memset(arg3, 0, sizeof(arg3));

    bytecode = 0;

    if (strchr(&prog[j][0], ':')) { // check if the line contains a label
      if (sscanf(
              &prog[j][0],
              "%" XSTR(MAX_ARG_LEN) "[^:]: %" XSTR(MAX_ARG_LEN) "s %" XSTR(
                  MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s",
              label, opcode, arg1, arg2,
              arg3) < 2) { // parse the line with label
        printf("parse error line %d\n", j);
        return (-1);
      }
    } else {
      if (sscanf(&prog[j][0],
                 "%" XSTR(MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s %" XSTR(
                     MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s",
                 opcode, arg1, arg2,
                 arg3) < 1) { // parse the line without label
        printf("parse error line %d\n", j);
        return (-1);
      }
    }

    for (i=0; i<MAX_OPCODE; i++){
        if (!strcmp(opcode, opcode_str[i]) && ((*opcode_func[i]) != NULL))
        {
            if ((*opcode_func[i])(j, &bytecode, opcode, arg1, arg2, arg3) < 0)
            {
                printf("ERROR: line %d opcode error (assembly: %s %s %s %s)\n", j, opcode, arg1, arg2, arg3);
                return (-1);
            }
            else
            {
                printf("0x%08x 0x%08x\n", ADDR_TEXT + 4 * j, bytecode);
                text[j] = bytecode;
                break;
            }
        }
        if (i == (MAX_OPCODE - 1))
        {
            printf("ERROR: line %d unknown opcode\n", j);
            return (-1);
        }
    }

    j++;
  }
  printf("... DONE!\n");
  return (0);
}

/* loading the program into memory */
int load_program(char *filename) {
  int j = 0;
  FILE *f;

  printf("LOADING PROGRAM %s ...\n", filename);

  f = fopen(filename, "r");
  if (f == NULL) {
      printf("ERROR: Cannot open program %s...\n", filename);
      return -1;
  }
  while (fgets(&prog[prog_len][0], MAX_LINE_LEN, f) != NULL) {
    prog_len++;
  }

  printf("PROGRAM:\n");
  for (j = 0; j < prog_len; j++) {
    printf("%d: %s", j, &prog[j][0]);
  }

  return (0);
}
