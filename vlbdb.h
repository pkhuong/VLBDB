#ifndef VLBDH_H
#define VLBDH_H

#include <stddef.h>
#include <stdarg.h>

#ifndef VLBDB_IN_IMPLEMENTATION
 struct vlbdb_unit;
 struct vlbdb_binder;

 typedef struct vlbdb_unit vlbdb_unit_t;
 typedef struct vlbdb_binder vlbdb_binder_t;
#endif

vlbdb_unit_t * vlbdb_unit_from_bitcode (const char *, void * context);
void vlbdb_unit_destroy (vlbdb_unit_t *);

vlbdb_binder_t * vlbdb_binder_create (vlbdb_unit_t *, void *);
vlbdb_binder_t * vlbdb_binder_create_block (vlbdb_unit_t *, void *); /* TODO */
vlbdb_binder_t * vlbdb_binder_copy (vlbdb_binder_t *);
void vlbdb_binder_destroy (vlbdb_binder_t *);

void vlbdb_register_function (vlbdb_unit_t *, void *, size_t, const char *);
void vlbdb_register_function_name (vlbdb_unit_t *, const char *, size_t);

void vlbdb_register_block (vlbdb_unit_t *, void *, size_t); /* TODO */

/* TODO */
void * vlbdb_specializef(vlbdb_unit_t *, void * function, const char *, ...);
void * vlbdb_vspecializef(vlbdb_unit_t *, void * function, const char *, va_list);
void * vlbdb_block_specializef(vlbdb_unit_t *, void * function, const char *, ...);
void * vlbdb_vblock_specializef(vlbdb_unit_t *, void * function, const char *, va_list);

/* TODO */
int vlbdb_intern_range (vlbdb_unit_t *, void *, size_t);
int vlbdb_register_range (vlbdb_unit_t *, void *, size_t);

void vlbdb_bind_uint(vlbdb_binder_t *, unsigned long long);
void vlbdb_bind_int(vlbdb_binder_t *, long long);
void vlbdb_bind_fp(vlbdb_binder_t *, double);
void vlbdb_bind_ptr(vlbdb_binder_t *, void *);
void vlbdb_bind_range(vlbdb_binder_t *, void *, size_t); /* TODO */
void * vlbdb_specialize(vlbdb_binder_t *);
#endif
