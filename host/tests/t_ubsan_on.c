// The undefined-behavior sanitizer is on and fatal in `make run-ubsan` (plan item 111): an int sum that does not overflow runs; built with
// -DDM_TEST_UB the same sum overflows (INT_MAX + 1 from a volatile, so nothing folds it away) and the run must stop with UBSan's "runtime error".
#include <limits.h>
#include <stdio.h>
int main(void) {
#ifdef DM_TEST_UB
    volatile int a = INT_MAX;
#else
    volatile int a = INT_MAX - 1;
#endif
    int s = a + 1;
    printf("t_ubsan_on: %d, no overflow\n", s);
    return 0;
}
