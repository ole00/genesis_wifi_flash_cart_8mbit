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

#define L5_INTR_STACK_SIZE  20
#define LX_INTR_A15_OFFSET 0
#define LX_INTR_A14_OFFSET 4
#define LX_INTR_A13_OFFSET 8
#define LX_INTR_A12_OFFSET 12
#define LX_INTR_A11_OFFSET 16


// get API version
#define CMD_API   0

// read from MCU
#define CMD_READ  1

//write to MCU
#define CMD_WRITE 2

// print text
#define CMD_PRINT 3

#define API_VERSION 0x01

/*

Transaction structure:
------------------+ Byte 0|Byte 1|Byte2|Byte3|Byte4|Byte5||Byte6|...
Name              | Magic |Cmd   |Len L|Len H|Adr L|Adr H||Payload
------------------+-------+------+-----+-----+-----+-----++-------------
Read Slow mem     | 0x5E  |1     | Length    | Address   || payload
Write Slow mem    | 0x5E  |2     | Length    | Address   || payload
---------------------------------------------------------------------
Print console     | 0x5E  |3     | UTF8 Payload ....... 0 terminated

*/

//end of addressable space in slow RAM (4096 + 65536)
#define ADDR_END 0x11000

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
    s32i    a11, a0, LX_INTR_A11_OFFSET

    // load gpio status reg to A13
    movi    a14, GPIO_STATUS_REG
    l32i    a13, a14, 0
    memw
  
    // This routine is executed too early at many times - we have to postpone its execution.
    // Basically we want to make sure the Console set the OE or WE line back high before we
    // start any processing. Failing to do so might disturb the data bus.
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
    // branch if bit was cleared by the previous instruction
    bbci    a13, 16, we_intr

// write from the MCU to Console (ie Console requested reads data from MCU)
// note that the transaction header must alredy exist in the scratch area
// of the isrRam. See we_intr: label for more inofrmation.
oe_intr:

    // set ON output reg - GPIO0 - for debug only
    movi    a13, GPIO_OUT_W1TS_REG
    movi    a15, (1 << 0)
    s32i    a15, a13, 0

    // load weAddr to A12
    movi    a15, weAddr
    l32i    a12, a15, 0
    memw

    // load weCount to A13, keep its address in A15
    movi    a15, weCount
    l32i    a13, a15, 0
    memw

    // Note that the GPIO direction must have been already set when the read command was issued.
    // Also, the fist byte was already pushed to transmitter during header processing, so we have to advance weCount early.
    //weCount ++
    addi     a13, a13, 1
    // store weCount
    s32i     a13, a15, 0
    memw

    // check whether it is the last payload byte
    movi    a15, weLen
    l32i    a14, a15, 0
    memw

    // add 6 bytes of transaction header to weLen
    addi  a14, a14, 6

    //still not the end if (weCount < weLen + 6) goto isr5_oe_end_check_done
    blt a13, a14, isr5_oe_end_check_done

        //last byte was written!
        // * set weCount to 0
        // * set weAddr to 0
        // * change direction of data port to In
    
        movi a14, 0
    
        movi a15, weCount
        s32i a14, a15, 0
        memw
    
        movi a15, weAddr
        s32i a14, a15, 0
        memw

        // set data dir in for bits 1 - 8 on PORT_B
        movi    a14, GPIO_ENABLE1_W1TC_REG
        movi    a12, 0b111111110 
        s32i    a12, a14, 0
        memw
    
        // set direction pin (PA15) for transceiver on Chip 1 (bottom 8 bits of the data bus)
        movi    a14, GPIO_OUT_W1TS_REG
        movi    a12, (1 << 15)
        s32i    a12, a14, 0
        memw

        //goto isr5_finish
        j isr5_finish
    isr5_oe_end_check_done:

    //we still need to read and pass the data 
    //load isrRam pointer to A14
    movi    a14, isrRam

    //read byte from isrRam[1] to A11 (that is the command byte)
    l8ui    a11, a14, 1
    memw

   //sanity check: if a11 (command byte) is not 1 (read mem) then something went wrong - reset counters
    beqi a11, CMD_READ, isr5_write_done_read_sanity_check
        //reset command
        movi    a13, 0xFF
        s8i     a13, a14, 1
        memw 
        
        movi    a13, 0
        
        // reset & store weAddr
        movi    a15, weAddr
        s32i     a13, a15, 0
        memw

        // reset & store weCount
        movi    a15, weCount
        s32i     a13, a15, 0
        memw

        // reset & store weLen
        movi    a15, weLen
        s32i     a13, a15, 0
        memw

        // goto isr5_finish
        j isr5_finish
    isr5_write_done_read_sanity_check:
   

    //add weAddr to the pointer A14
    add     a14, a14, a12
    //add weCount to the pointer A14 (isrRam)
    add     a14, a14, a13
    
    //read byte from isrRam to A15
    l8ui    a15, a14, 0
    memw
    
    // shift the value left by 1 bit (need to write to PORT_B bits 1 - 8)
    slli    a15, a15, 1

    //write the value to PORT_B
    movi    a14, GPIO_OUT1_REG
    s32i    a15, a14, 0
    memw

    // jump to the end of the routine
    j isr5_finish

// write from Console to MCU
// That's the initial request the Console makes to interact with the extra features of the cart:
//  Presume weCount = 0 and weAddr = 0 : writes go to the scratch area of the buffer
// - writes magic byte 0x5E
// - writes command byte (1 - read mem, 2 write mem, 3 print text)
// - write length (16 bit, Lo, Hi)
// - write ram addr (16 bit Lo, Hi)
// These 6  bytes comprise the header. Print command's header is only 2 bytes long as the 
// the printed text must be zero terminated and therefore no length is needed.
// After the header is read to the scratch memory area the header is checked.
// If the command is 1 (Read mem) then the Data Port is switched for output
// and the first byte from the Slow RAM (isrRam + offset 4096 + read address) is pushed on the port.
// Then, when the next interrupt line OE (Read) is asserted (this must be enforced in the Console
// program or reading library), the fist byte is already on the Data bus and the console
// reads the data. Then we read the next byte from the isrRam memory and push it on the Data bus for the
// next OE interrupt event. When the last byte was read the Data Port is switched to
// input, so that the MCU can read the next commands sent from the console (whenever they come).
// If the command is 2 (Write mem) the Data Port stays in the input state and incoming data
// are stored to the isrRam area.
we_intr:

    // set ON output reg - GPIO0 - for debug only
    movi    a13, GPIO_OUT_W1TS_REG
    movi    a15, (1 << 0)
    s32i    a15, a13, 0


    // load weAddr to A12
    movi    a15, weAddr
    l32i    a12, a15, 0
    memw

    // load weCount to A13, keep its address in A15
    movi    a15, weCount
    l32i    a13, a15, 0
    memw

    //load isrRam[] pointer to A14
    movi    a14, isrRam

    //set up roll-over address to a11
    movi    a11, ADDR_END
    add     a11, a11, a14

    //load isrRam[] pointer to A14
    movi    a14, isrRam

    //add weAddr to the pointer A14
    add     a14, a14, a12
    
    //add weCount to the pointer A14
    add     a14, a14, a13

    //check the address roll-over
    // if (addr < isrRam + ADDR_END) goto isr5_write_done_roll_over
    blt     a14, a11, isr5_write_done_roll_over
        //addr -= 65536
        addmi a14, a14, -32768
        addmi a14, a14, -32768
    isr5_write_done_roll_over:

    //weCount ++
    addi     a13, a13, 1
    // store weCount
    s32i     a13, a15, 0
    memw

    // set address of GPIO PORT_B to A15
    movi     a15, GPIO_IN1_REG
    // read current value of PORT_B to A11
    l32i  a11, a15, 0
    memw
    // we are interested in bits 1 - 8, so shift right by 1 bit
    srli  a11, a11, 1

    //keep the low 8 bits only (a11 &= 0xFF)
    movi a15, 0xFF
    and  a11, a11, a15 
    
    //write byte in A11 (value of PORT_B) to the pointer A14
    s8i    a11, a14, 0
    memw

    // now check the contents of the scratch buffer

    // if weCount != 1 goto isr5_write_done_magic_check
    movi.n a15, 1
    bne a13, a15, isr5_write_done_magic_check
        // weCount is 1       
        movi a15, 0x5E
        // Check the magic: if (data == 0x5E) goto isr5_finish
        _beq a15, a11, isr5_write_exit_magic_check //isr5_finish
            
        // A11 is 1 (first byte of the transaction) and magic is not 0x5E -> ignore data
        // and reset the weCount and save it
        addi a13, a13, -1 //weCount-- XX1
        // store weCount
        movi    a15, weCount
        s32i    a13, a15, 0 //X1
        memw
        
        isr5_write_exit_magic_check:
        j isr5_finish // go to isr5_finish
    isr5_write_done_magic_check:

    movi a15, 2
    // if weCount != 2 goto isr5_write_done_command_check
    bne a13, a15, isr5_write_done_command_check
        //weCount is 2

        //check the command is 0 (Get API version)
        bnez a11, isr5_write_done_command_api_check
            //prepare for read
            
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

            movi    a15, API_VERSION
            // shift the value left by 1 bit (need to write to PORT_B bits 1 - 8)
            slli    a15, a15, 1

            //write the value to PORT_B
            movi    a14, GPIO_OUT1_REG
            s32i    a15, a14, 0
            memw

            //set weCount to 6 so that the next read the transaction is finished (weLen should be 0)
            movi    a15, weCount
            movi    a13, 6
            // store weCount
            s32i    a13, a15, 0
            memw
        
            // goto isr5_finish
            j isr5_finish
        isr5_write_done_command_api_check:
        
        //check the command is < than 4
        movi a15, 4
        blt a11, a15, isr5_write_exit_command_check //isr5_finish

        // command is 4 or more -> reset weCount  and save it
        addi a13, a13, -2 //weCount -= 2 XX2
        // store weCount
        movi    a15, weCount
        s32i    a13, a15, 0
        memw
        
        isr5_write_exit_command_check:
        j isr5_finish // go to isr5_finish      
    isr5_write_done_command_check:    

    // here we know data[0] and data[1] are most likely sane 
    // decide whether the command is:
    //  * a simple one: cmd_3 - print; then just check the terminating zero to finish the transaction
    //  * advanced    : cmd_1 and cmd_2 - then load the length and addr
    
    //load isrRam[] pointer to A14
    movi    a14, isrRam
    //read byte from isrRam[1] to A12 (that is the command byte)
    l8ui    a12, a14, 1
    memw

    //if cmd != CMD_PRINT goto isr5_write_done_print_check
    bnei a12, CMD_PRINT, isr5_write_done_print_check
        //cmd is CMD_PRINT (3)
        //check the data value is 0 (if data != 0) goto isr5_finish
        bnez a11, isr5_write_exit_print_check

        // the data is the terminating zero -> end the transaction
        // set weCount to 0
        movi    a15, weCount
        movi    a13, 0
        // store weCount
        s32i     a13, a15, 0
        memw

        // set weAddr to 0
        movi    a15, weAddr
        movi    a13, 0
        // store weAddr
        s32i     a13, a15, 0
        memw

        // set pinWeAsserted to 1
        movi    a15, pinWeAsserted
        movi    a13, 1
        // store pinWeAsserted
        s8i     a13, a15, 0
        memw

        //test - io pin 0 down
        movi    a14, GPIO_OUT_W1TC_REG
        movi    a15, (1 << 0)
        s32i    a15, a14, 0
        memw

        isr5_write_exit_print_check:
        // goto isr5_finish
        j isr5_finish
    isr5_write_done_print_check:

    // all other commands
    // * check the position 4 - set up command's length
    // * check the position 6 - set up command's address
    // * position > 6 - check the end  of the transaction

    // if (weCount == 4) => store weLength
    bnei a13, 4, isr5_write_done_len_check
        //weCount is 4
        //load isrRam[] pointer to A14
        movi    a14, isrRam
        //read short int from isrRam[2] to A12 (that is the length)
        l16ui    a12, a14, 2
        memw
    
        movi    a15, weLen
        // store weLen
        s32i     a12, a15, 0
        memw
        // goto isr5_finish
        j isr5_finish
    isr5_write_done_len_check:

    // if (weCount != 6 goto isr5_write_done_addr_check
    bnei a13, 6, isr5_write_done_addr_check
        // weCount is 6
        // store weAddr
        //load isrRam[] pointer to A14
        movi    a14, isrRam
        //read short int from isrRam[4] to A13 (that is the address)
        l16ui    a13, a14, 4
        memw

        //add the RAM position decreased by 6: because when reading or writing the weCount is already 6
        addi a13, a13, 4090 // 4096 - 6
    
        movi    a15, weAddr
        // store weAddr
        s32i     a13, a15, 0
        memw


        // now check the command - if it is actually a Read command
        // we need to prepare for a read
        //if cmd != 1 goto isr5_finish
        bnei a12, CMD_READ, isr5_finish
            
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

        // load the transceiver/register with the first byte from the address
        //load isrRam pointer to A14
        movi    a14, isrRam

        // a13 still contains the index within  isrRam, advance isrRam pointer by the index
        add     a14, a14, a13
        
        //read byte from isrRam to A15
        l8ui    a15, a14, 6    // 6 is current weCount
        memw

        // shift the value left by 1 bit (need to write to PORT_B bits 1 - 8)
        slli    a15, a15, 1

        //write the value to PORT_B
        movi    a14, GPIO_OUT1_REG
        s32i    a15, a14, 0
        memw
        
        // goto isr5_finish
        j isr5_finish
    isr5_write_done_addr_check:

    // if (weCount  < 6 ) goto  isr5_finish (still reading the transaction header data)
    blti a13, 6, isr5_finish


    //sanity check: if a12 (command byte) is 1 (read mem) then something went wrong - reset counters
    bnei a12, CMD_READ, isr5_write_done_write_sanity_check
        //reset command
        movi    a13, 0xFF
        s8i     a13, a12, 1
        memw 
        
        movi    a13, 0
        
        // reset & store weAddr
        movi    a15, weAddr
        s32i     a13, a15, 0
        memw

        // reset & store weCount
        movi    a15, weCount
        s32i     a13, a15, 0
        memw

        // reset & store weLen
        movi    a15, weLen
        s32i     a13, a15, 0
        memw

        // goto isr5_finish
        j isr5_finish
    isr5_write_done_write_sanity_check:

    // now we've written a payload byte of the transaction
    // check whether it is the last payload byte
    movi    a15, weLen
    l32i    a14, a15, 0
    memw

    // add 6 bytees of transaction header
    addi  a14, a14, 6

    //if (weCount < weLen + 6) goto isr5_finish
    blt a13, a14, isr5_finish

    //last byte was written!
    //set weCount to 0
    //set weAddr to 0

    movi a14, 0

    movi a15, weCount
    s32i a14, a15, 0
    memw

    movi a15, weAddr
    s32i a14, a15, 0
    memw
    


isr5_finish:

    //turn off the GPIO0 - for debug only
    movi    a14, GPIO_OUT_W1TC_REG
    movi    a15, (1 << 0)
    s32i    a15, a14, 0
    memw


     /* restore registers */
   	l32i    a15, a0, LX_INTR_A15_OFFSET
    l32i    a14, a0, LX_INTR_A14_OFFSET
    l32i    a13, a0, LX_INTR_A13_OFFSET
    l32i    a12, a0, LX_INTR_A12_OFFSET
    l32i    a11, a0, LX_INTR_A11_OFFSET

    rsr     a0, EXCSAVE_5 // restore a0
    /* Return from interrupt 5 */
    rfi     5

    .global ld_include_xt_highint5
ld_include_xt_highint5:
