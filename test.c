#include "vlbdb.h"
#include <stdio.h>

int test (int x, int y)
{
        return x+y;
}

int main ()
{
        vlbdb_unit_t * unit = vlbdb_unit_from_bitcode("test.bc", NULL);
        vlbdb_register_function(unit, test, 0, NULL);
        vlbdb_binder_t * binder = vlbdb_binder_create(unit, test);
        vlbdb_bind_int(binder, 42);
        int (*test2)(int) = vlbdb_specialize(binder);
        vlbdb_binder_destroy(binder);

        binder = vlbdb_binder_create(unit, test2);
        vlbdb_bind_int(binder, 2);
        int (*test3)() = vlbdb_specialize(binder);
        vlbdb_binder_destroy(binder);

        printf("%p %p -- %i, %i\n", test2, test3, test2(4), test3());

        return 0;
}
