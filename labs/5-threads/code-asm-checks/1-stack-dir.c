// write code in C to check if stack grows up or down.
// suggestion:
//   - local variables are on the stack.
//   - so take the address of a local, call another routine, and
//     compare addresses of one of its local variables to the 
//     original.
//   - make sure you check the machine code make sure the
//     compiler didn't optimize the calls away!
//
//   - bonus: also use inline assembly or a gcc intrinsic to get the
//     stack pointer and compare.
#include "rpi.h"

int foo(int a) {
    int temp = a + 5;
    int address = (uintptr_t)&temp;
    return address;
}

int stack_grows_down(void) {
    //todo("implement this routine\n");
    int temp = 5;
    int foo_temp = foo(1);
    return foo_temp < ((uintptr_t)&temp);
}

void notmain(void) {
    if(stack_grows_down())
        trace("stack grows down\n");
    else
        trace("stack grows up\n");
}
