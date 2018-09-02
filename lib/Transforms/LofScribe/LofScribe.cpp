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

#include <set>
#include <cassert>
#include <iostream>

using namespace llvm;

#define DEBUG_TYPE "lofscribe"

LofScribePass::LofScribePass() : FunctionPass(ID) {}

Value* LofScribePass::CreateBitCast(Value* orig, IRBuilder<> &IRB) {
    Value* result;
    if(isa<BitCastInst>(orig)) {
        result = orig;
    } else if(orig->getType()->isPointerTy()) {
        result = IRB.CreateBitCast(orig, IRB.getInt8PtrTy());
    } else {
        result = IRB.CreateIntToPtr(orig, IRB.getInt8PtrTy());
    }

    return result;
}

bool LofScribePass::runOnFunction(Function &F) {
    LLVM_DEBUG(dbgs() << "Leap of Faith Scribe starting...\n");
    std::set<CallInst*> callset;
    
    for(inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        I->dump();
        if(CallInst* ci = dyn_cast<CallInst>(&*I)) {
            callset.insert(ci);
        }
    }

    LLVM_DEBUG(dbgs() << "Found "
            << callset.size()
            << " call instructions for function "
            << F.getName()
            << "\n");

    Type *voidTy = Type::getVoidTy(F.getContext());
    Type *boolTy = Type::getInt1Ty(F.getContext());
    Type *charTy = Type::getInt8Ty(F.getContext());
    Type *voidStarTy = Type::getInt8PtrTy(F.getContext());

    /* lof_precall provides the number of arguments to a called function,
     * and is followed up by zero or more lof_record_arg calls */
    Function* precall = dyn_cast<Function>(
            F.getParent()->getOrInsertFunction("lof_precall", voidTy, charTy ));

    /* lof_record_arg provides a single function call argument to the runtime 
     * component.  It converts everything to a 64-bit integer, and indicates 
     * if the argument is a pointer or not */
    Function* record_arg = dyn_cast<Function>(
            F.getParent()->getOrInsertFunction("lof_record_arg", voidTy, voidStarTy, boolTy ));
   
    /* lof_postcall records the return value of the function call, records if the return value
     * is a pointer, and then records all changes to pointers */
    Function* postcall = dyn_cast<Function>(
            F.getParent()->getOrInsertFunction("lof_postcall", voidTy, voidStarTy, boolTy ));

    assert(precall != nullptr);
    assert(record_arg != nullptr);
    assert(postcall != nullptr);

    for(std::set<CallInst*>::iterator it = callset.begin(); it != callset.end(); ++it) {
        CallInst* ci = *it;
        IRBuilder<> IRB(ci);

        ci->dump();
        LLVM_DEBUG(dbgs() << "Found call to "
                << ci->getCalledFunction()->getName()
                << "\n");

        IRB.CreateCall(precall, { IRB.getInt8(ci->getNumArgOperands()) });

        for(Value* arg : ci->arg_operands()) {
            Value* args[2];
            args[0] = CreateBitCast(arg, IRB);
            args[1] = IRB.getInt1(arg->getType()->isPointerTy());

            IRB.CreateCall(record_arg, args);
        }
        BasicBlock::iterator bbit(ci);
        bbit++;
        IRB.SetInsertPoint(&*bbit);

        Value* args[2];
        args[0] = CreateBitCast(ci, IRB);
        args[1] = IRB.getInt1(ci->getType()->isPointerTy());
        IRB.CreateCall(postcall, args);
    }
    F.dump();

    return true;
}

char LofScribePass::ID = 0;

static RegisterPass<LofScribePass> X("lofscribe", "Leap of Faith Scribe Pass",
                             false ,
                             false );
