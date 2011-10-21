#include "vlbdb.h"
#include <assert.h>
#include <stdlib.h>
#include <limits.h>

void * vlbdb_specializef (vlbdb_unit_t * unit, void * function, 
                          const char * format, ...)
{
        va_list values;
        va_start(values, format);
        return vlbdb_vspecializef(unit, function, format, values);
}

void * vlbdb_block_specializef (vlbdb_unit_t * unit, void * block, 
                                const char * format, ...)
{
        va_list values;
        va_start(values, format);
        return vlbdb_vblock_specializef(unit, block, format, values);
}

void * vlbdb_vspecializef (vlbdb_unit_t * unit, void * function, 
                           const char * format, va_list values)
{
        vlbdb_binder_t * binder = vlbdb_binder_create(unit, function);
        vlbdb_vbindf(binder, format, values);
        return vlbdb_specialize(binder);
}

void * vlbdb_vblock_specializef (vlbdb_unit_t * unit, void * block, 
                                 const char * format, va_list values)
{
        vlbdb_binder_t * binder = vlbdb_binder_create_block(unit, block);
        vlbdb_vbindf(binder, format, values);
        return vlbdb_specialize(binder);
}

void * vlbdb_block_specialize(vlbdb_unit_t * unit, void * block)
{
        vlbdb_binder_t * binder = vlbdb_binder_create_block(unit, block);
        return vlbdb_specialize(binder);
}

void vlbdb_bindf (vlbdb_binder_t * binder, const char * format, ...)
{
        va_list values;
        va_start(values, format);
        return vlbdb_vbindf(binder, format, values);
}

static const char *
bind_one_argf (vlbdb_binder_t * binder, const char * format, va_list values)
{
        int have_width = 0;
        long width = 0;
        enum {normal, l, z, ll} modifier = normal;
        int after_point = 0;

        for (; *format != '\0'; format++) {
                switch(*format) {
                case '*':
                case '-': case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                {
                        int value = 0;
                        have_width = 1;
                        if (*format == '*') {
                                value = va_arg(values, int);
                        } else {
                                value = strtol(format, (char**)&format, 10);
                                format--;
                        }
                        if (!after_point) 
                                width = value;
                        break;
                }
                case '.':
                        after_point = 1;
                        break;
                case 'h': break;
                case 'l': 
                        if (modifier == normal)
                                modifier = l;
                        else    modifier = ll;
                        break;
                case 'j':
                        modifier = ll;
                        break;
                case 'z':
                case 't':
                        modifier = z;
                        break;
                case 'd': case 'i':
                {
                        long long i = 0;
                        if (modifier == normal)
                                i = va_arg(values, int);
                        else if (modifier < ll)
                                i = va_arg(values, long);
                        else    i = va_arg(values, long long);
                        vlbdb_bind_int(binder, i);
                        return format;
                }
                case 'c':
                        assert(modifier == normal);
                case 'o': case 'u': case 'x': case 'X':
                {
                        unsigned long long u = 0;
                        if (modifier == normal)
                                u = va_arg(values, unsigned);
                        else if (modifier < ll)
                                u = va_arg(values, unsigned long);
                        else    u = va_arg(values, unsigned long long);
                        vlbdb_bind_uint(binder, u);
                        return format+1;
                }
                case 'f': case 'F': case 'e': case 'E':
                case 'g': case 'G': case 'a': case 'A':
                {
                        double fp = va_arg(values, double);
                        vlbdb_bind_fp(binder, fp);
                        return format;
                }
                case 'p': 
                {
                        void * ptr = va_arg(values, void*);
                        if ((!have_width) || (width == 0)) {
                                vlbdb_bind_ptr(binder, ptr);
                        } else if (width < 0) {
                                assert (0);
                                /* vlbdb_register_range(binder->unit, */
                                /*                      ptr, -width); */
                                /* vlbdb_bind_ptr(binder, ptr); */
                        } else if (width > 0) {
                                vlbdb_bind_range(binder, ptr, width);
                        } else {
                                assert(0);
                        }
                        return format;
                }
                case '%': break;
                default:
                        assert(0);
                        break;
                }
        }
        return format;
}

void vlbdb_vbindf (vlbdb_binder_t * binder, const char * format, va_list values)
{
        for (; *format != '\0'; format++) {
                if (*format == '%')
                        format = bind_one_argf(binder, format+1, values);
        }
        va_end(values);
}

void * vlbdb_specialize (vlbdb_binder_t * binder)
{
        void * ret = vlbdb_specialize_retain(binder);
        vlbdb_binder_destroy(binder);
        return ret;
}
