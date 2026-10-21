.syntax unified
.cpu cortex-m0plus
.thumb
.section .isr_vector,"a",%progbits
.global vector_table
vector_table:
    .word _estack
    .word Reset_Handler
    .rept 46
    .word Default_Handler
    .endr
