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

#include <Block.h>
void * make_adder (int x)
{
        return Block_copy(^ (int y) {return x + y; });
}

int main ()
{
        vlbdb_unit_t * unit = vlbdb_unit_from_bitcode("test.bc", NULL);
        vlbdb_register_all_functions(unit);

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

        void * block = make_adder(33);
        binder = vlbdb_binder_create_block(unit, block);
        int (*test6)(int) = vlbdb_specialize(binder);
        vlbdb_binder_destroy(binder);

        printf("%p %p -- %i, %i\n", test2, test3, test2(4), test3(5));

        return 0;
}
