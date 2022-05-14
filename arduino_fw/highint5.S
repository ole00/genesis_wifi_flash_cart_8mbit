/* 
 *  credits: https://github.com/HaydenDekker/esp-int-test
 *  article: https://haydendekker.medium.com/esp32-interrupts-can-only-do-200khz-56f8dbb6a61c
*/

#include <xtensa/coreasm.h>
#include <xtensa/corebits.h>
#include <xtensa/config/system.h>
#include "freertos/xtensa_context.h"
#include "esp_private/panic_reason.h"
#include "sdkconfig.h"
#include "soc/soc.h"
#include "soc/gpio_reg.h"
#include "soc/dport_reg.h"

#define L5_INTR_STACK_SIZE  16
#define LX_INTR_A15_OFFSET 0
#define LX_INTR_A14_OFFSET 4
#define LX_INTR_A13_OFFSET 8
#define LX_INTR_A12_OFFSET 12
    .data
_l5_intr_stack:
    .space      L5_INTR_STACK_SIZE

    .section .iram1,"ax"
    .global     xt_highint5
    .type       xt_highint5,@function
    .align      4
xt_highint5:

	movi    a0, _l5_intr_stack // assuming a0 saved restored by EXCSAVE_5 at the end of this
    s32i    a15, a0, LX_INTR_A15_OFFSET
    s32i    a14, a0, LX_INTR_A14_OFFSET
    s32i    a13, a0, LX_INTR_A13_OFFSET
    s32i    a12, a0, LX_INTR_A12_OFFSET

    // load gpio status reg to A13
    movi    a14, GPIO_STATUS_REG
    l32i    a13, a14, 0
    memw

    // TODO - change the execution interrupt on Raising edge and remove the nops
    
    // this routine is executed too early in many times - we have to postpone its execution
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    memw
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    memw
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    memw
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    memw


    /* Clear interupt on GPIO16 (OE) and GPIO17 (WE) */
    movi    a14, GPIO_STATUS_W1TC_REG
    movi    a15, (1 << 16) | (1 << 17)
    s32i    a15, a14, 0
    memw

    // check wheter OE or WE interrupt was signalled
    // branch if bit was cleared by previous instruction
    bbci    a13, 16, we_intr

// write from the MCU to Console
oe_intr:
/*
    // toggle output reg - GPIO0
    movi    a13, GPIO_OUT_W1TS_REG
    movi    a14, GPIO_OUT_W1TC_REG
    movi    a15, (1 << 0)
    s32i    a15, a13, 0
    s32i    a15, a14, 0
*/

    // load pinOeCount to A13, keep its address in A15
    movi    a15, pinOeCount
    l32i    a13, a15, 0
    memw

    // if (pinOeCount != 0) goto write_data
    bnei    a13, 0, write_data
    // pionOeCount is 0
    // set data dir out for bits 1 - 8 on PORT_B
    movi    a14, GPIO_ENABLE1_W1TS_REG
    movi    a12, 0b111111110 
    s32i    a12, a14, 0
    memw

    // set direction pin (PA15) for tranceiver on Chip 1 (bottom 8 bits of the data bus)
    movi    a14, GPIO_OUT_W1TC_REG
    movi    a12, (1 << 15)
    s32i    a12, a14, 0
    memw
     
write_data:
    //load isrRam pointer to A14
    movi    a14, isrRam

    //add pinOeCount to the pointer A14 (isrRam)
    add     a14, a14, a13

    //pinOeCount ++
    addi     a13, a13, 1
    // store pinOeCount
    s32i     a13, a15, 0
    memw

    //read byte from isrRam to A15
    l8ui    a15, a14, 0
    memw
    // shift the value left by 1 bit (need to write to PORT_B bits 1 - 8)
    slli    a15, a15, 1

    //write the value to PORT_B
    movi    a14, GPIO_OUT1_REG
    s32i    a15, a14, 0
    memw

    // if pinOeCount != 3 goto isr5_finish
    bnei   a13, 3, isr5_finish 

    // clear pinOeCount
    movi    a15, pinOeCount
    movi    a13, 0
    s32i    a13, a15, 0
    memw

    // this is a test - increase tha value 
    // isrRam[1]++;
    movi    a15, isrRam
    addi    a15, a15, 1
    l8ui    a13, a15, 0
    memw
    addi    a13, a13, 1
    s8i     a13, a15, 0
    memw
    
    // pinOeCount was 3 => set data dir in 
    // set data dir in for bits 1 - 8 on PORT_B
    movi    a14, GPIO_ENABLE1_W1TC_REG
    movi    a12, 0b111111110 
    s32i    a12, a14, 0
    memw

    // set direction pin (PA15) for tranceiver on Chip 1 (bottom 8 bits of the data bus)
    movi    a14, GPIO_OUT_W1TS_REG
    movi    a12, (1 << 15)
    s32i    a12, a14, 0
    memw

    // jump to the end of the routine
    j isr5_finish

// write from Console to MCU
we_intr:
    // load pinWeCount to A13, keep its address in A15
    movi    a15, pinWeCount
    l32i    a13, a15, 0
    memw

    //load isrRead pointer to A14
    movi    a14, isrRead

    //add pinWeCount to the pointer A14
    add     a14, a14, a13

    //pinWeCount ++
    addi     a13, a13, 1
    // store pinWeCount
    s32i     a13, a15, 0
    memw

    // set address of GPIO PORT_B to A15
    movi     a15, GPIO_IN1_REG
    // read current value of PORT_B to A13
    l32i  a13, a15, 0
    memw
    // we are interested in bits 1 - 8, so shift right by 1 bit
    srli  a13, a13, 1
    
    //write byte in A13 (value of PORT_B) to the pointer A14
    s8i    a13, a14, 0
    memw
   
    
/*
    // toggle output reg - GPIO0
    movi    a13, GPIO_OUT_W1TS_REG
    movi    a14, GPIO_OUT_W1TC_REG
    movi    a15, (1 << 0)
    s32i	a15, a13, 0
    s32i	a15, a14, 0

    // Increment intr_cnt
    movi    a14, intCounter
    l32i    a15, a14, 0
    addi    a15, a15, 1
    s32i    a15, a14, 0
    memw
*/
isr5_finish:

     /* restore registers */
   	l32i    a15, a0, LX_INTR_A15_OFFSET
    l32i    a14, a0, LX_INTR_A14_OFFSET
    l32i    a13, a0, LX_INTR_A13_OFFSET
    l32i    a12, a0, LX_INTR_A12_OFFSET

    rsr     a0, EXCSAVE_5 // restore a0
    /* Return from interrupt 5 */
    rfi     5

    .global ld_include_xt_highint5
ld_include_xt_highint5:
