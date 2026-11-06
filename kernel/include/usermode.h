#ifndef USERMODE_H
#define USERMODE_H

#define USER_CODE_ADDR  0x400000
#define USER_STACK_TOP  0x800000
#define USER_STACK_PAGES 4

void usermode_start(void);

#endif
