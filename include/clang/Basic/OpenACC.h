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
class FunctionDecl;
class AccStmt;
class PrintingPolicy;
class ASTContext;

namespace openacc {

extern const std::string ExtensionName;

enum DirectiveKind {
    DK_TASK = 0,   // #pragma task ...
    DK_TASKWAIT,   // #pragma taskwait ...
    DK_TASK_COORD, // #pragma task_coord ...
    DK_SUBTASK     // #pragma subtask ...
};

const unsigned DK_START = DK_TASK;
const unsigned DK_END = DK_SUBTASK + 1;

enum ClauseKind {
    CK_LABEL = 0,
    CK_SIGNIFICANT,
    CK_APPROXFUN,
    CK_IN,
    CK_OUT,
    CK_INOUT,
    CK_ON,
    CK_WORKERS,
    CK_GROUPS,
    CK_BIND,
    CK_SUGGEST,
    CK_ENERGY_JOULE,
    CK_RATIO
};

const unsigned CK_START = CK_LABEL;
const unsigned CK_END = CK_RATIO + 1;

enum ArgKind {
    A_RawExpr,
    A_Var,
    A_Array,
    A_ArrayElement,
    A_SubArray,
    A_Label,
    A_Function
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
    Arg(ArgKind K, CommonInfo *p, Expr *expr = 0);

    ArgKind getKind() const { return Kind; }
    Expr *getExpr() const { return E; }

    template<class T>
    T *getAs() const { return cast<T>(this); }

    SourceLocation getLocStart() const { return StartLoc; }
    SourceLocation getLocEnd() const { return EndLoc; }

    CommonInfo *getParent() { return Parent; }
    FieldNesting &getFieldNesting() { return Fields; }

    VarDecl *getVarDecl() const {
        assert(V && "Arg does not have VarDecl");
        return V;
    }

    bool Matches(Arg *Target);
    bool Contains(Arg *Target);

    const char *getKindAsString() const {
        switch (Kind) {
        case A_RawExpr: return "raw expr";
        case A_Var:    return "variable";
        case A_Array:  return "array";
        case A_ArrayElement: return "array element";
        case A_SubArray:     return "subarray";
        case A_Label:  return "label";
        case A_Function:  return "function name";
        }
        llvm_unreachable("unkonwn arg type");
        return std::string().c_str();
    }

    llvm::APSInt &getICE() {
        assert(isICE() && "not constant");
        return ICE;
    }
    bool isICE() const { return ValidICE; }
    void setValidICE(bool value) { ValidICE = value; }

    std::string getPrettyArg(const PrintingPolicy &) const;
};

class RawExprArg : public Arg {
private:
    const bool ImplDefault;

public:
    RawExprArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context);
    RawExprArg(CommonInfo *Parent, unsigned Value = 1);
    RawExprArg(CommonInfo *Parent, llvm::APSInt Value);

    bool IsImplDefault() const { return ImplDefault; }

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

class LabelArg : public Arg {
public:
    LabelArg(CommonInfo *Parent, Expr *E);

    std::string getLabel() const;
    std::string getQuotedLabel() const;

    static bool classof(const Arg *A) {
        return A->getKind() == A_Label;
    }
};

class FunctionArg : public Arg {
private:
    FunctionDecl *FD;
public:
    FunctionArg(CommonInfo *Parent, FunctionDecl *FD);

    FunctionDecl *getFunctionDecl() const { return FD; }

    static bool classof(const Arg *A) {
        return A->getKind() == A_Function;
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
    Arg *getArg() const { return Args.back(); }

    template<class T>
    T *getArgAs() const { return cast<T>(getArg()); }

    const SourceLocation &getLocStart() const { return StartLoc; }
    const SourceLocation &getLocEnd() const { return EndLoc; }
    SourceRange getSourceRange() const { return SourceRange(getLocStart(),getLocEnd()); }

    void setLocEnd(SourceLocation eloc) { EndLoc = eloc; }
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

    //true if implicitly added by the implementation, (not defined by user)
    const bool ImplDefault;

    DirectiveInfo *ParentDI;

public:
    static const std::string Name[CK_END];

    ClauseKind getKind() const { return CK; }

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
        default:
            return false;
        }
    }
    bool hasNoArgs() const {
        switch (CK) {
        default:
            return false;
        }
    }
    bool hasArgList() const {
        switch (CK) {
        case CK_IN:
        case CK_OUT:
        case CK_INOUT:
        case CK_ON:
        case CK_GROUPS:
            return true;
        default:
            return false;
        }
    }
    bool isDataClause() const {
        switch (CK) {
        case CK_IN:
        case CK_OUT:
        case CK_INOUT:
        case CK_ON:
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

public:
    static const std::string Name[DK_END];

    DirectiveKind getKind() const { return DK; }
    ClauseList &getClauseList() { return CList; }
    void setAccStmt(AccStmt *Acc) { ACC = Acc; }
    AccStmt *getAccStmt() const { assert(ACC); return ACC; }

    DirectiveInfo(DirectiveKind dk, SourceLocation startloc) :
        CommonInfo(startloc,startloc), DK(dk), ACC(0) {}

    static const unsigned ValidDirective[DK_END];

    bool hasValidClause(const ClauseKind CK) const {
#define BITMASK(x) ((unsigned int)1 << x)
        return ValidDirective[DK] & BITMASK(CK);
    }

    std::string getAsString() const { return Name[DK]; }
    std::string getPrettyDirective(const PrintingPolicy &,
                                   const bool IgnoreImplDefaults = true) const;

    bool hasOptionalClauses() const {
        switch (DK) {
        case DK_TASKWAIT:
            return true;
        default:
            return false;
        }
    }
};

// Helper Functions
class RegionStack : public SmallVector<DirectiveInfo*,8> {
public:
    ArgVector InterRegionMemBuffers;

    void EnterRegion(DirectiveInfo *EnterDI);
    void ExitRegion(DirectiveInfo *ExpectedDI);

    bool InRegion(DirectiveKind DK) const;
    bool InRegion(DirectiveInfo *TargetDI) const;

    bool InAnyRegion() const { return !empty(); }
    bool CurrentRegionIs(DirectiveKind DK) const;

    bool DetectInvalidConstructNesting(DirectiveInfo *DI);

    Arg* FindVisibleCopyInClause(ClauseInfo *CI, Arg *Target);
    Arg* FindVisibleCopyInDirective(DirectiveInfo *DI, Arg *Target);
    Arg* FindVisibleCopyInRegionStack(Arg *Target);

    //a wider version of FindVisibleCopy*() functions
    Arg* FindDataOnDevice(Arg *Target);

    Arg* FindBufferObjectInClause(ClauseInfo *CI, Arg *Target);
    Arg* FindBufferObjectInDirective(DirectiveInfo *DI, Arg *Target);
    Arg* FindBufferObjectInRegionStack(Arg *Target);

};

}  // end namespace openacc
}  // end namespace clang

#endif
