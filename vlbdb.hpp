/* VLBDB: Very late bitcode and data binder
 *
 */

#ifndef VLBDB_HPP
#define VLBDB_HPP

#include <tr1/memory>
#include <stdarg.h>

extern "C" {
#include "vlbdb.h"
}

/* binding_unit_t is the largest granularity at which runtime-binding
 * happens.
 *
 * A binding_unit is created from a bitcode file.  Only functions
 * defined in that bitcode file can be specialized and compiled at
 * runtime.
 *
 * Users will usually directly pass pointers to functions, and not
 * function names.  Instead, ld.so is used to map from names in the
 * bitcode to addresses.  The register methods can be used to
 * supplement that mapping.
 *
 * The binding_unit is more than just a runtime partial applier,
 * however.  In addition to caching specialization results, it also
 * preserves backward-translation information.  Thus, calls to
 * specialized functions can be de-optimized into calls of functions
 * known to the binding_unit (and to LLVM).  This de-optimization
 * process is useful because it lets us replace callees with cached
 * specializations, and even inline some calls.
 *
 * Each function (native or specialized) may have some metadata
 * associated with it.  Currently, the only metadata is the maximum
 * number of automatically-specialized arguments.  It defaults to 0,
 * but increasing that value can result in useful implicit recursive
 * specialization.
 *
 * It's often the case that C functions receive values as pointers.
 * In order to better support specialization of such functions
 * binding_unit have the ability to mark certain address ranges as
 * immutable (and thus subject to constant folding).  When the address
 * is irrelevant, it is preferable to instead intern a byte range,
 * which will increase the odds of successful caching.
 *
 * Finally, binding_units also have support for blocks, the
 * clang/Apple extension.  These closures are internally represented
 * as pointers to structures.  The specialized parses these structures
 * and ends up specializing calls in which the first argument is an
 * interned byte range.
 */

/* binder_t is used to programmatically build the constant argument
 * list to a given function or block.
 *
 * binding_unit provides some convenience template methods for known
 * number and types of arguments.  However, neither the template or
 * the format-string methods are well-suited to variable number or
 * types of arguments.  The binder interface is the most general
 * interface, and the convenience methods are just that.
 */

class binder_t;

class binding_unit_t {
        struct binding_unit_impl;
        std::tr1::shared_ptr<binding_unit_impl> impl;
        explicit binding_unit_t (binding_unit_impl *);
public:
        static binding_unit_t create_from_bitcode(const char *, 
                                                  void * context = 0);
        template <typename T>
        binder_t create_binder(T function);
        binder_t create_binder(void * function);

        template <typename T>
        binder_t create_block_binder(T block);
        binder_t create_block_binder(void * block);

        unsigned register_all_functions();
        template <typename T>
        void register_function(T function, size_t metadata = 0, const char * = 0);
        void register_function(void * function, size_t metadata = 0, const char * = 0);
        void register_function(const char *, size_t metadata = 0);

        template <typename T>
        void register_block(T block, size_t metadata = 0);
        void register_block(void * block, size_t metadata = 0);

        template <typename T>
        void * specializef(T function, const char *, ...);
        void * specializef(void * function, const char *, ...);

        template <typename T>
        void * vspecializef(T function, const char *, va_list args);
        void * vspecializef(void * function, const char *, va_list args);

        template <typename T>
        void * block_specializef(T block, const char *, ...);
        void * block_specializef(void * block, const char *, ...);

        template <typename T>
        void * vblock_specializef(T block, const char *, va_list args);
        void * vblock_specializef(void * block, const char *, va_list args);

        template <typename T>
        void * specialize(T function);
        template <typename T, typename A1>
        void * specialize(T function, const A1& arg1);
        template <typename T, typename A1, typename A2>
        void * specialize(T function, const A1& arg1, const A2& arg2);
        template <typename T, typename A1, typename A2, typename A3>
        void * specialize(T function, const A1& arg1, const A2& arg2,
                          const A3& arg3);
        template <typename T, typename A1, typename A2, typename A3,
                  typename A4>
        void * specialize(T function, const A1& arg1, const A2& arg2,
                          const A3& arg3, const A4 &arg4);

        template <typename T>
        void * specialize_block (T block);
        template <typename T, typename A1>
        void * specialize_block (T block, const A1& arg1);
        template <typename T, typename A1, typename A2>
        void * specialize_block (T block, const A1& arg1, const A2& arg2);
        template <typename T, typename A1, typename A2, typename A3>
        void * specialize_block (T block, const A1& arg1,
                                 const A2& arg2, const A3& arg3);
        template <typename T, typename A1, typename A2,
                  typename A3, typename A4>
        void * specialize_block (T block, const A1& arg1,
                                 const A2& arg2, const A3& arg3,
                                 const A4 &arg4);
        template <typename T>
        int intern_range (const T * address, size_t size = sizeof(T));
        int intern_range (const void * address, size_t size);
        template <typename T>
        int register_range (const T * address, size_t size = sizeof(T));
        int register_range (const void * address, size_t size);
};

class binder_t {
        struct binder_impl;
        std::tr1::shared_ptr<binder_impl> impl;
        explicit binder_t (binder_impl *);
public:
        binder_t clone (const binder_t &);
        static binder_t create_from_unit(binding_unit_t &unit,
                                         void * function);
        void bind_uint (unsigned long long);
        void bind_int (long long);
        void bind_fp (double);
        void bind_ptr (void *);
        void bind_range (const void *, size_t, bool intern = true);
        void * specialize();

        void bind (unsigned char x);
        void bind (unsigned short x);
        void bind (unsigned int x);
        void bind (unsigned long x);
        void bind (unsigned long long x);
        
        void bind (char x);
        void bind (short x);
        void bind (int x);
        void bind (long x);
        void bind (long long x);

        void bind (double x);
        void bind (float x);

        template <typename T>
        void bind (T * x);
        template <typename T>
        void bind (const T * x, size_t = sizeof(T), bool intern = true);
};

// Template implementation noise...
template <typename T>
binder_t binding_unit_t::create_binder (T function)
{
        return create_binder((void*)function);
}

template <typename T>
binder_t binding_unit_t::create_block_binder (T block)
{
        return create_block_binder((void*)block);
}

template <typename T>
void binding_unit_t::register_function(T function, size_t metadata, const char * name)
{
        return register_function((void*)function, metadata, name);
}

template <typename T>
void binding_unit_t::register_block(T block, size_t metadata)
{
        return register_block((void*)block, metadata);
}

template <typename T>
void * binding_unit_t::specializef (T function, const char *format, ...)
{
        va_list ap;
        va_start(ap, format);
        return vspecializef((void *)function, format, ap);
}

template <typename T>
void * binding_unit_t::vspecializef (T function, const char *format, va_list ap)
{
        return vspecializef((void *)function, format, ap);
}

template <typename T>
void * binding_unit_t::block_specializef (T block, const char *format, ...)
{
        va_list ap;
        va_start(ap, format);
        return vblock_specializef((void *)block, format, ap);
}

template <typename T>
void * binding_unit_t::vblock_specializef (T block, const char *format, va_list ap)
{
        return vblock_specializef((void *)block, format, ap);
}

template <typename T>
void * binding_unit_t::specialize (T fun)
{
        binder_t binder(create_binder(fun));
        return binder.specialize();
}

template <typename T, typename A1>
void * binding_unit_t::specialize (T fun, const A1& arg1)
{
        binder_t binder(create_binder(fun));
        binder.bind(arg1);
        return binder.specialize();
}

template <typename T, typename A1, typename A2>
void * binding_unit_t::specialize (T fun, const A1& arg1, const A2 &arg2)
{
        binder_t binder(create_binder(fun));
        binder.bind(arg1);
        binder.bind(arg2);
        return binder.specialize();
}

template <typename T, typename A1, typename A2, typename A3>
void * binding_unit_t::specialize (T fun, const A1& arg1, const A2 &arg2,
                                 const A3 &arg3)
{
        binder_t binder(create_binder(fun));
        binder.bind(arg1);
        binder.bind(arg2);
        binder.bind(arg3);
        return binder.specialize();
}

template <typename T, typename A1, typename A2, typename A3, typename A4>
void * binding_unit_t::specialize (T fun, const A1& arg1, const A2 &arg2,
                                 const A3 &arg3, const A4 &arg4)
{
        binder_t binder(create_binder(fun));
        binder.bind(arg1);
        binder.bind(arg2);
        binder.bind(arg3);
        binder.bind(arg4);
        return binder.specialize();
}

template <typename T>
void * binding_unit_t::specialize_block (T block)
{
        binder_t binder(create_block_binder(block));
        return binder.specialize();
}

template <typename T, typename A1>
void * binding_unit_t::specialize_block (T block, const A1& arg1)
{
        binder_t binder(create_block_binder(block));
        binder.bind(arg1);
        return binder.specialize();
}

template <typename T, typename A1, typename A2>
void * binding_unit_t::specialize_block (T block, const A1& arg1, const A2 &arg2)
{
        binder_t binder(create_block_binder(block));
        binder.bind(arg1);
        binder.bind(arg2);
        return binder.specialize();
}

template <typename T, typename A1, typename A2, typename A3>
void * binding_unit_t::specialize_block (T block, const A1& arg1, const A2 &arg2,
                                 const A3 &arg3)
{
        binder_t binder(create_block_binder(block));
        binder.bind(arg1);
        binder.bind(arg2);
        binder.bind(arg3);
        return binder.specialize();
}

template <typename T, typename A1, typename A2, typename A3, typename A4>
void * binding_unit_t::specialize_block (T block, const A1& arg1, const A2 &arg2,
                                 const A3 &arg3, const A4 &arg4)
{
        binder_t binder(create_block_binder(block));
        binder.bind(arg1);
        binder.bind(arg2);
        binder.bind(arg3);
        binder.bind(arg4);
        return binder.specialize();
}

template <typename T>
int binding_unit_t::intern_range (const T * address, size_t size)
{
        return intern_range((const void*)address, size);
}

template <typename T>
int binding_unit_t::register_range (const T * address, size_t size)
{
        return register_range((const void*)address, size);
}

template <typename T>
void binder_t::bind (T * x)
{
        return bind_ptr((void*)x);
}
template <typename T>
void binder_t::bind (const T * x, size_t range, bool intern)
{
        return bind_range((const void *)x, range, intern);        
}

#endif
