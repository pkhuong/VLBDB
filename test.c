#include "vlbdb.h"
#include <stdio.h>

int test (int x, int y)
{
        return x+y;
}

int test_int_ptr (int * x, int y)
{
        return (*x)+y;
}

int funcall (void * ptr, int x, int y)
{
        //int(*fun)(int) = ptr;
        return ((int(*)(int, int))ptr)(x, y);
}

int funcall_ptr (void ** ptr, int x)
{
        /* int(*fun)(int) = *ptr; */
        /* return fun(x); */
        return ((int(*)(int))ptr[1])(x);
}

int main ()
{
        vlbdb_unit_t * unit = vlbdb_unit_from_bitcode("test.bc", NULL);
        vlbdb_register_function(unit, test, 0, NULL);
        vlbdb_register_function(unit, test_int_ptr, 0, NULL);
        vlbdb_register_function(unit, funcall, 0, NULL);
        vlbdb_register_function(unit, funcall_ptr, 0, NULL);

        vlbdb_binder_t * binder = vlbdb_binder_create(unit, test);
        vlbdb_bind_int(binder, 42);
        int (*test2)(int) = vlbdb_specialize(binder);
        vlbdb_binder_destroy(binder);

        binder = vlbdb_binder_create(unit, funcall);
        vlbdb_bind_ptr(binder, test);
        vlbdb_bind_int(binder, 42);
        int (*test3)(int) = vlbdb_specialize(binder);
        vlbdb_binder_destroy(binder);

        int x = 30;
        binder = vlbdb_binder_create(unit, test_int_ptr);
        vlbdb_bind_range(binder, &x, sizeof(x));
        int (*test4)(int) = vlbdb_specialize(binder);
        vlbdb_binder_destroy(binder);

        vlbdb_register_range(unit, (&test2)-1, sizeof(test2)*2);
        binder = vlbdb_binder_create(unit, funcall_ptr);
        vlbdb_bind_ptr(binder, (&test2)-1); //, 2*sizeof(test2));
        int (*test5)(int) = vlbdb_specialize(binder);
        vlbdb_binder_destroy(binder);

        printf("%p %p -- %i, %i\n", test2, test3, test2(4), test3(5));

        return 0;
}
