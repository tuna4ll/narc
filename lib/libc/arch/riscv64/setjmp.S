.section .text
.global setjmp
.global _setjmp
.type setjmp, @function
.type _setjmp, @function
setjmp:
_setjmp:
    sd ra, 0(a0)
    sd sp, 8(a0)
    sd s0, 16(a0)
    sd s1, 24(a0)
    sd s2, 32(a0)
    sd s3, 40(a0)
    sd s4, 48(a0)
    sd s5, 56(a0)
    sd s6, 64(a0)
    sd s7, 72(a0)
    sd s8, 80(a0)
    sd s9, 88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)
    fsd fs0, 112(a0)
    fsd fs1, 120(a0)
    fsd fs2, 128(a0)
    fsd fs3, 136(a0)
    fsd fs4, 144(a0)
    fsd fs5, 152(a0)
    fsd fs6, 160(a0)
    fsd fs7, 168(a0)
    fsd fs8, 176(a0)
    fsd fs9, 184(a0)
    fsd fs10, 192(a0)
    fsd fs11, 200(a0)
    li a0, 0
    ret
.size setjmp, .-setjmp

.global longjmp
.global _longjmp
.type longjmp, @function
.type _longjmp, @function
longjmp:
_longjmp:
    ld ra, 0(a0)
    ld sp, 8(a0)
    ld s0, 16(a0)
    ld s1, 24(a0)
    ld s2, 32(a0)
    ld s3, 40(a0)
    ld s4, 48(a0)
    ld s5, 56(a0)
    ld s6, 64(a0)
    ld s7, 72(a0)
    ld s8, 80(a0)
    ld s9, 88(a0)
    ld s10, 96(a0)
    ld s11, 104(a0)
    fld fs0, 112(a0)
    fld fs1, 120(a0)
    fld fs2, 128(a0)
    fld fs3, 136(a0)
    fld fs4, 144(a0)
    fld fs5, 152(a0)
    fld fs6, 160(a0)
    fld fs7, 168(a0)
    fld fs8, 176(a0)
    fld fs9, 184(a0)
    fld fs10, 192(a0)
    fld fs11, 200(a0)
    seqz a0, a1
    add a0, a0, a1
    ret
.size longjmp, .-longjmp

.global sigsetjmp
.type sigsetjmp, @function
sigsetjmp:
    sw a1, 208(a0)
    beqz a1, setjmp
    addi sp, sp, -16
    sd a0, 0(sp)
    sd ra, 8(sp)
    call __sigsetjmp_save
    ld a0, 0(sp)
    ld ra, 8(sp)
    addi sp, sp, 16
    j setjmp
.size sigsetjmp, .-sigsetjmp

.section .note.GNU-stack,"",@progbits
