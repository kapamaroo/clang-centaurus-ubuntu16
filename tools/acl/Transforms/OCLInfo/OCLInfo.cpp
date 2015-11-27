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
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/Debug.h"

#include <fstream>

using namespace llvm;

//https://weaponshot.wordpress.com/2012/05/06/extract-all-the-metadata-nodes-in-llvm/

namespace {

enum StatisticsType {
    STAT_INSTR_START = 0,
    STAT_INSTR_TOTAL = 0,
    STAT_INSTR_MEM,
    STAT_INSTR_INT,
    STAT_INSTR_FLOAT,
    STAT_INSTR_END
};

struct BasicBlockStatistics {
    StringRef Name;
    unsigned int BBnum;
    unsigned int Counter[STAT_INSTR_END];

    BasicBlockStatistics(const BasicBlock &BB, unsigned int BBnum) : Name(BB.getName()), BBnum(BBnum)
    {
        for (int i=STAT_INSTR_START; i < STAT_INSTR_END; ++i )
            Counter[i] = 0;
    }

    void printStatistics() const {
        // outs() << Name << ":";
        outs() << BBnum << ":";
        for (int i=STAT_INSTR_START; i < STAT_INSTR_END; ++i )
            outs() << ":" << Counter[i];
        outs() << "\n";
    }

    void writeStatistics(std::ofstream &profile_info) {
        // profile_info << Name << ":";
        profile_info << BBnum << ":";
        for (int i=STAT_INSTR_START; i < STAT_INSTR_END; ++i )
            profile_info << ":" << Counter[i];
        profile_info << "\n";
    }

};

struct FunctionStatistics : public SmallVector<BasicBlockStatistics,16> {
    Function &F;

    FunctionStatistics(Function &F) : F(F) {}

    void printStatistics() {
        for (SmallVector<BasicBlockStatistics,16>::iterator
                 II = begin(), EE = end(); II != EE; ++II) {
            // outs() << F.getName() << "::";
            II->printStatistics();
        }
    }

    void writeStatistics(std::ofstream &profile_info) {
        for (SmallVector<BasicBlockStatistics,16>::iterator
                 II = begin(), EE = end(); II != EE; ++II) {
            // profile_info << F.getName() << "::";
            II->writeStatistics(profile_info);
        }
    }

};

struct OCLModuleInfo {
    Module &M;
    LLVMContext &Context;

    SmallVector<FunctionStatistics, 32> FunctionStats;
    DenseMap<MDNode*, unsigned> _MDMap; //Map for MDNodes.
    unsigned _MDNext;

    // enumerate all basic blocks inside the module
    static unsigned int BBCounter;

    OCLModuleInfo(Module &M)
        : M(M), Context(M.getContext()), _MDNext(0) {}

    MDNode *appendValueToNewMD(MDNode *OldOp, Metadata *Val) {
        SmallVector<Metadata *, 1> _tmp;
        for (unsigned i = 0, e = OldOp->getNumOperands(); i!=e; ++i)
            _tmp.push_back(OldOp->getOperand(i));
        _tmp.push_back(Val);
        MDNode *NewMD = MDNode::get(Context,_tmp);
        return NewMD;
    }

    MDNode *insertValueToMDOperand(MDNode *Parent, unsigned Idx, Metadata *Val) {
        MDNode *Op = dyn_cast<MDNode>(Parent->getOperand(Idx));
        MDNode *NewOp = appendValueToNewMD(Op,Val);
        Parent->replaceOperandWith(Idx,NewOp);
        //outs() << "__________________________________\n";
        //NewOp->print(outs());
        return NewOp;
    }

    void updateMetadata() {
        NamedMDNode *OpenCLKernelMetadata = M.getNamedMetadata("opencl.kernels");
        NamedMDNode *NvvmAnnotations = M.getNamedMetadata("nvvm.annotations");
        assert(OpenCLKernelMetadata && NvvmAnnotations);
        assert(OpenCLKernelMetadata->getNumOperands() == NvvmAnnotations->getNumOperands());
        if (!OpenCLKernelMetadata || !NvvmAnnotations)
            return;

        std::vector<Function *> SPIR_Kernels;

        for (Module::iterator mi = M.begin(), me = M.end(); mi != me; ++mi) {
            Function *F = &*mi;
            if (!F || !F->size())
                continue;
            if (F->getReturnType()->isVoidTy() && F->getNumUses() == 0) {
                // errs() << F->getName() << ": treat as kernel\n";
                SPIR_Kernels.push_back(F);
            }
        }

        assert(OpenCLKernelMetadata->getNumOperands() == SPIR_Kernels.size());
        if (!(OpenCLKernelMetadata->getNumOperands() == SPIR_Kernels.size())) {
            errs() << "aoua\n";
            return;
        }

        for (unsigned i = 0, e = NvvmAnnotations->getNumOperands(); i != e; ++i) {
            MDNode *Op = NvvmAnnotations->getOperand(i);
            ValueAsMetadata *ValMD = ValueAsMetadata::get(SPIR_Kernels[i]);
            if (Op->isDistinct()) {
                SmallVector<Metadata *, 3> NewMD;
                NewMD.push_back(ValMD);
                NewMD.push_back(Op->getOperand(1));
                NewMD.push_back(Op->getOperand(2));
                NvvmAnnotations->setOperand(i,MDNode::get(Context,NewMD));
            }
            else
                Op->replaceOperandWith(0,ValMD);
        }

        for (unsigned i = 0, e = OpenCLKernelMetadata->getNumOperands(); i != e; ++i) {
            MDNode *Op = OpenCLKernelMetadata->getOperand(i);
            ValueAsMetadata *ValMD = ValueAsMetadata::get(SPIR_Kernels[i]);
            Op->replaceOperandWith(0,ValMD);

            Metadata *AddrSpace =
                ValueAsMetadata::get(Constant::getIntegerValue(Type::getInt32Ty(Context),APInt(32,0)));
            Metadata *AddrSpace1 =
                ValueAsMetadata::get(Constant::getIntegerValue(Type::getInt32Ty(Context),APInt(32,1)));
            Metadata *AccessQual = MDString::get(Context,"none");
            Metadata *ArgIntPtrType = MDString::get(Context,"int*");
            Metadata *ArgIntType = MDString::get(Context,"int");
            Metadata *TypeQual = MDString::get(Context,"");
            Metadata *BaseIntPtrType = MDString::get(Context,"int*");
            Metadata *BaseIntType = MDString::get(Context,"int");


            // add metadata for __oclprof__
            insertValueToMDOperand(Op,1,AddrSpace1);
            insertValueToMDOperand(Op,2,AccessQual);
            insertValueToMDOperand(Op,3,ArgIntPtrType);
            insertValueToMDOperand(Op,4,BaseIntPtrType);
            insertValueToMDOperand(Op,5,TypeQual);

            // add metadata for __oclprof_size__
            insertValueToMDOperand(Op,1,AddrSpace);
            insertValueToMDOperand(Op,2,AccessQual);
            insertValueToMDOperand(Op,3,ArgIntType);
            insertValueToMDOperand(Op,4,BaseIntType);
            insertValueToMDOperand(Op,5,TypeQual);
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
            //N->print(outs());
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
            outs() << Name <<  "\n";
            if (Name.equals("opencl.kernels")) {
                MDNode *Op = NMD.getOperand(0);
                //Op->replaceOperandWith(0,);
                outs() << "__________________________________\n";
                Op->print(outs());
            }
            for (unsigned i = 0, e = NMD.getNumOperands(); i!=e; ++i){
                if(MDNode *MD = dyn_cast_or_null<MDNode>(NMD.getOperand(i))){
                    keepMetadata(MD);
                }
            }
        }
    }
#endif

    void printStatistics() {
        outs() << "Module '" << M.getModuleIdentifier() << "': track " << BBCounter << " BasicBlocks\n";

        // outs() << "Function::";
        outs() << "BasicBlock::TOTAL:MEM:INT:FLOAT\n";
        for (SmallVector<FunctionStatistics, 32>::iterator
                 II = FunctionStats.begin(), EE = FunctionStats.end(); II != EE; ++II)
            II->printStatistics();
    }

    void writeStatistics(std::string &ProfileInfoName) {
        std::ofstream profile_info(ProfileInfoName);
        writeStatistics(profile_info);
        profile_info.flush();
    }

    void writeStatistics(std::ofstream &profile_info) {
        profile_info << "# ";
        // profile_info << "Function::";
        profile_info << "BasicBlock::MEM:INT:FLOAT:TOTAL\n";
        for (SmallVector<FunctionStatistics, 32>::iterator
                 II = FunctionStats.begin(), EE = FunctionStats.end(); II != EE; ++II)
            II->writeStatistics(profile_info);
    }

    bool collectStatistics(Module &M) {
        //CollectMD(M);

        for (Module::iterator mi = M.begin(), me = M.end(); mi != me; ++mi) {
            Function *F = &*mi;

            if (!F || !F->size())
                continue;
#if 1
            // track OpenCL builtins
            if (F->getName().startswith("__clc_"))
                continue;
#endif
            if (F->getBasicBlockList().size() == 0)
                continue;

            FunctionStatistics Fout = analyzeFunc(*F);
            FunctionStats.push_back(Fout);
        }

        return false;
    }

    FunctionStatistics analyzeFunc(Function &F);
    BasicBlockStatistics analyzeBB(BasicBlock &BB);

};

//struct OclProf : public CallGraphSCCPass {
struct OclProf : public ModulePass {
    static char ID;
    //virtual bool doInitialization(CallGraph &CG);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<AliasAnalysis>();
        AU.addRequired<CallGraphWrapperPass>();
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
    static unsigned BBCounter;
    //virtual bool doInitialization(CallGraph &CG);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<AliasAnalysis>();
        AU.addRequired<CallGraphWrapperPass>();
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
    void addCalltoProfileBuiltins(AliasAnalysis &AA, CallGraph &CG, CallGraphNode *NF_CGN, Function *Builtin_inc);

};

}

char OclProf::ID = 0;
char OclProf2::ID = 0;
unsigned OclProf2::BBCounter = 0;
unsigned OCLModuleInfo::BBCounter = 0;

static RegisterPass<OclProf> Y1("oclprof","Add Profile Counters to Device Code");
static RegisterPass<OclProf2> Y2("oclprof2","Add Profile Counters to Device Code");

BasicBlockStatistics OCLModuleInfo::analyzeBB(BasicBlock &BB) {
    BasicBlockStatistics out(BB,BBCounter++);
    for (BasicBlock::iterator II = BB.begin(), IE = BB.end(); II != IE; ++II) {
        if (isa<PHINode>(*II))
            continue;
        CastInst *CI = dyn_cast<CastInst>(II);
        DataLayout TD(II->getParent()->getParent()->getParent()->getDataLayout());
        if (CI && CI->isNoopCast(TD.getIntPtrType(II->getParent()->getContext())))
            continue;
        out.Counter[STAT_INSTR_TOTAL]++;
        if (II->mayReadOrWriteMemory())
            out.Counter[STAT_INSTR_MEM]++;
        else if ((CI && !CI->isIntegerCast()) ||
                 (II->getType()->isArrayTy() &&
                  II->getType()->getArrayElementType()->isFloatingPointTy()) ||
                 (II->getType()->isVectorTy() &&
                  II->getType()->getScalarType()->isFloatingPointTy())
                 || II->getType()->isFloatingPointTy())
            out.Counter[STAT_INSTR_FLOAT]++;
        else
            out.Counter[STAT_INSTR_INT]++;
    }
    return out;
}

FunctionStatistics OCLModuleInfo::analyzeFunc(Function &F) {
    FunctionStatistics out(F);

    for (Function::iterator BI = F.begin(), BE = F.end(); BI != BE; ++BI) {
        BasicBlockStatistics BBout = analyzeBB(*BI);
        out.push_back(BBout);
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
    CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();

    DenseMap<Function *, Function *> New;

    //CG.print(outs(),&CG.getModule());

    std::vector<Function *> Pool;
    for (Module::iterator mi = M.begin(), me = M.end(); mi != me; ++mi) {
        Function *F = &*mi;
        if (!F || !F->size())
            continue;
        if (F->getName().startswith("__clc_"))
            continue;
        Pool.push_back(F);
    }

    for (std::vector<Function *>::iterator I = Pool.begin(), E = Pool.end(); I != E; ++I) {
        Function *F = *I;
    // Attempt to promote arguments from all functions in this SCC.
    //for (CallGraphSCC::iterator I = SCC.begin(), E = SCC.end(); I != E; ++I) {
    //    CallGraphNode *CGN = *I;
    //    Function *F = CGN->getFunction();

        //outs() << F->getNumUses() << "\n";
        New[F] = addProfileCounters(AA,CG,F);
        //outs() << F->getNumUses() << "\n";
        //outs() << New[F]->getNumUses() << "\n";
        //CallGraphNode *NEW_CGN =
        backpatchWithNullPtr(AA,CG,F,New[F]);

        //SCC.ReplaceNode(CGN, NEW_CGN);

        //outs() << F->getNumUses() << "\n";
        //outs() << New[F]->getNumUses() << "\n";
        //outs() << "CGN->getNumReferences() = " << CGN->getNumReferences() << "\n";
        //replaceNullPtrWithProfileCounters(AA,CG,NEW_CGN);
    }

#if 1
    // use it if we have OpenCL metadata that need update

    OCLModuleInfo Handler(M);
    Handler.updateMetadata();
    Handler.collectStatistics(M);
    Handler.printStatistics();
    std::string ProfileInfoName = M.getModuleIdentifier() + ".aclprofinfo";
    Handler.writeStatistics(ProfileInfoName);
#endif

    return true;
}

//bool OclProf2::runOnSCC(CallGraphSCC &SCC) {
bool OclProf2::runOnModule(Module &M) {
    // Get the alias analysis information that we need to update to reflect our
    // changes.
    AliasAnalysis &AA = getAnalysis<AliasAnalysis>();

    // Get the callgraph information that we need to update to reflect our
    // changes.
    CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();

    //CG.print(outs(),&CG.getModule());

    StringRef Name = "__clc_atomic_add_addr1";
    Function *Builtin_inc = M.getFunction(Name);
    assert(Builtin_inc);
    if (!Builtin_inc) {
        errs() << Name << " not found inside module\n";
        return false;
    }

    // reset basic block counter for each module
    BBCounter = 0;

    //for (CallGraphSCC::iterator I = SCC.begin(), E = SCC.end(); I != E; ++I) {
    //    CallGraphNode *CGN = *I;
    //    Function *F = CGN->getFunction();
    for (Module::iterator mi = M.begin(), me = M.end(); mi != me; ++mi) {
        Function *F = &*mi;

        if (!F || !F->size())
            continue;

        if (F->getName().startswith("__clc_"))
            continue;
#if 1
        if (F->getName().endswith(".__deprecated__")) {
            outs() << "found deprecated function\n";
            continue;
        }
#endif

        //replaceNullPtrWithProfileCounters(AA,CG,CGN);
        replaceNullPtrWithProfileCounters(AA,CG,CG[F]);
        addCalltoProfileBuiltins(AA,CG,CG[F],Builtin_inc);
    }

    return true;
}

Function *OclProf::addProfileCounters(AliasAnalysis &AA, CallGraph &CG, Function *F) {
    // Start by computing a new prototype for the function, which is the same as
    // the old function, but has modified arguments.

    //outs() << F->getName() << "  -  " << __FUNCTION__ << "\n";

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
    //NF->getFunctionType()->print(outs()); outs() << "\n";

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
    //outs() << NF->getName() << "  -  " << __FUNCTION__ << "\n";

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
        CallSite CS(F->user_back());
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
        CalleeNode->replaceCallEdge(CallSite(Call), CallSite(New), NF_CGN);

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

    //outs() << NF->getName() << "  -  " << __FUNCTION__ << "\n";

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
            Args.pop_back();  //_val()->print(outs()); outs() << "\n";
            Args.pop_back();  //_val()->print(outs()); outs() << "\n";
            //outs() << "Caller : " << CS.getCaller()->getName() << "  -  type: ";
            //CS.getCaller()->getFunctionType()->print(outs()); outs() << "\n";

            Function::ArgumentListType::iterator E = CS.getCaller()->getArgumentList().end();
            --E; --E;
            Argument *__oclprof__Arg = &*E;
            Argument *__oclprof_size__Arg = &CS.getCaller()->getArgumentList().back();
            //NewArg->print(outs()); outs() << "\n";
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

        //outs() << "Args.size() = " << Args.size() << "  -  NF params = " << NF->getFunctionType()->getNumParams() << "\n";

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
        CalleeNode->replaceCallEdge(CallSite(Call), CallSite(New), NF_CGN);

        if (!Call->use_empty()) {
            Call->replaceAllUsesWith(New);
            New->takeName(Call);
        }

        // Finally, remove the old call from the program, reducing the use-count of
        // F.
        Call->eraseFromParent();
    }
}

void OclProf2::addCalltoProfileBuiltins(AliasAnalysis &AA, CallGraph &CG, CallGraphNode *NF_CGN,
                                        Function *Builtin_inc) {
    Function *F = NF_CGN->getFunction();

    //outs() << F->getName() << "  -  " << __FUNCTION__ << "\n";

    for (Function::iterator BI = F->begin(), BE = F->end(); BI != BE; ++BI) {
        const unsigned BBnum = BBCounter++;

        Instruction *Term = BI->getTerminator();

        Function::ArgumentListType::iterator E = F->getArgumentList().end();
        --E; --E;
        Argument *__oclprof__Arg = &*E;
        Value *__BBnum__ = ConstantInt::get(Type::getInt32Ty(F->getContext()),BBnum);
        Value *__ProfPos__ = GetElementPtrInst::CreateInBounds(__oclprof__Arg,__BBnum__,"acl.prof.blockptr",Term);
        Value *__One__ = ConstantInt::get(Type::getInt32Ty(F->getContext()),1);

        SmallVector<Value*, 2> Args;

        Args.push_back(__ProfPos__);
        Args.push_back(__One__);

        CallInst *New = CallInst::Create(Builtin_inc, Args, "", Term);
        New->setCallingConv(Builtin_inc->getCallingConv());
        New->setDoesNotThrow();
        New->setTailCall();

    }
}
