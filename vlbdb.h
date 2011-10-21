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
void vlbdb_unit_retain (vlbdb_unit_t *);
int vlbdb_unit_release (vlbdb_unit_t *);

vlbdb_binder_t * vlbdb_binder_create (vlbdb_unit_t *, void *);
vlbdb_binder_t * vlbdb_binder_create_block (vlbdb_unit_t *, void *);
vlbdb_binder_t * vlbdb_binder_copy (vlbdb_binder_t *);
void vlbdb_binder_retain (vlbdb_binder_t *);
int vlbdb_binder_release (vlbdb_binder_t *);

unsigned vlbdb_register_all_functions (vlbdb_unit_t *);
void vlbdb_register_function (vlbdb_unit_t *, void *, size_t, const char *);
void vlbdb_register_function_name (vlbdb_unit_t *, const char *, size_t);

void vlbdb_register_block (vlbdb_unit_t *, void *, size_t);

__attribute__ ((format (printf, 3, 4)))
void * vlbdb_specializef(vlbdb_unit_t *, void * function, const char *, ...);
__attribute__ ((format (printf, 3, 0)))
void * vlbdb_vspecializef(vlbdb_unit_t *, void * function, const char *, va_list);
__attribute__ ((format (printf, 3, 4)))
void * vlbdb_block_specializef(vlbdb_unit_t *, void * block, const char *, ...);
__attribute__ ((format (printf, 3, 0)))
void * vlbdb_vblock_specializef(vlbdb_unit_t *, void * block, const char *, va_list);
void * vlbdb_block_specialize(vlbdb_unit_t *, void * block);

int vlbdb_intern_range (vlbdb_unit_t *, void *, size_t);
int vlbdb_register_range (vlbdb_unit_t *, void *, size_t);

void vlbdb_bind_uint(vlbdb_binder_t *, unsigned long long);
void vlbdb_bind_int(vlbdb_binder_t *, long long);
void vlbdb_bind_fp(vlbdb_binder_t *, double);
void vlbdb_bind_ptr(vlbdb_binder_t *, void *);
void vlbdb_bind_range(vlbdb_binder_t *, void *, size_t);
__attribute__ ((format (printf, 2, 3)))
void vlbdb_bindf(vlbdb_binder_t *, const char *, ...);
__attribute__ ((format (printf, 2, 0)))
void vlbdb_vbindf(vlbdb_binder_t *, const char *, va_list);
void * vlbdb_specialize(vlbdb_binder_t *);
void * vlbdb_specialize_retain(vlbdb_binder_t *);
#endif
