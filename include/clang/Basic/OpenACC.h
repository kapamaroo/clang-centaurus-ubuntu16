#ifndef LLVM_CLANG_SEMA_SEMA_OPENACC
#define LLVM_CLANG_SEMA_SEMA_OPENACC

#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/Ownership.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"

namespace clang {
class Token;
class VarDecl;
class FieldDecl;
class AccStmt;
class PrintingPolicy;
class ASTContext;

namespace openacc {

enum DirectiveKind {
    DK_PARALLEL = 0,  // #pragma acc parallel...
    DK_PARALLEL_LOOP,  // #pragma acc parallel loop...
    DK_KERNELS,   // #pragma acc kernels...
    DK_KERNELS_LOOP,   // #pragma acc kernels loop...
    DK_DATA,      // #pragma acc data...
    DK_HOST_DATA, // #pragma acc host_data...
    DK_LOOP,      // #pragma acc loop...
    DK_CACHE,     // #pragma acc cache...
    DK_DECLARE,   // #pragma acc declare...
    DK_UPDATE,    // #pragma acc update...
    DK_WAIT       // #pragma acc wait
};

const unsigned DK_START = DK_PARALLEL;
const unsigned DK_END = DK_WAIT + 1;

enum ClauseKind {
    CK_IF = 0,
    CK_ASYNC,
    CK_NUM_GANGS,
    CK_NUM_WORKERS,
    CK_VECTOR_LENGTH,
    CK_REDUCTION,
    CK_COPY,
    CK_COPYIN,
    CK_COPYOUT,
    CK_CREATE,
    CK_PRESENT,
    CK_PCOPY,     //or present_or_copy
    CK_PCOPYIN,   //or present_or_copyin
    CK_PCOPYOUT,  //or present_or_copyout
    CK_PCREATE,   //or present_or_create
    CK_DEVICEPTR,
    CK_PRIVATE,
    CK_FIRSTPRIVATE,
    CK_USE_DEVICE,
    CK_COLLAPSE,
    CK_GANG,
    CK_WORKER,
    CK_VECTOR,
    CK_SEQ,
    CK_INDEPENDENT,
    CK_DEVICE_RESIDENT,
    CK_HOST,
    CK_DEVICE
};

const unsigned CK_START = CK_IF;
const unsigned CK_END = CK_DEVICE + 1;

enum ReductionOperator {
    ROP_PLUS,
    ROP_MULT,
    ROP_MAX,
    ROP_MIN,
    ROP_BITWISE_AND,
    ROP_BITWISE_OR,
    ROP_BITWISE_XOR,
    ROP_LOGICAL_AND,
    ROP_LOGICAL_OR
};

enum ReductionInitValue {
    RIV_PLUS = 0,
    RIV_MULT = 1,

    //FIXME we must change the max/min values according to the reduction datatype
    RIV_MAX = ~1,  //std::numeric_limits<unsigned int>::max(),
    RIV_MIN = 0, //UINT_MIN,  //std::numeric_limits<unsigned int>::min(),
    RIV_BITWISE_AND = ~0,
    RIV_BITWISE_OR = 0,
    RIV_BITWISE_XOR = 0,
    RIV_LOGICAL_AND = 1,
    RIV_LOGICAL_OR = 0
};

enum ArgKind {
    A_RawExpr,
    A_Var,
    A_Array,
    A_ArrayElement,
    A_SubArray,
    A_Const
};

class CommonInfo;
typedef SmallVector<FieldDecl*,4> FieldNesting;

class Arg {
private:
    const ArgKind Kind;
    CommonInfo *Parent;

    llvm::APSInt ICE;
    bool ValidICE;

    FieldNesting Fields;

    SourceLocation StartLoc;
    SourceLocation EndLoc;

    Expr *E;
    VarDecl *V;

    clang::ASTContext *Context;

public:
    Arg(ArgKind K, CommonInfo *p, Expr *expr, clang::ASTContext *Context);
    Arg(ArgKind K, CommonInfo *p, unsigned Value);
    Arg(ArgKind K, CommonInfo *p, llvm::APSInt Value);

    ArgKind getKind() const { return Kind; }
    Expr *getExpr() const { return E; }

    SourceLocation getStartLocation() const { return StartLoc; }
    SourceLocation getEndLocation() const { return EndLoc; }

    CommonInfo *getParent() { return Parent; }
    FieldNesting &getFieldNesting() { return Fields; }

    VarDecl *getVarDecl() const {
        assert(Kind != A_Const && "ConstArg does not have VarDecl");
        return V;
    }

    bool Matches(Arg *Target);
    bool Contains(Arg *Target);

    const char *getKindAsString() const {
        switch (Kind) {
        case A_RawExpr: return "raw expr";
        case A_Var:    return "variable";
        case A_Array:  return "array";
        case A_Const:  return "constant";
        case A_ArrayElement: return "array element";
        case A_SubArray:     return "subarray";
        }
        llvm_unreachable("unkonwn arg type");
    }

    llvm::APSInt &getICE() {
        assert(Kind != A_Array && "ArrayArg does not have ICE");
        assert(Kind != A_SubArray && "SubArrayArg does not have ICE");
        return ICE;
    }
    bool isICE() const { return ValidICE; }
    void setValidICE(bool value) { ValidICE = value; }

    std::string getPrettyArg(const PrintingPolicy &) const;
};

class RawExprArg : public Arg {
public:
    RawExprArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context);

    static bool classof(const Arg *A) {
        return A->getKind() == A_RawExpr;
    }
};

class VarArg : public Arg {
public:
    VarArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context);

    static bool classof(const Arg *A) {
        return A->getKind() == A_Var;
    }
};

class ArrayArg : public Arg {
public:
    ArrayArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context);

    static bool classof(const Arg *A) {
        return A->getKind() == A_Array;
    }
};

class ArrayElementArg : public Arg {
public:
    ArrayElementArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context);

    static bool classof(const Arg *A) {
        return A->getKind() == A_ArrayElement;
    }
};

class SubArrayArg : public Arg {
private:
    //Arg::E is of type ArraySubscriptExpr which holds the first element
    //of the subarray
    Expr *Length;

public:
    SubArrayArg(CommonInfo *Parent, Expr *firstASE, Expr *length,
                clang::ASTContext *Context);

    Expr *getLength() { return Length; }
    const Expr *getLength() const { return Length; }

    static bool classof(const Arg *A) {
        return A->getKind() == A_SubArray;
    }
};

class ConstArg : public Arg {
private:
    bool ImplDefault;

public:
    ConstArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context);
    ConstArg(CommonInfo *Parent, unsigned Value = 1);
    ConstArg(CommonInfo *Parent, llvm::APSInt Value);

    bool IsImplDefault() const { return ImplDefault; }

    static bool classof(const Arg *A) {
        return A->getKind() == A_Const;
    }
};

typedef SmallVector<Arg*, 12> ArgVector;

class ClauseInfo;
class DirectiveInfo;

class CommonInfo {
private:
    ArgVector Args;

    SourceLocation StartLoc;
    SourceLocation EndLoc;

    const bool isClauseInfo;

public:
    ArgVector &getArgs() { return Args; }
    const ArgVector &getArgs() const { return Args; }
    Arg *getArg() { return Args.back(); }
    Arg *getArg() const { return Args.back(); }

    const SourceLocation &getStartLocation() const { return StartLoc; }
    const SourceLocation &getEndLocation() const { return EndLoc; }

    void setEndLocation(SourceLocation eloc) { EndLoc = eloc; }
    void setArg(Arg *A) { Args.push_back(A); }
    bool hasArgs() const { return !Args.empty(); }

    //bool isClause() const { return isClauseInfo; }
    ClauseInfo *getAsClause();
    DirectiveInfo *getAsDirective();

protected:
    CommonInfo(SourceLocation startloc, SourceLocation endloc, bool clause = false) :
        StartLoc(startloc), EndLoc(endloc), isClauseInfo(clause) {}

    //Constructor for DefaultClauseInfo
    CommonInfo() : isClauseInfo(true) {}

};

class DirectiveInfo;

/// keep information about one clause
class ClauseInfo : public CommonInfo {
private:
    const ClauseKind CK;
    //if CK == CK_REDUCTION, consider operator, else ignore
    ReductionOperator ROP;

    //true if implicitly added by the implementation, (not defined by user)
    const bool ImplDefault;

    DirectiveInfo *ParentDI;
    static const std::string Name[CK_END];

public:
    ClauseKind getKind() const { return CK; }
    ReductionOperator getReductionOperator() const { return ROP; }
    void setReductionOperator(ReductionOperator rop) {
        assert(CK==CK_REDUCTION && "Reduction Operator set to invalid clause");
        ROP = rop;
    }
    std::string printReductionOperator() const {
        assert(CK==CK_REDUCTION && "invalid Reduction Operator");
        switch (ROP) {
        case ROP_PLUS:         return "+";
        case ROP_MULT:         return "*";
        case ROP_MAX:          return "max";
        case ROP_MIN:          return "min";
        case ROP_BITWISE_AND:  return "&";
        case ROP_BITWISE_OR:   return "|";
        case ROP_BITWISE_XOR:  return "^";
        case ROP_LOGICAL_AND:  return "&&";
        case ROP_LOGICAL_OR:   return "||";
        }
        llvm_unreachable("unknown Reduction Operator");
    }

    bool isImplDefault() const { return ImplDefault; }

    DirectiveInfo *getParentDirective() const { return ParentDI; }

    ClauseInfo(ClauseKind ck, SourceLocation startloc, DirectiveInfo *DI) :
        CommonInfo(startloc,startloc,true), CK(ck), ImplDefault(false), ParentDI(DI) {}

    ClauseInfo(ClauseKind ck, SourceLocation startloc, SourceLocation endloc, DirectiveInfo *DI) :
        CommonInfo(startloc,endloc,true), CK(ck), ImplDefault(false), ParentDI(DI) {}

    explicit ClauseInfo(ClauseKind ck, DirectiveInfo *DI) :
        CommonInfo(), CK(ck), ImplDefault(true), ParentDI(DI) {}

    bool hasOptionalArgs() const {
        switch (CK) {
        case CK_ASYNC:
        case CK_VECTOR:
        case CK_GANG:
        case CK_WORKER:
            return true;
        default:
            return false;
        }
    }
    bool hasNoArgs() const {
        switch (CK) {
        case CK_SEQ:
        case CK_INDEPENDENT:
            return true;
        default:
            return false;
        }
    }
    bool hasArgList() const {
        switch (CK) {
        case CK_REDUCTION:
        case CK_COPY:
        case CK_COPYIN:
        case CK_COPYOUT:
        case CK_CREATE:
        case CK_PRESENT:
        case CK_PCOPY:
        case CK_PCOPYIN:
        case CK_PCOPYOUT:
        case CK_PCREATE:
        case CK_DEVICEPTR:
        case CK_PRIVATE:
        case CK_FIRSTPRIVATE:
        case CK_USE_DEVICE:
        case CK_DEVICE_RESIDENT:
        case CK_HOST:
        case CK_DEVICE:
            return true;
        default:
            return false;
        }
    }
    bool isDataClause() const {
        switch (CK) {
        case CK_DEVICEPTR:
        case CK_COPY:
        case CK_COPYIN:
        case CK_COPYOUT:
        case CK_CREATE:
        case CK_PRESENT:
        case CK_PCOPY:
        case CK_PCOPYIN:
        case CK_PCOPYOUT:
        case CK_PCREATE:
            return true;
        default:
            return false;
        }
    }
    bool isCreateOrPresentClause() const {
        switch (CK) {
        case CK_CREATE:
        case CK_PRESENT:
        case CK_PCREATE:
            return true;
        default:
            return false;
        }
    }
    bool isCopyClause(bool AllowPresent = true) const {
        switch (CK) {
        case CK_COPY:
        case CK_COPYIN:
        case CK_COPYOUT:
            return true;
        case CK_PCOPY:
        case CK_PCOPYIN:
        case CK_PCOPYOUT:
            return AllowPresent;
        default:
            return false;
        }
    }
    bool isPrivateClause() const {
        switch (CK) {
        case CK_PRIVATE:
        case CK_FIRSTPRIVATE:
            return true;
        default:
            return false;
        }
    }

    std::string getAsString() const { return Name[CK]; }
    std::string getPrettyClause(const PrintingPolicy &) const;

};

class DefaultClauseInfo : public ClauseInfo {
public:
    explicit DefaultClauseInfo(ClauseKind ck,DirectiveInfo *DI) : ClauseInfo(ck,DI) {}
};

//max number of clauses is 24 (parallel loops directive)
typedef SmallVector<ClauseInfo*,24> ClauseList;

/// keep information about one directive
// reminder: one directive may have zero, one, many clauses
class DirectiveInfo : public CommonInfo {
private:
    const DirectiveKind DK;
    ClauseList CList;
    AccStmt *ACC;
    ClauseInfo *IfClause;

    static const std::string Name[DK_END];

public:
    DirectiveKind getKind() const { return DK; }
    ClauseList &getClauseList() { return CList; }
    void setAccStmt(AccStmt *Acc) { ACC = Acc; }
    AccStmt *getAccStmt() const { assert(ACC); return ACC; }

    void setIfClause(ClauseInfo *CI) { IfClause = CI; }
    ClauseInfo *getIfClause() const { return IfClause; }

    DirectiveInfo(DirectiveKind dk, SourceLocation startloc) :
        CommonInfo(startloc,startloc), DK(dk), ACC(0), IfClause(0) {}

    bool isExecutableDirective() const {
        switch (DK) {
        case DK_UPDATE:
        case DK_WAIT:
            return true;
        default:
            return false;
        }
    }
    bool isExecutableOrCacheOrDeclareDirective() const {
        switch (DK) {
        case DK_UPDATE:
        case DK_WAIT:
        case DK_CACHE:
        case DK_DECLARE:
            return true;
        default:
            return false;
        }
    }
    bool isStartOfLoopRegion() const {
        switch (DK) {
        case DK_PARALLEL_LOOP:
        case DK_KERNELS_LOOP:
        case DK_LOOP:
            return true;
        default:
            return false;
        }
    }
    bool hasOptionalClauses() const {
        //'loop' and 'kernels loop' directives have default values for
        //gang, worker and vector clauses in case they are missing
        switch (DK) {
        case DK_WAIT:
        case DK_LOOP:
        case DK_KERNELS_LOOP:
            return true;
        default:
            return false;
        }
    }
    bool isCombinedDirective() const {
        switch (DK) {
        case DK_PARALLEL_LOOP:
        case DK_KERNELS_LOOP:
            return true;
        default:
            return false;
        }
    }
    bool isComputeDirective() const {
        switch (DK) {
        case DK_PARALLEL:
        case DK_KERNELS:
            return true;
        case DK_PARALLEL_LOOP:
        case DK_KERNELS_LOOP:
            //use isCombinedDirective() instead
            return false;
        default:
            return false;
        }
    }
    bool isDataDirective() const {
        switch (DK) {
        case DK_DATA:
        case DK_HOST_DATA:
            return true;
        default:
            return false;
        }
    }

    static const unsigned ValidDirective[DK_END];

    bool hasValidClause(const ClauseKind CK) const {
#define BITMASK(x) ((unsigned int)1 << x)
        return ValidDirective[DK] & BITMASK(CK);
    }

    std::string getAsString() const { return Name[DK]; }
    std::string getPrettyDirective(const PrintingPolicy &,
                                   const bool IgnoreImplDefaults = true) const;

};

// Helper Functions
class RegionStack : public SmallVector<DirectiveInfo*,8> {
public:
    ArgVector InterRegionMemBuffers;

    void EnterRegion(DirectiveInfo *EnterDI);
    void ExitRegion(DirectiveInfo *ExpectedDI);

    bool InRegion(DirectiveKind DK) const;
    bool InRegion(DirectiveInfo *TargetDI) const;
    bool InComputeRegion() const;
    bool InLoopRegion() const;

    bool InAnyRegion() const { return !empty(); }
    bool CurrentRegionIs(DirectiveKind DK) const;

    bool DetectInvalidConstructNesting(DirectiveInfo *DI);

    Arg* FindVisibleCopyInClause(ClauseInfo *CI, Arg *Target,
                                 bool IgnoreDeviceResident = false);
    Arg* FindVisibleCopyInDirective(DirectiveInfo *DI, Arg *Target,
                                    bool IgnoreDeviceResident = false);
    Arg* FindVisibleCopyInRegionStack(Arg *Target,
                                      bool IgnoreDeviceResident = false);

    Arg* FindMatchingPrivateOrFirstprivateInClause(ClauseInfo *CI, Arg *Target);
    Arg* FindMatchingPrivateOrFirstprivateInDirective(DirectiveInfo *DI, Arg *Target);
    Arg* FindMatchingPrivateOrFirstprivateInRegionStack(Arg *Target);

    //a wider version of FindVisibleCopy*() functions
    Arg* FindDataOnDevice(Arg *Target, bool IgnoreDeviceResident = false);

    Arg* FindReductionInClause(ClauseInfo *CI, Arg *Target);
    Arg* FindReductionInDirective(DirectiveInfo *DI, Arg *Target);
    Arg* FindReductionInRegionStack(Arg *Target);

    DirectiveInfo *getTopComputeOrCombinedRegion() const;

    Arg* FindBufferObjectInClause(ClauseInfo *CI, Arg *Target);
    Arg* FindBufferObjectInDirective(DirectiveInfo *DI, Arg *Target);
    Arg* FindBufferObjectInRegionStack(Arg *Target);

};

}  // end namespace openacc
}  // end namespace clang

#endif
