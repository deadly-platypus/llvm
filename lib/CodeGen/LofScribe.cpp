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

using namespace llvm;

#define DEBUG_TYPE "lofscribe"

LofScribePass::LofScribePass() : FunctionPass(ID) {
    initializeLofScribePassPass(*PassRegistry::getPassRegistry());
}

bool LofScribePass::runOnFunction(Function &F) {
    LLVM_DEBUG(dbgs() << "Leap of Faith Scribe starting...\n");
    std::set<CallInst*> callset;
    
    for(inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        if(CallInst* ci = dyn_cast<CallInst>(&*I)) {
            callset.insert(ci);
        }
    }

    LLVM_DEBUG(dbgs() << "Found "
            << callset.size()
            << " call instructions for function "
            << F.getName()
            << "\n");

    Function* precall = dyn_cast<Function>(
            F.getParent()->getOrInsertFunction("lof_precall", Type::getVoidTy(F.getContext())));
    Function* record_arg = dyn_cast<Function>(
            F.getParent()->getOrInsertFunction("lof_record_arg", Type::getVoidTy(F.getContext())));
    Function* postcall = dyn_cast<Function>(
            F.getParent()->getOrInsertFunction("lof_postcall", Type::getVoidTy(F.getContext())));

    assert(precall != nullptr);
    assert(record_arg != nullptr);
    assert(postcall != nullptr);

    for(std::set<CallInst*>::iterator it = callset.begin(); it != callset.end(); ++it) {
        CallInst* ci = *it;
        IRBuilder<> IRB(ci);

        LLVM_DEBUG(dbgs() << "Found call to "
                << ci->getCalledFunction()->getName()
                << "\n");

        IRB.CreateCall(precall, { IRB.getInt8(ci->getNumArgOperands()) });

        for(Value* arg : ci->arg_operands()) {
            Value* args[2];
            args[0] = arg;
            args[1] = IRB.getInt1(arg->getType()->isPointerTy());

            IRB.CreateCall(precall, args);
        }

        IRB.CreateCall(postcall, { ci });
    }

    return true;
}

char LofScribePass::ID = 0;

INITIALIZE_PASS_BEGIN(LofScribePass, "lofscribe", "gets function return values and input arguments", false, false);
INITIALIZE_PASS_END(LofScribePass, "lofscribe", "gets function return values and input arguments", false, false);

FunctionPass *llvm::createLofScribePass() { return new LofScribePass(); }
