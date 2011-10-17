#include "llvm/Type.h"
#include "llvm/Value.h"
#include "llvm/Function.h"
#include "llvm/BasicBlock.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/LLVMContext.h"
#include "llvm/Support/IRReader.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/PassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"

#include "llvm/Support/TargetSelect.h"
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

static void * 
specialize_call (vlbdb_unit_t *, Function *, const vector<Value *> &);

static Function * 
specialize_inner (vlbdb_unit_t *, const specialization_key_t &info);

template <typename Map, typename Key>
static bool exists (const Map &map, const Key &key)
{
        return map.find(key) != map.end();
}

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
                                    .create());
        if (!engine) {
                fprintf(stderr, "Could not create ExecutionEngine: %s\n", ErrStr.c_str());
                exit(1);
        }

        vlbdb_unit_t * unit = new vlbdb_unit_t(context, module, engine);

        FunctionPassManager &fpm(unit->fpm);
        fpm.add(new TargetData(*engine->getTargetData()));
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
vlbdb_unit_destroy (vlbdb_unit_t * unit)
{
        delete unit;
}

vlbdb_binder_t * 
vlbdb_binder_create (vlbdb_unit_t * unit, void * base)
{
        return new vlbdb_binder_t(unit, base);
}

vlbdb_binder_t *
vlbdb_binder_copy (vlbdb_binder_t * binder)
{
        return new binder_impl(*binder);
}

void 
vlbdb_binder_destroy (vlbdb_binder_t * binder)
{
        delete binder;
}

void 
vlbdb_register_function (vlbdb_unit_t * unit, void * function,
                         size_t nspecialize, const char * name)
{
        assert(function || name);
        if (exists(unit->ptr_to_function, function)) {
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

void * vlbdb_specialize(vlbdb_binder_t * binder)
{
        return specialize_call(binder->unit, binder->base, binder->args);
}

void * 
specialize_call (vlbdb_unit_t * unit, Function * fun, const vector<Value *> &args)
{
        assert(exists(unit->function_to_specialization, fun));
        specialization_key_t key(unit->function_to_specialization[fun]->key);
        key.second.insert(key.second.end(), args.begin(), args.end());

        Function * specialized = specialize_inner(unit, key);
        void * binary = unit->engine->getPointerToFunction(specialized);
        unit->ptr_to_function[binary] = specialized;
        return binary;
}

Function *
specialize_inner (vlbdb_unit_t * unit, const specialization_key_t &key)
{
        if (exists(unit->specializations, key))
                return unit->specializations[key]->specialized;

        Function * base = key.first;
        const vector<Value *> &args = key.second;

        specialization_info_t info(new specialization_info(key, 0, 0));

        vector<Type *> remaining_types;
        vector<Argument *> remaining_args;
        ValueToValueMapTy substitution;
        {
                Function::arg_iterator base_arg = base->arg_begin();
                for (size_t i = 0; i < args.size(); i++, ++base_arg)
                        substitution[base_arg] = args[i];
                for (; base_arg != base->arg_end(); ++base_arg) {
                        remaining_types.push_back(base_arg->getType());
                        remaining_args.push_back(base_arg);
                }
        }

        FunctionType * specialized_type = FunctionType::get(base->getReturnType(),
                                                            remaining_types,
                                                            base->isVarArg());
        Function * specialized_fun = Function::Create(specialized_type,
                                                      Function::ExternalLinkage,
                                                      "", unit->module);
        {
                Function::arg_iterator runtime_arg = specialized_fun->arg_begin();
                for (size_t i = 0; i < remaining_args.size(); i++, ++runtime_arg)
                        substitution[remaining_args[i]] = runtime_arg;
        }

        SmallVector<ReturnInst *, 4> returns;
        CloneAndPruneFunctionInto(specialized_fun, base,
                                  substitution, false, returns, "");

        verifyFunction(*specialized_fun);
        info->specialized = specialized_fun;

        unit->specializations[key] = info;
        unit->function_to_specialization[specialized_fun] = info;

        unit->fpm.run(*specialized_fun);

        return specialized_fun;
}

