#define DEBUG_TYPE "oclinfo"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/ADT/DenseMap.h"

//#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

//https://weaponshot.wordpress.com/2012/05/06/extract-all-the-metadata-nodes-in-llvm/

namespace {

struct BasicBlockStatistics {
    StringRef Name;
    unsigned int TotalInstr;
    unsigned int MemInstr;
    unsigned int ScalarInstr;
    unsigned int FloatInstr;

    BasicBlockStatistics(const BasicBlock &BB) : Name(BB.getName()),
                                                 TotalInstr(0), MemInstr(0),
                                                 ScalarInstr(0), FloatInstr(0) {}

    void print() const {
        errs() << Name << ": "
               << MemInstr << "^mem + " << ScalarInstr << "^int + "
               << FloatInstr << "^fp = " << TotalInstr << "\n";
    }

};

struct FunctionStatistics {
    SmallVector<BasicBlockStatistics,16> BBStats;
};

struct OCLModuleInfo {
    Module &M;
    LLVMContext &Context;

    DenseMap<Function *, FunctionStatistics> FunctionStats;
    DenseMap<MDNode*, unsigned> _MDMap; //Map for MDNodes.
    unsigned _MDNext;

    OCLModuleInfo(Module &M)
        : M(M), Context(M.getContext()), _MDNext(0) {}

    MDNode *appendValueToNewMD(MDNode *OldOp, Value *Val) {
        SmallVector<Value *, 1> _tmp;
        for (unsigned i = 0, e = OldOp->getNumOperands(); i!=e; ++i)
            _tmp.push_back(OldOp->getOperand(i));
        _tmp.push_back(Val);
        MDNode *NewMD = MDNode::get(Context,_tmp);
        return NewMD;
    }

    MDNode *insertValueToMDOperand(MDNode *Parent, unsigned Idx, Value *Val) {
        MDNode *Op = dyn_cast<MDNode>(Parent->getOperand(Idx));
        MDNode *NewOp = appendValueToNewMD(Op,Val);
        Parent->replaceOperandWith(Idx,NewOp);
        //errs() << "__________________________________\n";
        //NewOp->print(errs());
        return NewOp;
    }

    void update() {
        //warning: assume one spir kernel per module

        std::vector<Function *> SPIR_Kernels;
        for (Module::iterator mi = M.begin(), me = M.end(); mi != me; ++mi) {
            Function *F = &*mi;
            if (!F || !F->size())
                continue;
            if (F->getCallingConv() == CallingConv::SPIR_KERNEL)
                SPIR_Kernels.push_back(F);
        }
        assert(SPIR_Kernels.size());

        NamedMDNode *NMDclKernels = 0;
        for (Module::named_metadata_iterator MI = M.named_metadata_begin(),
                 ME = M.named_metadata_end(); MI != ME; ++MI) {
            NamedMDNode *NMD = &*MI;
            StringRef Name = NMD->getName();
            //errs() << Name <<  "\n";
            if (Name.equals("opencl.kernels")) {
                NMDclKernels = NMD;
                break;
            }
#if 0
            for (unsigned i = 0, e = NMD.getNumOperands(); i!=e; ++i)
                if(MDNode *MD = dyn_cast_or_null<MDNode>(NMD.getOperand(i)))
                    keepMetadata(MD);
#endif
        }
        assert(NMDclKernels);

        assert(NMDclKernels->getNumOperands() == SPIR_Kernels.size());
        for (unsigned i = 0, e = NMDclKernels->getNumOperands(); i != e; ++i) {
            MDNode *Op = NMDclKernels->getOperand(i);
            Op->replaceOperandWith(0,SPIR_Kernels[i]);

            Value *AddrSpace = Constant::getIntegerValue(Type::getInt32Ty(Context),APInt(32,0));
            Value *AddrSpace1 = Constant::getIntegerValue(Type::getInt32Ty(Context),APInt(32,1));
            Value *AccessQual = MDString::get(Context,"none");
            Value *ArgIntPtrType = MDString::get(Context,"int*");
            Value *ArgIntType = MDString::get(Context,"int");
            Value *TypeQual = MDString::get(Context,"");
            Value *BaseIntPtrType = MDString::get(Context,"int*");
            Value *BaseIntType = MDString::get(Context,"int");


            // add metadata for __oclprof__
            insertValueToMDOperand(Op,1,AddrSpace1);
            insertValueToMDOperand(Op,2,AccessQual);
            insertValueToMDOperand(Op,3,ArgIntPtrType);
            insertValueToMDOperand(Op,4,TypeQual);
            insertValueToMDOperand(Op,5,BaseIntPtrType);

            // add metadata for __oclprof_size__
            insertValueToMDOperand(Op,1,AddrSpace);
            insertValueToMDOperand(Op,2,AccessQual);
            insertValueToMDOperand(Op,3,ArgIntType);
            insertValueToMDOperand(Op,4,TypeQual);
            insertValueToMDOperand(Op,5,BaseIntType);
        }
    }

#if 0
    void keepMetadata(MDNode *N){
        if(!N->isFunctionLocal()){
            DenseMap<MDNode*, unsigned>::iterator I = _MDMap.find(N);
            if(I!=_MDMap.end()){
                return;
            }
            //the map also stores the number of each metadata node. It is the same order as in the dumped bc file.
            unsigned DestSlot = _MDNext++;
            _MDMap[N] = DestSlot;
            //N->print(errs());
        }

        for (unsigned i = 0, e = N->getNumOperands(); i!=e; ++i){
            if(MDNode *Op = dyn_cast_or_null<MDNode>(N->getOperand(i))){
                keepMetadata(Op);
            }
        }
    }

    void CollectMD(Module &M) {
        for (Module::named_metadata_iterator MI = M.named_metadata_begin(),
                 ME = M.named_metadata_end(); MI != ME; ++MI) {
            NamedMDNode &NMD = *MI;
            StringRef Name = NMD.getName();
            errs() << Name <<  "\n";
            if (Name.equals("opencl.kernels")) {
                MDNode *Op = NMD.getOperand(0);
                //Op->replaceOperandWith(0,);
                errs() << "__________________________________\n";
                Op->print(errs());
            }
            for (unsigned i = 0, e = NMD.getNumOperands(); i!=e; ++i){
                if(MDNode *MD = dyn_cast_or_null<MDNode>(NMD.getOperand(i))){
                    keepMetadata(MD);
                }
            }
        }
    }

    bool runOnModule(Module &M) {
        errs() << "OCLModuleInfo\n";

        //CollectMD(M);

        for (Module::iterator mi = M.begin(), me = M.end(); mi != me; ++mi) {
            Function &F = *mi;
            FunctionStatistics Fout = analyzeFunc(F);
            FunctionStats[&F] = Fout;
        }

        print();

        return false;
    }
#endif

    FunctionStatistics analyzeFunc(Function &F);
    BasicBlockStatistics analyzeBB(BasicBlock &BB);

};

//struct OclProf : public CallGraphSCCPass {
struct OclProf : public ModulePass {
    static char ID;
    //virtual bool doInitialization(CallGraph &CG);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<AliasAnalysis>();
        AU.addRequired<CallGraph>();
        //CallGraphSCCPass::getAnalysisUsage(AU);
        ModulePass::getAnalysisUsage(AU);
    }

    //virtual bool runOnSCC(CallGraphSCC &SCC);
    virtual bool runOnModule(Module &M);
    //OclProf() : CallGraphSCCPass(ID) {}
    OclProf() : ModulePass(ID) {}

    Function *addProfileCounters(AliasAnalysis &AA, CallGraph &CG, Function *F);
    CallGraphNode *backpatchWithNullPtr(AliasAnalysis &AA, CallGraph &CG, Function *F, Function *NF);
    //void replaceNullPtrWithProfileCounters(AliasAnalysis &AA, CallGraph &CG, CallGraphNode *NF_CGN);

};

struct OclProf2 : public ModulePass {
    static char ID;
    //virtual bool doInitialization(CallGraph &CG);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<AliasAnalysis>();
        AU.addRequired<CallGraph>();
        //AU.addRequired<OclProf>();
        //CallGraphSCCPass::getAnalysisUsage(AU);
        ModulePass::getAnalysisUsage(AU);
    }

    //virtual bool runOnSCC(CallGraphSCC &SCC);
    virtual bool runOnModule(Module &M);
    //OclProf2() : CallGraphSCCPass(ID) {}
    OclProf2() : ModulePass(ID) {}

    //Function *addProfileCounters(AliasAnalysis &AA, CallGraph &CG, Function *F);
    //CallGraphNode *backpatchWithNullPtr(AliasAnalysis &AA, CallGraph &CG, Function *F, Function *NF);
    void replaceNullPtrWithProfileCounters(AliasAnalysis &AA, CallGraph &CG, CallGraphNode *NF_CGN);

};

}

char OclProf::ID = 0;
char OclProf2::ID = 0;

static RegisterPass<OclProf> Y1("oclprof","Add Profile Counters to Device Code");
static RegisterPass<OclProf2> Y2("oclprof2","Add Profile Counters to Device Code");

BasicBlockStatistics OCLModuleInfo::analyzeBB(BasicBlock &BB) {
    BasicBlockStatistics out(BB);
    for (BasicBlock::iterator II = BB.begin(), IE = BB.end(); II != IE; ++II) {
        if (isa<PHINode>(*II))
            continue;
        CastInst *CI = dyn_cast<CastInst>(II);
        DataLayout TD(II->getParent()->getParent()->getParent()->getDataLayout());
        if (CI && CI->isNoopCast(TD.getIntPtrType(II->getParent()->getContext())))
            continue;
        out.TotalInstr++;
        if (II->mayReadOrWriteMemory())
            out.MemInstr++;
        else if ((CI && !CI->isIntegerCast()) ||
                 (II->getType()->isArrayTy() &&
                  II->getType()->getArrayElementType()->isFloatingPointTy()) ||
                 (II->getType()->isVectorTy() &&
                  II->getType()->getScalarType()->isFloatingPointTy())
                 || II->getType()->isFloatingPointTy())
            out.FloatInstr++;
        else
            out.ScalarInstr++;
    }
    return out;
}

FunctionStatistics OCLModuleInfo::analyzeFunc(Function &F) {
    FunctionStatistics out;

    const unsigned BlockNum = F.getBasicBlockList().size();

    // we care for Functions with 2 or more BasicBlocks
    if (BlockNum == 0)
        return out;

    errs() << "\n****    Function: " << F.getName() << "    ****\n";

    for (Function::iterator BI = F.begin(), BE = F.end(); BI != BE; ++BI) {
        BasicBlockStatistics BBout = analyzeBB(*BI);
        out.BBStats.push_back(BBout);
        BBout.print();
    }
    return out;
}

//bool OclProf::runOnSCC(CallGraphSCC &SCC) {
bool OclProf::runOnModule(Module &M) {
    // Get the alias analysis information that we need to update to reflect our
    // changes.
    AliasAnalysis &AA = getAnalysis<AliasAnalysis>();

    // Get the callgraph information that we need to update to reflect our
    // changes.
    CallGraph &CG = getAnalysis<CallGraph>();

    DenseMap<Function *, Function *> New;

    //CG.print(errs(),&CG.getModule());

    std::vector<Function *> Pool;
    for (Module::iterator mi = M.begin(), me = M.end(); mi != me; ++mi) {
        Function *F = &*mi;
        if (!F || !F->size())
            continue;
        Pool.push_back(F);
    }

    for (std::vector<Function *>::iterator I = Pool.begin(), E = Pool.end(); I != E; ++I) {
        Function *F = *I;
    // Attempt to promote arguments from all functions in this SCC.
    //for (CallGraphSCC::iterator I = SCC.begin(), E = SCC.end(); I != E; ++I) {
    //    CallGraphNode *CGN = *I;
    //    Function *F = CGN->getFunction();

        //errs() << F->getNumUses() << "\n";
        New[F] = addProfileCounters(AA,CG,F);
        //errs() << F->getNumUses() << "\n";
        //errs() << New[F]->getNumUses() << "\n";
        //CallGraphNode *NEW_CGN =
        backpatchWithNullPtr(AA,CG,F,New[F]);

        //SCC.ReplaceNode(CGN, NEW_CGN);

        //errs() << F->getNumUses() << "\n";
        //errs() << New[F]->getNumUses() << "\n";
        //errs() << "CGN->getNumReferences() = " << CGN->getNumReferences() << "\n";
        //replaceNullPtrWithProfileCounters(AA,CG,NEW_CGN);
    }

    OCLModuleInfo MDHandler(M);
    MDHandler.update();

    return true;
}

//bool OclProf2::runOnSCC(CallGraphSCC &SCC) {
bool OclProf2::runOnModule(Module &M) {
    // Get the alias analysis information that we need to update to reflect our
    // changes.
    AliasAnalysis &AA = getAnalysis<AliasAnalysis>();

    // Get the callgraph information that we need to update to reflect our
    // changes.
    CallGraph &CG = getAnalysis<CallGraph>();

    //CG.print(errs(),&CG.getModule());

    //for (CallGraphSCC::iterator I = SCC.begin(), E = SCC.end(); I != E; ++I) {
    //    CallGraphNode *CGN = *I;
    //    Function *F = CGN->getFunction();
    for (Module::iterator mi = M.begin(), me = M.end(); mi != me; ++mi) {
        Function *F = &*mi;

        if (!F || !F->size())
            continue;

#if 1
        if (F->getName().endswith(".__deprecated__")) {
            errs() << "found deprecated function\n";
            continue;
        }
#endif

        //replaceNullPtrWithProfileCounters(AA,CG,CGN);
        replaceNullPtrWithProfileCounters(AA,CG,CG[F]);
    }

    return true;
}

Function *OclProf::addProfileCounters(AliasAnalysis &AA, CallGraph &CG, Function *F) {
    // Start by computing a new prototype for the function, which is the same as
    // the old function, but has modified arguments.

    errs() << F->getName() << "  -  " << __FUNCTION__ << "\n";

    CallGraphNode *Root = CG.getExternalCallingNode();
    Root->removeAnyCallEdgeTo(CG[F]);

    FunctionType *FTy = F->getFunctionType();
    std::vector<Type*> Params;

    // Attribute - Keep track of the parameter attributes for the arguments
    // that we are *not* promoting. For the ones that we do promote, the parameter
    // attributes are lost
    SmallVector<AttributeSet, 8> AttributesVec;
    const AttributeSet &PAL = F->getAttributes();

    // Add any return attributes.
    if (PAL.hasAttributes(AttributeSet::ReturnIndex))
        AttributesVec.push_back(AttributeSet::get(F->getContext(),
                                                  PAL.getRetAttributes()));

    // First, determine the new argument list
    unsigned ArgIndex = 1;
    for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end(); I != E;
         ++I, ++ArgIndex) {
        Params.push_back(I->getType());
        // Unchanged argument
        AttributeSet attrs = PAL.getParamAttributes(ArgIndex);
        if (attrs.hasAttributes(ArgIndex)) {
            AttrBuilder B(attrs, ArgIndex);
            AttributesVec.
                push_back(AttributeSet::get(F->getContext(), Params.size(), B));
        }
    }

    {
        DataLayout TD(F->getParent()->getDataLayout());
        IntegerType *IntTy1 = TD.getIntPtrType(F->getContext(), /*AddressSpace=*/1);
        PointerType *PtrTy = Type::getIntNPtrTy(F->getContext(),
                                                IntTy1->getBitWidth(),/*AS=*/1);
        // add parameter for profiling data buffer
        Params.push_back(PtrTy);
        // add parameter for profiling data buffer size
        Params.push_back(TD.getIntPtrType(F->getContext()));
    }

    // Add any function attributes.
    if (PAL.hasAttributes(AttributeSet::FunctionIndex))
        AttributesVec.push_back(AttributeSet::get(FTy->getContext(),
                                                  PAL.getFnAttributes()));

    Type *RetTy = FTy->getReturnType();

    // Construct the new function type using the new arguments.
    FunctionType *NFTy = FunctionType::get(RetTy, Params, FTy->isVarArg());

    // Create the new function body and insert it into the module.
    Function *NF = Function::Create(NFTy, F->getLinkage(), F->getName());
    NF->copyAttributesFrom(F);

    // Recompute the parameter attributes list based on the new arguments for
    // the function.
    NF->setAttributes(AttributeSet::get(F->getContext(), AttributesVec));
    AttributesVec.clear();

    {
        Function::ArgumentListType::iterator E = NF->getArgumentList().end();
        --E; --E;
        Argument *__oclprof__ = &*E;
        Argument *__oclprof_size__ = &NF->getArgumentList().back();
        __oclprof__->setName("__oclprof__");
        __oclprof__->addAttr(AttributeSet::get(FTy->getContext(),NF->arg_size(),Attribute::NoCapture));
        __oclprof_size__->setName("__oclprof_size__");
    }

    F->getParent()->getFunctionList().insert(F, NF);
    NF->takeName(F);
    F->setName(NF->getName() + ".__deprecated__");
    //NF->getFunctionType()->print(errs()); errs() << "\n";

    // Since we have now created the new function, splice the body of the old
    // function right into the new function, leaving the old rotting hulk of the
    // function empty.
    NF->getBasicBlockList().splice(NF->begin(), F->getBasicBlockList());

    // Loop over the argument list, transferring uses of the old arguments over to
    // the new arguments, also transferring over the names as well.
    //
    for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end(),
             I2 = NF->arg_begin(); I != E; ++I) {
        // If this is an unmodified argument, move the name and users over to the
        // new version.
        I->replaceAllUsesWith(I2);
        I2->takeName(I);
        AA.replaceWithNewValue(I, I2);
        ++I2;
    }

    return NF;
}

CallGraphNode *OclProf::backpatchWithNullPtr(AliasAnalysis &AA, CallGraph &CG,
                                                            Function *F, Function *NF) {
    errs() << NF->getName() << "  -  " << __FUNCTION__ << "\n";

    SmallVector<AttributeSet, 8> AttributesVec;

    // Get a new callgraph node for NF.
    CallGraphNode *NF_CGN = CG.getOrInsertFunction(NF);

    CallGraphNode *Root = CG.getExternalCallingNode();
    Root->addCalledFunction(CallSite(),NF_CGN);

    // Loop over all of the callers of the function, transforming the call sites
    // to pass in the loaded pointers.
    //
    SmallVector<Value*, 16> Args;
    while (!F->use_empty()) {
        CallSite CS(F->use_back());
        assert(CS.getCalledFunction() == F);
        Instruction *Call = CS.getInstruction();
        const AttributeSet &CallPAL = CS.getAttributes();

        // Add any return attributes.
        if (CallPAL.hasAttributes(AttributeSet::ReturnIndex))
            AttributesVec.push_back(AttributeSet::get(F->getContext(),
                                                      CallPAL.getRetAttributes()));

        // Loop over the operands, inserting GEP and loads in the caller as
        // appropriate.
        CallSite::arg_iterator AI = CS.arg_begin();
        unsigned ArgIndex = 1;
        for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end();
             I != E; ++I, ++AI, ++ArgIndex) {
            Args.push_back(*AI);          // Unmodified argument

            if (CallPAL.hasAttributes(ArgIndex)) {
                AttrBuilder B(CallPAL, ArgIndex);
                AttributesVec.
                    push_back(AttributeSet::get(F->getContext(), Args.size(), B));
            }
        }

        {
            DataLayout TD(F->getParent()->getDataLayout());
            IntegerType *IntTy1 = TD.getIntPtrType(F->getContext(), /*AddressSpace=*/1);
            PointerType *PtrTy = Type::getIntNPtrTy(F->getContext(),
                                                    IntTy1->getBitWidth(),/*AS=*/1);
            Value *__oclprof__Arg = ConstantPointerNull::get(PtrTy);
            Value *__oclprof_size__Arg = ConstantInt::get(TD.getIntPtrType(F->getContext()),0);

            Args.push_back(__oclprof__Arg);
            Args.push_back(__oclprof_size__Arg);
        }

        // Push any varargs arguments on the list.
        for (; AI != CS.arg_end(); ++AI, ++ArgIndex) {
            Args.push_back(*AI);
            if (CallPAL.hasAttributes(ArgIndex)) {
                AttrBuilder B(CallPAL, ArgIndex);
                AttributesVec.
                    push_back(AttributeSet::get(F->getContext(), Args.size(), B));
            }
        }

        // Add any function attributes.
        if (CallPAL.hasAttributes(AttributeSet::FunctionIndex))
            AttributesVec.push_back(AttributeSet::get(Call->getContext(),
                                                      CallPAL.getFnAttributes()));

        Instruction *New;
        if (InvokeInst *II = dyn_cast<InvokeInst>(Call)) {
            New = InvokeInst::Create(NF, II->getNormalDest(), II->getUnwindDest(),
                                     Args, "", Call);
            cast<InvokeInst>(New)->setCallingConv(CS.getCallingConv());
            cast<InvokeInst>(New)->setAttributes(AttributeSet::get(II->getContext(),
                                                                   AttributesVec));
        } else {
            New = CallInst::Create(NF, Args, "", Call);
            cast<CallInst>(New)->setCallingConv(CS.getCallingConv());
            cast<CallInst>(New)->setAttributes(AttributeSet::get(New->getContext(),
                                                                 AttributesVec));
            if (cast<CallInst>(Call)->isTailCall())
                cast<CallInst>(New)->setTailCall();
        }
        Args.clear();
        AttributesVec.clear();

        // Update the alias analysis implementation to know that we are replacing
        // the old call with a new one.
        AA.replaceWithNewValue(Call, New);

        // Update the callgraph to know that the callsite has been transformed.
        CallGraphNode *CalleeNode = CG[Call->getParent()->getParent()];
        CalleeNode->replaceCallEdge(Call, New, NF_CGN);

        if (!Call->use_empty()) {
            Call->replaceAllUsesWith(New);
            New->takeName(Call);
        }

        // Finally, remove the old call from the program, reducing the use-count of
        // F.
        Call->eraseFromParent();
    }

    // Tell the alias analysis that the old function is about to disappear.
    AA.replaceWithNewValue(F, NF);

    NF_CGN->stealCalledFunctionsFrom(CG[F]);

    // Now that the old function is dead, delete it.  If there is a dangling
    // reference to the CallgraphNode, just leave the dead function around for
    // someone else to nuke.
    CallGraphNode *CGN = CG[F];
    if (CGN->getNumReferences() == 0)
        delete CG.removeFunctionFromModule(CGN);
    else
        F->setLinkage(Function::ExternalLinkage);

    return NF_CGN;
}

void OclProf2::replaceNullPtrWithProfileCounters(AliasAnalysis &AA, CallGraph &CG,
                                                 CallGraphNode *NF_CGN) {
    Function *NF = NF_CGN->getFunction();

    errs() << NF->getName() << "  -  " << __FUNCTION__ << "\n";

    SmallVector<AttributeSet, 8> AttributesVec;

    // Loop over all of the callers of the function, transforming the call sites
    // to pass in the loaded pointers.
    //
    SmallVector<Value*, 16> Args;
    for (Function::use_iterator
             UI = NF->use_begin(), UE = NF->use_end(); UI != UE; ++UI) {
        CallSite CS(*UI);
        assert(CS.getCalledFunction() == NF);
        Instruction *Call = CS.getInstruction();
        const AttributeSet &CallPAL = CS.getAttributes();

        // Add any return attributes.
        if (CallPAL.hasAttributes(AttributeSet::ReturnIndex))
            AttributesVec.push_back(AttributeSet::get(NF->getContext(),
                                                      CallPAL.getRetAttributes()));

        // Loop over the operands, inserting GEP and loads in the caller as
        // appropriate.
        CallSite::arg_iterator AI = CS.arg_begin();
        unsigned ArgIndex = 1;
        Function::arg_iterator I = NF->arg_begin(), E = NF->arg_end();
        for (; I != E; ++I, ++AI, ++ArgIndex) {
            Args.push_back(*AI);          // Unmodified argument

            if (CallPAL.hasAttributes(ArgIndex)) {
                AttrBuilder B(CallPAL, ArgIndex);
                AttributesVec.
                    push_back(AttributeSet::get(NF->getContext(), Args.size(), B));
            }
        }

        {
            Args.pop_back();  //_val()->print(errs()); errs() << "\n";
            Args.pop_back();  //_val()->print(errs()); errs() << "\n";
            //errs() << "Caller : " << CS.getCaller()->getName() << "  -  type: ";
            //CS.getCaller()->getFunctionType()->print(errs()); errs() << "\n";

            Function::ArgumentListType::iterator E = CS.getCaller()->getArgumentList().end();
            --E; --E;
            Argument *__oclprof__Arg = &*E;
            Argument *__oclprof_size__Arg = &CS.getCaller()->getArgumentList().back();
            //NewArg->print(errs()); errs() << "\n";
            Args.push_back(__oclprof__Arg);
            Args.push_back(__oclprof_size__Arg);
        }

        // Push any varargs arguments on the list.
        for (; AI != CS.arg_end(); ++AI, ++ArgIndex) {
            Args.push_back(*AI);
            if (CallPAL.hasAttributes(ArgIndex)) {
                AttrBuilder B(CallPAL, ArgIndex);
                AttributesVec.
                    push_back(AttributeSet::get(NF->getContext(), Args.size(), B));
            }
        }

        //errs() << "Args.size() = " << Args.size() << "  -  NF params = " << NF->getFunctionType()->getNumParams() << "\n";

        // Add any function attributes.
        if (CallPAL.hasAttributes(AttributeSet::FunctionIndex))
            AttributesVec.push_back(AttributeSet::get(Call->getContext(),
                                                      CallPAL.getFnAttributes()));

        Instruction *New;
        if (InvokeInst *II = dyn_cast<InvokeInst>(Call)) {
            New = InvokeInst::Create(NF, II->getNormalDest(), II->getUnwindDest(),
                                     Args, "", Call);
            cast<InvokeInst>(New)->setCallingConv(CS.getCallingConv());
            cast<InvokeInst>(New)->setAttributes(AttributeSet::get(II->getContext(),
                                                                   AttributesVec));
        } else {
            New = CallInst::Create(NF, Args, "", Call);
            cast<CallInst>(New)->setCallingConv(CS.getCallingConv());
            cast<CallInst>(New)->setAttributes(AttributeSet::get(New->getContext(),
                                                                 AttributesVec));
            if (cast<CallInst>(Call)->isTailCall())
                cast<CallInst>(New)->setTailCall();
        }
        Args.clear();
        AttributesVec.clear();

        // Update the alias analysis implementation to know that we are replacing
        // the old call with a new one.
        AA.replaceWithNewValue(Call, New);

        // Update the callgraph to know that the callsite has been transformed.
        CallGraphNode *CalleeNode = CG[Call->getParent()->getParent()];
        CalleeNode->replaceCallEdge(Call, New, NF_CGN);

        if (!Call->use_empty()) {
            Call->replaceAllUsesWith(New);
            New->takeName(Call);
        }

        // Finally, remove the old call from the program, reducing the use-count of
        // F.
        Call->eraseFromParent();
    }
}
