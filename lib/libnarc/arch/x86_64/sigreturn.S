.section .text
.global narc_signal_return
.type narc_signal_return, @function
narc_signal_return:
    movabsq $0x4e41524300000405, %rax
    syscall
    ud2
.size narc_signal_return, .-narc_signal_return

.section .note.GNU-stack,"",@progbits
