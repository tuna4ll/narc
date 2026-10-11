.section .text
.global narc_signal_return
.type narc_signal_return, %function
narc_signal_return:
    movz x8, #0x0405
    movk x8, #0x5243, lsl #32
    movk x8, #0x4e41, lsl #48
    svc #0
    brk #0
.size narc_signal_return, .-narc_signal_return

.section .note.GNU-stack,"",%progbits
