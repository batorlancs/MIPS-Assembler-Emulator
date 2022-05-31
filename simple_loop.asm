addi $a0 $zero 1
addi $t0 $zero 10
loop: sll $a0 $a0 1
addi $t0 $t0 -1
bne $t0 $zero loop
nop
