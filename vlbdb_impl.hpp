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

#define VLBDB_IN_IMPLEMENTATION
struct binding_unit_impl;
struct binder_impl;

typedef binding_unit_impl vlbdb_unit_t;
typedef binder_impl       vlbdb_binder_t;
#include "vlbdb.hpp"

using namespace llvm;
using std::map;
using std::set;
using std::vector;

struct specialization_key_t : std::pair<Function *, vector<Value *> >
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

struct specialization_info
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

struct binding_unit_impl
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

struct binder_impl
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
                vlbdb_register_function(unit, function, 0, NULL);
                base = unit->ptr_to_function[function];
                fun_type = base->getFunctionType();
                params = fun_type->param_begin();
        }
};

// see http://clang.llvm.org/docs/Block-ABI-Apple.txt

struct Block_literal {
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

static std::pair<GlobalVariable *, bool>
find_intern_range (vlbdb_unit_t *, void *, size_t);

static Function * 
specialize_call (vlbdb_unit_t *, Function *, const vector<Value *> &);

static Function * 
specialize_inner (vlbdb_unit_t *, const specialization_key_t &info,
                  size_t nspecialize);

static Function *
auto_specialize (vlbdb_unit_t * unit, const specialization_info_t &info,
                 const vector<Constant *> &args,
                 size_t &nspecialized);

static bool
optimize_function (vlbdb_unit_t *, Function *,
                   const vector<Value *> &inlinable);

static specialization_info_t
find_specialization_info (vlbdb_unit_t *, Function *);
static specialization_info_t
find_specialization_info (vlbdb_unit_t *, void *);
static Function *
value_to_function (vlbdb_unit_t *, Value *);
#endif
