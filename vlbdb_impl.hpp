#ifndef VLBDB_IMPL_HPP
#define VLBDB_IMPL_HPP
#include "llvm/Value.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/LLVMContext.h"
#include "llvm/PassManager.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"

#include <map>
#include <vector>
#include <utility>
#include <assert.h>

#define VLBDB_IN_IMPLEMENTATION
struct binding_unit_impl;
struct binder_impl;

typedef binding_unit_impl vlbdb_unit_t;
typedef binder_impl       vlbdb_binder_t;
#include "vlbdb.hpp"

using namespace llvm;
using std::map;
using std::vector;

#define INTERNAL __attribute__((visibility("hidden")))

INTERNAL struct
specialization_key_t : std::pair<Function *, vector<Value *> >
{
        specialization_key_t ()
        {
                first = NULL;
                second.clear();
        }

        explicit specialization_key_t (Function * fun)
        {
                first = fun;
                second.clear();
        }
        
        specialization_key_t (Function * fun, const vector<Value *> &values)
        {
                first  = fun;
                second = values;
        }
};

INTERNAL struct specialization_info
{
        specialization_key_t key;
        size_t nauto_specialize;
        Function * specialized;

        specialization_info (const specialization_key_t &key_,
                             size_t nauto_specialize_,
                             Function * specialized_)
                : key(key_),
                  nauto_specialize(nauto_specialize_),
                  specialized(specialized_)
        {}
};

typedef std::tr1::shared_ptr<specialization_info> specialization_info_t;

INTERNAL struct base_impl
{
        size_t refcount;
        int status;
        explicit base_impl (size_t count = 1, int status_ = 0)
                : refcount(count),
                  status(status_)
        {}
};

INTERNAL struct binding_unit_impl : public base_impl
{
        typedef vector<unsigned char> bytestring;
        LLVMContext * context;
        Module * module;
        ExecutionEngine * engine;
        const TargetData * target_data;
        FunctionPassManager fpm;

        map<void *, Function *> ptr_to_function;
        map<specialization_key_t, specialization_info_t> specializations;
        map<Function *, specialization_info_t> function_to_specialization;
        map<bytestring, GlobalVariable *> interned_ranges;
        map<void *, GlobalVariable *> frozen_ranges;

        binding_unit_impl (LLVMContext * context_, 
                           Module * module_,
                           ExecutionEngine * engine_)
                : context(context_),
                  module(module_),
                  engine(engine_),
                  target_data(engine->getTargetData()),
                  fpm(module)
        {}
};

INTERNAL struct binder_impl  : public base_impl
{
        binding_unit_impl * unit;
        Function * base;
        vector<Value *> args;
        const FunctionType * fun_type;
        FunctionType::param_iterator params;

        binder_impl (binding_unit_impl * unit_,
                     void * function)
                : unit(unit_)
        {
                assert(unit->ptr_to_function.find(function)
                       != unit->ptr_to_function.end());
                vlbdb_unit_retain(unit);
                base = unit->ptr_to_function[function];
                fun_type = base->getFunctionType();
                params = fun_type->param_begin();
        }

        ~binder_impl () 
        {
                vlbdb_unit_destroy(unit);
        }
};

// see http://clang.llvm.org/docs/Block-ABI-Apple.txt

INTERNAL struct Block_literal {
        void *isa; // initialized to &_NSConcreteStackBlock or &_NSConcreteGlobalBlock
        int flags;
        int reserved; 
        void *invoke;
        struct Block_descriptor {
                unsigned long int reserved;	// NULL
                unsigned long int size;         // sizeof(struct Block_literal)
                // optional helper functions
                void (*copy_helper)(void *dst, void *src);     // IFF (1<<25)
                void (*dispose_helper)(void *src);             // IFF (1<<25)
                // required ABI.2010.3.16
                const char *signature;                         // IFF (1<<30)
        } *descriptor;
        // tail
};

template <typename Map, typename Key>
static bool exists (const Map &map, const Key &key)
{
        return map.find(key) != map.end();
}

INTERNAL Function * 
specialize_call (vlbdb_unit_t *, Function *, const vector<Value *> &);
#endif
