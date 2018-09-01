//
// Created by derrick on 8/31/18.
//

#ifndef FOSBIN_FLOP_LOFSCRIBE_H
#define FOSBIN_FLOP_LOFSCRIBE_H

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/CodeGen/Passes.h"

class LofScribePass : public llvm::FunctionPass {
public:
    static char ID;
    LofScribePass();

    bool runOnFunction(llvm::Function &F) override;
};


#endif //FOSBIN_FLOP_LOFSCRIBE_H
