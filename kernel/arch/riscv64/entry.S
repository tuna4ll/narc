.section .text
.balign 4
.global riscv_trap_entry
riscv_trap_entry:
    csrrw sp, sscratch, sp
    bnez sp, 1f
    csrrw sp, sscratch, sp
1:
    addi sp, sp, -272
    sd zero, 0(sp)
    sd ra, 8(sp)
    sd gp, 24(sp)
    sd tp, 32(sp)
    sd t0, 40(sp)
    sd t1, 48(sp)
    sd t2, 56(sp)
    sd s0, 64(sp)
    sd s1, 72(sp)
    sd a0, 80(sp)
    sd a1, 88(sp)
    sd a2, 96(sp)
    sd a3, 104(sp)
    sd a4, 112(sp)
    sd a5, 120(sp)
    sd a6, 128(sp)
    sd a7, 136(sp)
    sd s2, 144(sp)
    sd s3, 152(sp)
    sd s4, 160(sp)
    sd s5, 168(sp)
    sd s6, 176(sp)
    sd s7, 184(sp)
    sd s8, 192(sp)
    sd s9, 200(sp)
    sd s10, 208(sp)
    sd s11, 216(sp)
    sd t3, 224(sp)
    sd t4, 232(sp)
    sd t5, 240(sp)
    sd t6, 248(sp)
    csrr t0, sscratch
    bnez t0, 2f
    addi t0, sp, 272
2:
    sd t0, 16(sp)
    csrw sscratch, zero
    csrr t0, sepc
    csrr t1, sstatus
    sd t0, 256(sp)
    sd t1, 264(sp)
    mv a0, sp
    call riscv_trap

.global riscv_trap_return
riscv_trap_return:
    ld t0, 256(sp)
    ld t1, 264(sp)
    csrw sepc, t0
    csrw sstatus, t1
    andi t1, t1, 0x100
    bnez t1, 3f
    addi t0, sp, 272
    csrw sscratch, t0
3:
    ld ra, 8(sp)
    ld gp, 24(sp)
    ld tp, 32(sp)
    ld t0, 40(sp)
    ld t1, 48(sp)
    ld t2, 56(sp)
    ld s0, 64(sp)
    ld s1, 72(sp)
    ld a0, 80(sp)
    ld a1, 88(sp)
    ld a2, 96(sp)
    ld a3, 104(sp)
    ld a4, 112(sp)
    ld a5, 120(sp)
    ld a6, 128(sp)
    ld a7, 136(sp)
    ld s2, 144(sp)
    ld s3, 152(sp)
    ld s4, 160(sp)
    ld s5, 168(sp)
    ld s6, 176(sp)
    ld s7, 184(sp)
    ld s8, 192(sp)
    ld s9, 200(sp)
    ld s10, 208(sp)
    ld s11, 216(sp)
    ld t3, 224(sp)
    ld t4, 232(sp)
    ld t5, 240(sp)
    ld t6, 248(sp)
    ld sp, 16(sp)
    sret

.global arch_enter_user
.type arch_enter_user, @function
arch_enter_user:
    mv sp, a0
    j riscv_trap_return

.option push
.option arch, +f,+d
.global arch_task_state_save
.type arch_task_state_save, @function
arch_task_state_save:
    fsd f0, 0(a0)
    fsd f1, 8(a0)
    fsd f2, 16(a0)
    fsd f3, 24(a0)
    fsd f4, 32(a0)
    fsd f5, 40(a0)
    fsd f6, 48(a0)
    fsd f7, 56(a0)
    fsd f8, 64(a0)
    fsd f9, 72(a0)
    fsd f10, 80(a0)
    fsd f11, 88(a0)
    fsd f12, 96(a0)
    fsd f13, 104(a0)
    fsd f14, 112(a0)
    fsd f15, 120(a0)
    fsd f16, 128(a0)
    fsd f17, 136(a0)
    fsd f18, 144(a0)
    fsd f19, 152(a0)
    fsd f20, 160(a0)
    fsd f21, 168(a0)
    fsd f22, 176(a0)
    fsd f23, 184(a0)
    fsd f24, 192(a0)
    fsd f25, 200(a0)
    fsd f26, 208(a0)
    fsd f27, 216(a0)
    fsd f28, 224(a0)
    fsd f29, 232(a0)
    fsd f30, 240(a0)
    fsd f31, 248(a0)
    frcsr t0
    sd t0, 256(a0)
    ret

.global arch_task_state_restore
.type arch_task_state_restore, @function
arch_task_state_restore:
    fld f0, 0(a0)
    fld f1, 8(a0)
    fld f2, 16(a0)
    fld f3, 24(a0)
    fld f4, 32(a0)
    fld f5, 40(a0)
    fld f6, 48(a0)
    fld f7, 56(a0)
    fld f8, 64(a0)
    fld f9, 72(a0)
    fld f10, 80(a0)
    fld f11, 88(a0)
    fld f12, 96(a0)
    fld f13, 104(a0)
    fld f14, 112(a0)
    fld f15, 120(a0)
    fld f16, 128(a0)
    fld f17, 136(a0)
    fld f18, 144(a0)
    fld f19, 152(a0)
    fld f20, 160(a0)
    fld f21, 168(a0)
    fld f22, 176(a0)
    fld f23, 184(a0)
    fld f24, 192(a0)
    fld f25, 200(a0)
    fld f26, 208(a0)
    fld f27, 216(a0)
    fld f28, 224(a0)
    fld f29, 232(a0)
    fld f30, 240(a0)
    fld f31, 248(a0)
    ld t0, 256(a0)
    fscsr t0
    ret
.option pop
