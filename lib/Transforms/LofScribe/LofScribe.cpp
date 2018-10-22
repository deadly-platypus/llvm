//
// Created by derrick on 8/31/18.
//

#include "LofScribe.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/PassRegistry.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Analysis/MemoryBuiltins.h"

#include <set>
#include <cassert>
#include <iostream>

using namespace llvm;

#define DEBUG_TYPE "lofscribe"

LofScribePass::LofScribePass() : FunctionPass(ID) {}

bool LofScribePass::isSupportedType(llvm::Type *type) {
    if(type->isPointerTy() ||
            type->isDoubleTy() ||
            type->isIntegerTy() ||
            type->isFloatTy() ||
            type->isX86_FP80Ty() ||
            type->isVoidTy()) {
        return true;
    }

    return false;
}

bool LofScribePass::isSupported(llvm::CallInst *ci) {
    if(isSupportedType(ci->getType())) {
        if(ci->getCalledFunction() && !ci->getCalledFunction()->getName().empty()) {
            StringRef name = ci->getCalledFunction()->getName();
            /* We do not support setjmp or longjmp yet */
            if(name.contains("setjmp") || name.contains("longjmp")) {
                return false;
            }
        }
        for(Value* arg : ci->arg_operands()) {
            if(!isSupportedType(arg->getType())) {
                return false;
            }
        }
        return true;
    }

    return false;
}

Value* LofScribePass::CreateBitCast(Value* orig, IRBuilder<> &IRB) {
    Value* result;
    if(orig->getType()->isPointerTy()) {
        result = IRB.CreateBitCast(orig, IRB.getInt8PtrTy());
    } else if(orig->getType()->isFloatTy()) {
        result = IRB.CreateIntToPtr(
                IRB.CreateFPToUI(orig, IRB.getInt32Ty()),
                IRB.getInt8PtrTy());
    } else if(orig->getType()->isIntegerTy()) {
        result = IRB.CreateIntToPtr(orig, IRB.getInt8PtrTy());
    } else if(orig->getType()->isDoubleTy()) {
        result = IRB.CreateIntToPtr(
                IRB.CreateFPToUI(orig, IRB.getInt64Ty()),
                IRB.getInt8PtrTy());
    } else if(orig->getType()->isX86_FP80Ty()) {
        result = IRB.CreateIntToPtr(
                IRB.CreateFPToUI(orig, IRB.getInt128Ty()),
                IRB.getInt8PtrTy());
    } else {
        result = IRB.CreatePointerCast(orig, IRB.getInt8PtrTy());
    }

    return result;
}

bool LofScribePass::runOnFunction(Function &F) {
    LLVM_DEBUG(dbgs() << "Leap of Faith Scribe starting for "
            << F.getName() << "\n");
    std::set<CallInst*> callset;
    
    for(inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        if(CallInst* ci = dyn_cast<CallInst>(&*I)) {
            /* We don't want to instrument functions like llvm.dbg.declare */
            if(ci->getCalledFunction() && ci->getCalledFunction()->isIntrinsic()) {
                LLVM_DEBUG(dbgs() << "Intrinsic function");
                continue;
            } else if(!isSupported(ci)) {
                LLVM_DEBUG(dbgs() << "Unsupported function");
                continue;
            }
            callset.insert(ci);
        }
    }

    LLVM_DEBUG(dbgs() << "Found "
            << callset.size()
            << " call instructions for function "
            << F.getName()
            << "\n");

    Type *voidTy = Type::getVoidTy(F.getContext());
    Type *int32Ty = Type::getInt32Ty(F.getContext());
    Type *int64Ty = Type::getInt64Ty(F.getContext());
    Type *voidStarTy = Type::getInt8PtrTy(F.getContext());
    Type *doubleTy = Type::getDoubleTy(F.getContext());

    /* Variadic function for handling different data types */
    FunctionType *record_post_ty = FunctionType::get(voidTy, { int32Ty, int64Ty, voidStarTy }, false);
    FunctionType *double_record_ty = FunctionType::get(voidTy, {int32Ty, int64Ty, doubleTy}, false);

    /* lof_precall provides the number of arguments to a called function,
     * and is followed up by zero or more lof_record_arg calls */
    Function* precall = dyn_cast<Function>(
            F.getParent()->getOrInsertFunction("lof_precall", voidTy, voidStarTy ));

    /* lof_record_arg provides a single function call argument to the runtime 
     * component.  It converts everything to a 64-bit integer, and indicates 
     * if the argument is a pointer or not */
    Function* record_arg = dyn_cast<Function>(
            F.getParent()->getOrInsertFunction("lof_record_arg", record_post_ty ));

    Function* record_double_arg = dyn_cast<Function>(
            F.getParent()->getOrInsertFunction("lof_double_record_arg", double_record_ty)
    );
   
    /* lof_postcall records the return value of the function call, records if the return value
     * is a pointer, and then records all changes to pointers */
    Function* postcall = dyn_cast<Function>(
            F.getParent()->getOrInsertFunction("lof_postcall", record_post_ty ));

    Function* postcall_double_arg = dyn_cast<Function>(
            F.getParent()->getOrInsertFunction("lof_double_postcall", double_record_ty)
            );

    assert(precall != nullptr);
    assert(record_arg != nullptr);
    assert(postcall != nullptr);

    for(std::set<CallInst*>::iterator it = callset.begin(); it != callset.end(); ++it) {
        CallInst* ci = *it;
        IRBuilder<> IRB(ci);

        if(ci->getCalledFunction()) {
            LLVM_DEBUG(dbgs() << "Found call to "
                              << ci->getCalledFunction()->getName()
                              << "\n");
        } else {
            LLVM_DEBUG(dbgs() << "Found call to indirect function\n");
        }

        if(!ci->getCalledFunction() || ci->getCalledFunction()->getName().empty()) {
            continue;
        }

        Constant* name = ConstantDataArray::getString(F.getContext(), ci->getCalledFunction()->getName(), true);
        Value* stack_name = IRB.CreateAlloca(name->getType());
        IRB.CreateStore(name, stack_name);
        Value* name_ptr = IRB.CreateGEP(stack_name, { IRB.getInt32(0), IRB.getInt32(0) });

        IRB.CreateCall(precall, { name_ptr });

        /* Reverse order because runtime library uses FIFO stack to store arguments */
        for(unsigned i = ci->getNumArgOperands(); i > 0; i--) {
            Value* arg = ci->getArgOperand(i - 1);
            Value* args[3];
            args[0] = IRB.getInt32(arg->getType()->getTypeID());
            if(PointerType *pt = dyn_cast<PointerType>(arg->getType())) {
                args[1] = IRB.getInt64(pt->getElementType()->getPrimitiveSizeInBits());
            }
            else {
                args[1] = IRB.getInt64(arg->getType()->getPrimitiveSizeInBits());
            }

            if(arg->getType()->isFloatingPointTy()) {
                args[2] = IRB.CreateFPCast(arg, doubleTy);
                IRB.CreateCall(record_double_arg, args);
            } else {
                args[2] = CreateBitCast(arg, IRB);
                IRB.CreateCall(record_arg, args);
            }
        }
        BasicBlock::iterator bbit(ci);
        bbit++;
        IRB.SetInsertPoint(&*bbit);

        Value* args[3];
        args[0] = IRB.getInt32(ci->getType()->getTypeID());
        if(PointerType *pt = dyn_cast<PointerType>(ci->getType())) {
            args[1] = IRB.getInt64(pt->getElementType()->getPrimitiveSizeInBits());
        } else {
            args[1] = IRB.getInt64(ci->getType()->getPrimitiveSizeInBits());
        }
        if(ci->getType()->isFloatingPointTy()) {
            args[2] = IRB.CreateFPCast(ci, doubleTy);
            IRB.CreateCall(postcall_double_arg, args);
        } else {
            if(ci->getType()->isVoidTy()) {
                args[2] = IRB.CreateIntToPtr(IRB.getInt32(0), voidStarTy);
            } else {
                args[2] = CreateBitCast(ci, IRB);
            }
            IRB.CreateCall(postcall, args);
        }
    }

    return true;
}

char LofScribePass::ID = 0;

static RegisterPass<LofScribePass> X("lofscribe", "Leap of Faith Scribe Pass",
                             false ,
                             false );

static void registerMyPass(const PassManagerBuilder &,
                           legacy::PassManagerBase &PM) {
    PM.add(new LofScribePass());
}
static RegisterStandardPasses
    RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                   registerMyPass);
