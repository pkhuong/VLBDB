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
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/InstIterator.h"

#include <map>
#include <set>
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
        vlbdb_unit_t * unit = binder->unit;
        Function * specialized = specialize_call(unit, binder->base, binder->args);
        void * binary = unit->engine->getPointerToFunction(specialized);
        unit->ptr_to_function[binary] = specialized;
        return binary;
}

static Function * 
specialize_call (vlbdb_unit_t * unit, Function * fun, const vector<Value *> &args)
{
        assert(exists(unit->function_to_specialization, fun));
        specialization_info_t info(unit->function_to_specialization[fun]);
        specialization_key_t key(info->key);
        key.second.insert(key.second.end(), args.begin(), args.end());

        size_t nspecialize = info->nauto_specialize;
        if (nspecialize > args.size())
                nspecialize -= args.size();
        else    nspecialize = 0;
        Function * specialized = specialize_inner(unit, key, nspecialize);
        return specialized;
}

static Function *
specialize_inner (vlbdb_unit_t * unit, const specialization_key_t &key,
                  size_t nspecialize)
{
        if (exists(unit->specializations, key))
                return unit->specializations[key]->specialized;

        Function * base = key.first;
        const vector<Value *> &args = key.second;

        specialization_info_t info(new specialization_info(key, nspecialize, 0));

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

        optimize_function(unit, specialized_fun, args);
        verifyFunction(*specialized_fun);
        unit->fpm.run(*specialized_fun);

        specialized_fun->dump();

        return specialized_fun;
}

static specialization_info_t
find_specialization_info (vlbdb_unit_t * unit, Function * fun)
{
        if (exists(unit->function_to_specialization, fun))
                return unit->function_to_specialization[fun];

        return specialization_info_t();
}

static specialization_info_t
find_specialization_info (vlbdb_unit_t * unit, void * fun)
{
        if (exists(unit->ptr_to_function, fun))
                return find_specialization_info(unit,
                                                unit->ptr_to_function[fun]);
        return specialization_info_t();
}

static Function *
auto_specialize (vlbdb_unit_t * unit, const specialization_info_t &info,
                 const vector<Constant *> &args,
                 size_t &nspecialized)
{
        specialization_key_t key(info->key);
        vector<Value *> &key_args(key.second);
        key_args.insert(key_args.end(), args.begin(), args.end());
        size_t nauto_specialize = info->nauto_specialize;
        for (nspecialized = args.size();
             nspecialized > nauto_specialize;
             key_args.pop_back(), nspecialized--) {
                if (exists(unit->specializations, key))
                        return unit->specializations[key]->specialized;
        }

        assert(nauto_specialize >= nspecialized);
        return specialize_inner(unit, key, nauto_specialize - nspecialized);
}

// Aggressive constant propagation and selective inlining
//  The only specialized optimisation pass (:
//
// The inlinable set makes this difficult to implement as a
// FunctionPass
//
// Assume that all bblocks are dead and values constants.  All call
// sites in the original Function are considered for inlining;
// however, only callees that were in arguments or constant address
// ranges will be inlined (others are still up for specialization).
// 
// This is most of SCCP and ADCE, with more agressive constant
// propagation for memory accesses, and an iterative inlining pass.

// Do this simply for now, no co-inductive process.
// Just straightforward inductive cprop.

static std::set<CallInst *> find_call_sites (Function * fun)
{
        std::set<CallInst *> ret;
        for (inst_iterator it = inst_begin(fun), end = inst_end(fun);
             it != end; ++it) {
                CallInst * call = dyn_cast<CallInst>(&*it);
                if (call)
                        ret.insert(call);
        }
        return ret;
}

// Next two functions are probably heavy handed. Revisit.
static Function *
ptr_cast_to_function (vlbdb_unit_t * unit, Constant * constant)
{
retry:
        if (!constant) return NULL;

        if (Function * fun = dyn_cast<Function>(constant))
                return fun;
        if (ConstantInt * addr = dyn_cast<ConstantInt>(constant)) {
                void* address = (void*)addr->getLimitedValue();
                specialization_info_t info
                        (find_specialization_info(unit, address));
                if (!info)
                        return NULL;
                return info->specialized;
        }
        if (!constant->getType()->isPointerTy()) return NULL;

        if (ConstantExpr * expr = dyn_cast<ConstantExpr>(constant)) {
                if (expr->isCast()) {
                        Value * operand = expr->getOperand(0);
                        constant = dyn_cast<Constant>(operand);
                        goto retry;
                }
        }

        return NULL;
}

static Function *
value_to_function (vlbdb_unit_t * unit, Value * value)
{
        if (!value) return NULL;
        Constant * constant = dyn_cast<Constant>(value);
        if (!constant) return NULL;

        if (Function * fun = ptr_cast_to_function(unit, constant))
                return fun;

        if (ConstantExpr * expr = dyn_cast<ConstantExpr>(constant)) {
                Constant * constant
                        = ConstantFoldConstantExpression(expr,
                                                         unit->target_data);
                if (Function * fun = ptr_cast_to_function(unit, constant))
                        return fun;
        }

        return NULL;
}

static bool
maybe_insert_call_target (vlbdb_unit_t * unit,
                          Value * value, std::set<Function *> &targets)
{
        Function * function = value_to_function(unit, value);
        if (!function) return false;
        if (specialization_info_t info
            = find_specialization_info(unit, function))
                return targets.insert(info->key.first).second;
        return false;
}

// returns true if the instruction has been folded away
// TODO: handle read into known-constant space
static bool
fold_instruction (vlbdb_unit_t * unit, Instruction * inst,
                  std::set<Instruction *> &worklist,
                  std::set<Function *> &targets)
{
        if (inst->use_empty()) return false;
        Constant * constant 
                = ConstantFoldInstruction(inst, unit->target_data);
        if (!constant)
                return false;
        maybe_insert_call_target(unit, constant, targets);

        for (Value::use_iterator use = inst->use_begin(),
                     end = inst->use_end();
             use != end; ++use)
                worklist.insert(cast<Instruction>(*use));
        worklist.erase(inst);
        inst->replaceAllUsesWith(constant);
        inst->eraseFromParent();
        return true;
}

static CallInst *
process_call (vlbdb_unit_t * unit, CallInst * call)
{
        Function * callee 
                = value_to_function(unit, call->getCalledValue());
        if (!callee) return call;
        if (call->getCalledFunction() != callee) {
                // Or we could flame the user.
                if (callee->getType() != call->getCalledValue()->getType())
                        return call;

                call->setCalledFunction(callee);
        }

        specialization_info_t info(find_specialization_info
                                   (unit, callee));
        if (!info) return call;
        vector<Constant *> constants;
        vector<Value *> args;
        {
                bool constant_prefix = true;
                for (size_t i = 0; i < call->getNumArgOperands(); i++) {
                        Value * arg = call->getArgOperand(i);
                        if (constant_prefix && isa<Constant>(arg))
                                constants.push_back(dyn_cast<Constant>(arg));
                        else    constant_prefix = false;
                        args.push_back(arg);
                }
        }
        size_t nspecialized = 0;
        Function * specialized = auto_specialize(unit, info,
                                                 constants, nspecialized);
        if (!nspecialized) return call;

        args.erase(args.begin(), args.begin()+nspecialized);
        Twine name("");
        return CallInst::Create(specialized, args, name, call);
}

static bool
inline_call_or_not (vlbdb_unit_t * unit, CallInst * call,
                    const std::set<Function *> &targets)
{
        specialization_info_t info(find_specialization_info
                                   (unit, call->getCalledFunction()));
        return (info && exists(targets, info->key.first));
}

static void
insert_basic_block_in_list (BasicBlock * bb, std::set<Instruction *> &worklist)
{
        for (BasicBlock::iterator inst = bb->begin(), end = bb->end();
             inst != end; ++inst)
                worklist.insert(&*inst);
}

static void
insert_new_basic_blocks_in_list (Function * fun, std::set<BasicBlock *> &known,
                                 std::set<Instruction *> &worklist)
{
        for (Function::iterator bb = fun->begin(), end = fun->end();
             bb != end; ++bb)
                if (known.insert(&*bb).second)
                        insert_basic_block_in_list(bb, worklist);
}

static void
inline_call (vlbdb_unit_t * unit, CallInst * call,
             std::set<BasicBlock *> &known_bb,
             std::set<Instruction *> &worklist)
{
        (void)unit;
        BasicBlock * original_bb = call->getParent();
        Function * fun = original_bb->getParent();

        InlineFunctionInfo IFI(NULL, unit->target_data);
        if (InlineFunction(call, IFI)) {
                insert_basic_block_in_list(original_bb, worklist);
                insert_new_basic_blocks_in_list(fun, known_bb, worklist);
        }
}

static bool
optimize_function (vlbdb_unit_t * unit, Function * fun,
                   const vector<Value *> &args)
{
        std::set<CallInst *> inline_sites(find_call_sites(fun));
        std::set<Function *> inlinable_targets;
        for (size_t i = 0; i < args.size(); i++)
                maybe_insert_call_target(unit, args[i], inlinable_targets);

        std::set<BasicBlock *> known_bb;
        std::set<Instruction *> worklist;

        insert_new_basic_blocks_in_list(fun, known_bb, worklist);

        bool any_change = false;
        while (!worklist.empty()) {
                Instruction * inst = *worklist.begin();
                worklist.erase(worklist.begin());
                if (fold_instruction(unit, inst, worklist,
                                     inlinable_targets)) {
                        any_change = true;
                        continue;
                }
                CallInst * call = dyn_cast<CallInst>(inst);
                if (!call) continue;

                {
                        CallInst * new_call = process_call(unit, call);
                        if (new_call != call) {
                                if (exists(inline_sites, call)) {
                                        inline_sites.erase(call);
                                        inline_sites.insert(new_call);
                                }
                                call->replaceAllUsesWith(new_call);
                                call->eraseFromParent();
                                call = new_call;
                                any_change = true;
                        }
                }
                if (exists(inline_sites, call) &&
                    inline_call_or_not(unit, call, inlinable_targets)) {
                        inline_sites.erase(call);
                        inline_call(unit, call, known_bb, worklist);
                        any_change = true;
                }
        }

        return any_change;
}
