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
vlbdb_unit_retain (vlbdb_unit_t * unit)
{
        // sticky value
        if (unit->refcount == -1UL) return;
        assert(__sync_fetch_and_add(&unit->refcount, 1) > 0);
}

static void 
unit_destroy (vlbdb_unit_t * unit)
{
        delete unit;
}

int
vlbdb_unit_destroy (vlbdb_unit_t * unit)
{
        if (unit->refcount == -1UL) return 0;
        if (__sync_fetch_and_sub(&unit->refcount, 1) > 1)
                return 0;
        unit_destroy(unit);
        return 1;
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
        // sticky value
        if (binder->refcount == -1UL) return;
        assert(__sync_fetch_and_add(&binder->refcount, 1) > 0);
}

static void 
binder_destroy (vlbdb_binder_t * binder)
{
        delete binder;
}

int
vlbdb_binder_destroy (vlbdb_binder_t * binder)
{
        if (binder->refcount == -1UL) return 0;
        if (__sync_fetch_and_sub(&binder->refcount, 1) > 1)
                return 0;
        binder_destroy(binder);
        return 1;
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
                vlbdb_register_function(unit, NULL, 0, name.c_str());
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

static std::pair<GlobalVariable *, bool>
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

void vlbdb_bindf (vlbdb_binder_t * binder, const char * format, ...)
{
        va_list values;
        va_start(values, format);
        return vlbdb_vbindf(binder, format, values);
}

static const char *
bind_one_argf (vlbdb_binder_t * binder, const char * format, va_list values)
{
        bool have_width = false;
        long width = 0;
        enum {normal, l, z, ll} modifier = normal;
        bool after_point = false;

        for (; *format != '\0'; format++) {
                switch(*format) {
                case '*':
                case '-': case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                        if (after_point) break;
                        have_width = true;
                        if (*format == '*') {
                                width = va_arg(values, int);
                        } else {
                                width = strtol(format, (char**)&format, 10);
                                format--;
                        }
                        break;
                case '.':
                        after_point = true;
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
                        if (!have_width) {
                                vlbdb_bind_ptr(binder, ptr);
                        } else if (width < 0) {
                                vlbdb_register_range(binder->unit,
                                                     ptr, -width);
                                vlbdb_bind_ptr(binder, ptr);
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

void * vlbdb_specialize_retain (vlbdb_binder_t * binder)
{
        vlbdb_unit_t * unit = binder->unit;
        Function * specialized = specialize_call(unit, binder->base, binder->args);
        void * binary = unit->engine->getPointerToFunction(specialized);
        unit->ptr_to_function[binary] = specialized;
        return binary;
}

void * vlbdb_specialize (vlbdb_binder_t * binder)
{
        void * ret = vlbdb_specialize_retain(binder);
        vlbdb_binder_destroy(binder);
        return ret;
}

static Function * 
specialize_call (vlbdb_unit_t * unit, Function * fun, const vector<Value *> &args)
{
        assert(exists(unit->function_to_specialization, fun));
        specialization_info_t info(unit->function_to_specialization[fun]);
        specialization_key_t key(info->key);
        for (size_t i = 0; i < args.size(); i++) {
                Value * arg = args[i];
                if (ConstantExpr * expr = dyn_cast<ConstantExpr>(arg)) {
                        Constant * opt = ConstantFoldConstantExpression(expr,
                                                                        unit->target_data);
                        if (opt)
                                key.second.push_back(opt);
                        else    key.second.push_back(arg);
                } else {
                        key.second.push_back(arg);
                }
        }

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
static Constant *
fold_load_from_global (vlbdb_unit_t * unit, LoadInst * load)
{
        PointerType * type = dyn_cast<PointerType>(load->getType());
        if (!type) return NULL;
        Constant * addr = dyn_cast<Constant>(load->getPointerOperand());
        if (!addr) return NULL;
        Constant * pun
                = ConstantExpr::getBitCast(addr,
                                           PointerType::getUnqual
                                           (IntegerType::get(*unit->context,
                                                             8*sizeof(void*))));
        Constant * constant = ConstantFoldLoadFromConstPtr(pun, unit->target_data);
        if (!constant) return NULL;
        return ConstantExpr::getIntToPtr(constant, type);
}

static Constant *
fold_load_from_memory (vlbdb_unit_t * unit, void * address, Type * type)
{
        if (!(type->isPrimitiveType() || type->isPointerTy()))
                return NULL;
        size_t type_size = unit->target_data->getTypeSizeInBits(type);
        if (type_size > 64) return NULL;
        IntegerType * bit_type = IntegerType::get(*unit->context, type_size);
        ConstantInt * bits = ConstantInt::get(bit_type, *(size_t*)address);
        if (type->isPointerTy())
                return ConstantExpr::getIntToPtr(bits, type);
        return ConstantExpr::getBitCast(bits, type);
}

static Constant *
load_from_frozen_to_interned (vlbdb_unit_t * unit, GlobalVariable * var, size_t offset,
                              Type * ptr_type)
{
        IntegerType * int32 = Type::getInt32Ty(*unit->context);
        ConstantInt * llvm_offset = ConstantInt::get(int32, offset);
        ConstantInt * zero = ConstantInt::get(int32, 0);
        SmallVector<Constant *, 2> idx;
        idx.push_back(zero);
        idx.push_back(llvm_offset);
        Constant * new_target = ConstantExpr::getGetElementPtr(var, idx);
        return ConstantExpr::getBitCast(new_target, ptr_type);
}

static Constant *
fold_load_from_frozen (vlbdb_unit_t * unit, LoadInst * load)
{
        Type * type = load->getType();
        if (!type->isSized()) return NULL;
        Constant * target = dyn_cast<Constant>(load->getPointerOperand());
        if (!target) return NULL;
        Constant * pun
                = ConstantExpr::getPtrToInt(target,
                                            IntegerType::get(*unit->context,
                                                             8*sizeof(void*)));
        if (ConstantExpr * expr = dyn_cast<ConstantExpr>(pun)) {
                pun = ConstantFoldConstantExpression(expr, unit->target_data);
                if (!pun) return NULL;
        }
        ConstantInt * addr = dyn_cast<ConstantInt>(pun);
        if (!addr) return NULL;
        void* address = (void*)addr->getLimitedValue();
        map<void *, GlobalVariable*>::const_iterator it
                = unit->frozen_ranges.lower_bound(address);

        if (it == unit->frozen_ranges.end())  {
                if (it == unit->frozen_ranges.begin())
                        return NULL;
                --it;
        }
        if (it->first > address) {
                if (it == unit->frozen_ranges.begin())
                        return NULL;
                --it;
        }
        assert(it->first <= address);
        GlobalVariable * var = it->second;
        size_t load_size = unit->target_data->getTypeStoreSize(type);
        size_t offset = (char*)address - (char*)(it->first);
        size_t max_constant_size = 0;
        {
                PointerType * ptr = dyn_cast<PointerType>(var->getType());
                assert(ptr);
                ArrayType * data = dyn_cast<ArrayType>(ptr->getElementType());
                assert(data);
                size_t count = data->getNumElements();
                max_constant_size = count - offset;
        }
        if (load_size > max_constant_size)
                return NULL;

        if (Constant * folded = fold_load_from_memory(unit, address, type))
                return folded;

        target = load_from_frozen_to_interned(unit, var,
                                              offset, target->getType());
        if (!target) return NULL;
        load->setOperand(0, target);
        return fold_load_from_global(unit, load);
}

static bool
fold_instruction (vlbdb_unit_t * unit, Instruction * inst,
                  std::set<Instruction *> &worklist,
                  std::set<Function *> &targets)
{
        if (inst->use_empty()) return false;
        Constant * constant 
                = ConstantFoldInstruction(inst, unit->target_data);
        if (!constant) {
                LoadInst * load = dyn_cast<LoadInst>(inst);
                if (!load) return false;
                constant = fold_load_from_global(unit, load);
                if (!constant)
                        constant = fold_load_from_frozen(unit, load);
                if (!constant)
                        return false;
        }
        assert(constant);
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
                        if (constant_prefix && isa<Constant>(arg)) {
                                Constant * con = dyn_cast<Constant>(arg);
                                if (ConstantExpr * expr = dyn_cast<ConstantExpr>(con)) {
                                        if (Constant * optimized
                                            = (ConstantFoldConstantExpression
                                               (expr, unit->target_data)))
                                                con = optimized;
                                }
                                constants.push_back(con);
                        } else {
                                constant_prefix = false;
                        }
                        args.push_back(arg);
                }
        }
        size_t nspecialized = 0;
        Function * specialized = auto_specialize(unit, info,
                                                 constants, nspecialized);
        if (!nspecialized) return call;

        args.erase(args.begin(), args.begin()+nspecialized);
        Twine name("");
        return CallInst::Create(specialized, args, "", call);
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
