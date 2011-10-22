#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Value.h"
#include "llvm/Constant.h"
#include "llvm/Constants.h"
#include "llvm/Function.h"

#include "llvm/LLVMContext.h"

#include "llvm/Module.h"
#include "llvm/Support/IRReader.h"
#include "llvm/Support/SourceMgr.h"

#include "llvm/Target/TargetData.h"

#include "llvm/Analysis/Passes.h"
#include "llvm/Transforms/Scalar.h"

#include "llvm/Support/TargetSelect.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"

#include <map>
#include <vector>
#include <utility>
#include <string>
#include <assert.h>
#include <iostream>
#include <cstdio>
#include <dlfcn.h>
#include <cxxabi.h>
#include <algorithm>

#include "vlbdb_impl.hpp"

static std::pair<GlobalVariable *, bool>
find_intern_range (vlbdb_unit_t *, void *, size_t);

static void retain_base (base_impl * obj)
{
        // sticky count
        if (obj->refcount == -1UL) return;
        // watch for overflow
        assert(__sync_fetch_and_add(&obj->refcount, 1) != -1UL);
}

template <typename T>
static int destroy_base (T * obj)
{
        if (obj->refcount == -1UL) return 0;
        if (__sync_fetch_and_sub(&obj->refcount, 1) > 1)
                return 0;
        delete obj;
        return 1;
}

// TODO: load all symbols in bitcode.
vlbdb_unit_t * 
vlbdb_unit_from_bitcode (const char * file, void * context_)
{
        InitializeNativeTarget();
        LLVMContext * context = (LLVMContext*)context_;
        if (!context) 
                context = &getGlobalContext();
        SMDiagnostic error;
        Module * module = ParseIRFile(file, error, *context);
        if (!module) {
                std::cerr << "Error " << error.getMessage() << std::endl;
                exit(1);
        }
        std::string ErrStr;
        ExecutionEngine * engine = (EngineBuilder(module)
                                    .setErrorStr(&ErrStr)
                                    .setOptLevel(CodeGenOpt::Less)
                                    .setRelocationModel(Reloc::Static)
                                    .create());
        if (!engine) {
                fprintf(stderr, "Could not create ExecutionEngine: %s\n", ErrStr.c_str());
                exit(1);
        }

        vlbdb_unit_t * unit = new vlbdb_unit_t(context, module, engine);

        FunctionPassManager &fpm(unit->fpm);
        fpm.add(new TargetData(*engine->getTargetData()));
        //fpm.add(createPromoteMemoryToRegisterPass());
        fpm.add(createSCCPPass());
        fpm.add(createAggressiveDCEPass());
        fpm.add(createBasicAliasAnalysisPass());
        fpm.add(createInstructionCombiningPass());
        fpm.add(createReassociatePass());
        fpm.add(createGVNPass());
        fpm.add(createCFGSimplificationPass());
        fpm.doInitialization();

        return unit;
}

void
vlbdb_unit_retain (vlbdb_unit_t * unit)
{
        retain_base(unit);
}

int
vlbdb_unit_destroy (vlbdb_unit_t * unit)
{
        return destroy_base(unit);
}

vlbdb_binder_t * 
vlbdb_binder_create (vlbdb_unit_t * unit, void * base)
{
        return new vlbdb_binder_t(unit, base);
}

vlbdb_binder_t *
vlbdb_binder_create_block (vlbdb_unit_t * unit, void * ptr)
{
        Block_literal * block = (Block_literal*)ptr;
        vlbdb_binder_t * binder = vlbdb_binder_create(unit, block->invoke);
        vlbdb_bind_range(binder, ptr, block->descriptor->size);
        return binder;
}

vlbdb_binder_t *
vlbdb_binder_copy (vlbdb_binder_t * binder)
{
        return new binder_impl(*binder);
}

void
vlbdb_binder_retain (vlbdb_binder_t * binder)
{
        retain_base(binder);
}

int
vlbdb_binder_destroy (vlbdb_binder_t * binder)
{
        return destroy_base(binder);
}

unsigned
vlbdb_register_all_functions (vlbdb_unit_t * unit)
{
        unsigned count = 0;
        for (Module::iterator it = unit->module->begin(),
                     end = unit->module->end();
             it != end; ++it, count++) {
                Function * fun = &*it;
                if (fun->isDeclaration()) continue;
                if (!fun->hasExternalLinkage()) continue;
                std::string name(it->getNameStr());
                const char * cname = name.c_str();
                if (void * function = dlsym(RTLD_DEFAULT, cname))
                        vlbdb_register_function(unit, function, 0, cname);
        }

        return count;
}

void 
vlbdb_register_function (vlbdb_unit_t * unit, void * function,
                         size_t nspecialize, const char * name)
{
        assert(function || name);
        if (function == NULL)
                function = dlsym(RTLD_DEFAULT, name);

        if (function && exists(unit->ptr_to_function, function)) {
                if (nspecialize == 0) return;
                Function * LF = unit->ptr_to_function[function];
                specialization_info_t info(unit->function_to_specialization[LF]);
                info->nauto_specialize = std::max(info->nauto_specialize,
                                                  nspecialize);
                return;
        }

        if (name == NULL) {
                Dl_info info;
                assert(dladdr(function, &info));
                name = info.dli_sname;
        }

        Function * LF = unit->module->getFunction(name);
        if (!LF) {
                int status = 0;
                char * demangled = abi::__cxa_demangle(name, NULL, 
                                                       NULL, &status);
                if (!status) {
                        fprintf(stderr, 
                                "Unable to find bitcode for function %s\n",
                                demangled);
                } else {
                        fprintf(stderr,
                                "Unable to find bitcode for function %s (%p)\n",
                                name, function);
                        }
                free(demangled);
                assert(LF);
        }
        unit->fpm.run(*LF);
        if (function)
                unit->ptr_to_function[function] = LF;
        specialization_key_t key(LF);
        specialization_info_t info(new specialization_info(key, nspecialize, LF));
        unit->specializations[key] = info;
        unit->function_to_specialization[LF] = info;
}

void 
vlbdb_register_function_name (vlbdb_unit_t * unit, const char * name, 
                              size_t nspecialize)
{
        assert(name);
        return vlbdb_register_function(unit, NULL, nspecialize, name);
}

void
vlbdb_register_block (vlbdb_unit_t * unit, void * ptr, size_t nspecialize)
{
        Block_literal * block = (Block_literal*)ptr;
        return vlbdb_register_function(unit, block->invoke, nspecialize+1, NULL);
}

std::pair<GlobalVariable *, bool>
find_intern_range (vlbdb_unit_t * unit, void * ptr, size_t size)
{
        vector<unsigned char> contents((unsigned char*)ptr,
                                       (unsigned char*)ptr+size);
        if (exists(unit->interned_ranges, contents))
                return std::make_pair(unit->interned_ranges[contents],
                                      false);

        Type * char_type = IntegerType::get(*unit->context, 8);
        ArrayType * type = ArrayType::get(char_type, size);
        Constant * llvm_contents;
        {
                vector<Constant *> constants(size);
                for (size_t i = 0; i < size; i++)
                        constants[i] = ConstantInt::get(char_type,
                                                        contents[i]);
                llvm_contents = ConstantArray::get(type, constants);
        }
        Twine name("constant");
        GlobalVariable * var
                = new GlobalVariable(*unit->module, type,
                                     true, Function::InternalLinkage,
                                     llvm_contents,
                                     name);
        unit->interned_ranges[contents] = var;
        return std::make_pair(var, true);
}

int vlbdb_intern_range (vlbdb_unit_t * unit, void * ptr, size_t size)
{
        return find_intern_range(unit, ptr, size).second;
}

int vlbdb_register_range (vlbdb_unit_t * unit, void * ptr, size_t size)
{
        // not quite, we want to merge ranges, etc.
        if (exists(unit->frozen_ranges, ptr))
                return false;

        GlobalVariable * var
                = find_intern_range(unit, ptr, size).first;
        unit->frozen_ranges[ptr] = var;
        return true;
}

void vlbdb_bind_uint (vlbdb_binder_t * binder, unsigned long long x)
{
        assert(binder->params != binder->fun_type->param_end());
        binder->args.push_back(ConstantInt::get(*binder->params, x, false));
        ++binder->params;
}

void vlbdb_bind_int (vlbdb_binder_t * binder, long long x)
{
        assert(binder->params != binder->fun_type->param_end());
        binder->args.push_back(ConstantInt::get(*binder->params, x, true));
        ++binder->params;
}

void vlbdb_bind_fp (vlbdb_binder_t * binder, double x)
{
        assert(binder->params != binder->fun_type->param_end());
        binder->args.push_back(ConstantFP::get(*binder->params, x));
        ++binder->params;
}

void vlbdb_bind_ptr (vlbdb_binder_t * binder, void * x)
{
        assert(binder->params != binder->fun_type->param_end());
        PointerType * ptr = dyn_cast<PointerType>(*binder->params);
        assert(ptr);
        ++binder->params;
        Constant * arg = 0;
        if (x == NULL) {
                arg = ConstantPointerNull::get(ptr);
        } else if (exists(binder->unit->ptr_to_function, x)) {
                Function * LF = binder->unit->ptr_to_function[x];
                arg = ConstantExpr::getBitCast(LF, ptr);
        } else {
                unsigned long long address = (unsigned long long)x;
                Constant * addr 
                        = ConstantInt::get(IntegerType::get(*binder->unit->context,
                                                            8*sizeof(address)),
                                           address);
                arg = ConstantExpr::getIntToPtr(addr, ptr);
        }
        binder->args.push_back(arg);
}

void vlbdb_bind_range(vlbdb_binder_t * binder, void * address, size_t size)
{
        assert(binder->params != binder->fun_type->param_end());
        PointerType * ptr = dyn_cast<PointerType>(*binder->params);
        assert(ptr);
        ++binder->params;
        GlobalVariable * var = find_intern_range(binder->unit,
                                                 address, size).first;
        binder->args.push_back(ConstantExpr::getBitCast(var, ptr));
}

void * vlbdb_specialize_retain (vlbdb_binder_t * binder)
{
        vlbdb_unit_t * unit = binder->unit;
        Function * specialized = specialize_call(unit, binder->base, binder->args);
        void * binary = unit->engine->getPointerToFunction(specialized);
        unit->ptr_to_function[binary] = specialized;
        return binary;
}
