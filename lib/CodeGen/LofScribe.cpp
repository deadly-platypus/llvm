//
// Created by derrick on 8/31/18.
//

#include "LofScribe.h"

char LofScribePass::ID = 0;

using namespace llvm;

LofScribePass::LofScribePass() : FunctionPass(ID) {
    initializeLofScribePassPass(*PassRegistry::getPassRegistry());
}

bool LofScribePass::runOnFunction(Function &F) {
    return false;
}

INITIALIZE_PASS_BEGIN(LofScribePass, "lofscribe", "gets function return values and input arguments", false, false);
INITIALIZE_PASS_END(LofScribePass, "lofscribe", "gets function return values and input arguments", false, false);

FunctionPass *llvm::createLofScribePass() { return new LofScribePass(); }
