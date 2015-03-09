#ifndef LLVM_CLANG_SEMA_SEMAOPENACC
#define LLVM_CLANG_SEMA_SEMAOPENACC

#include "clang/Basic/OpenACC.h"

namespace clang {
class Sema;
class ForStmt;
namespace openacc {

typedef SmallVector<unsigned,64> UVec;

/// keep state of pragma acc parsing
class OpenACC {
private:
    Sema &S;

    //apply to all Directives

    RegionStack RStack;
    DirectiveInfo *PendingDirective;

    //iternal flag for processing the last parsed directive.
    //if this stays true after all the checks, the new Directive
    //gets inserted into the PendingDirective, else it is ignored
    bool Valid;

    //Unique Number generator
    unsigned ICEPool;
    UVec UsedICE;
    ArgVector BackPatch;

    bool ValidArg;
    Expr *LengthTmp;

public:
    typedef bool (isValidClauseFn) (DirectiveKind DK, ClauseInfo *CI);
    //pointer to member function
    typedef bool (OpenACC::*isValidClauseFnPtr) (DirectiveKind DK, ClauseInfo *CI);

    typedef bool (isValidDirectiveFn) (DirectiveInfo *DI);
    //pointer to member function
    typedef bool (OpenACC::*isValidDirectiveFnPtr) (DirectiveInfo *DI);

    //array of clause handlers
    isValidClauseFnPtr isValidClause[CK_END];
    isValidDirectiveFnPtr isValidDirective[DK_END];

    //wrapper
    isValidClauseFn isValidClauseWrapper;

    isValidClauseFn isValidClauseLabel;
    isValidClauseFn isValidClauseSignificant;
    isValidClauseFn isValidClauseApproxfun;
    isValidClauseFn isValidClauseIn;
    isValidClauseFn isValidClauseOut;
    isValidClauseFn isValidClauseInout;
    isValidClauseFn isValidClauseOn;
    isValidClauseFn isValidClauseWorkers;
    isValidClauseFn isValidClauseGroups;
    isValidClauseFn isValidClauseBind;
    isValidClauseFn isValidClauseSuggest;
    isValidClauseFn isValidClauseEnergy_joule;
    isValidClauseFn isValidClauseRatio;

    //wrapper
    isValidDirectiveFn isValidDirectiveWrapper;

    isValidDirectiveFn isValidDirectiveTask;
    isValidDirectiveFn isValidDirectiveTaskwait;
    isValidDirectiveFn isValidDirectiveTask_coord;
    isValidDirectiveFn isValidDirectiveSubtask;

    DirectiveInfo *getPendingDirectiveOrNull(enum DirectiveKind DK);
    StmtResult CreateRegion(DirectiveInfo *DI, Stmt *SubStmt = 0);

    void DiscardAndWarn();

    ///////////////////////////////////////////////

    OpenACC(Sema &s);

    void SetOpenCL(bool value);

    RegionStack &getRegionStack() { return RStack; }

    bool isInvalid() const { return !Valid; }
    void AssumeValid() { Valid = true; }
    void Invalidate() { Valid = false; }

    bool SubArraysAreAllowed() const { return ValidArg; }
    void AllowSubArrays() { ValidArg = true; }
    void ProhibitSubArrays() { ValidArg = false; }

    Arg *CreateArg(Expr *E, CommonInfo *Common);
    void FindAndKeepSubArrayLength(SourceLocation ColonLoc, Expr *ase, Expr *Length);

    void WarnOnDirective(DirectiveInfo *DI);  //deprecated

    bool FindValidICE(Arg *A);
    void SetDefaultICE(Arg *A, unsigned Default = 1);

    unsigned getUniqueICE() const { return ICEPool; }
    unsigned generateUniqueICE() { return ICEPool++; }
    void rememberICE(unsigned val) { UsedICE.push_back(val); }
    void needBackPatch(Arg *A) { BackPatch.push_back(A); }

    bool WarnOnInvalidLists(DirectiveInfo *DI);
    bool WarnOnDuplicatesInList(ArgVector &Tmp, ArgVector &Args);
    bool WarnOnVisibleCopyFromDeclareDirective(ArgVector &Args);
    bool WarnOnMissingVisibleCopy(ArgVector &Args, DirectiveInfo *ExtraDI = 0);
    bool WarnOnInvalidReductionList(ArgVector &Args);

    bool WarnOnArgKind(ArgVector &Args, ArgKind AK);
    bool WarnOnVarArg(ArgVector &Args) { return WarnOnArgKind(Args,A_Var); }
    bool WarnOnArrayArg(ArgVector &Args) { return WarnOnArgKind(Args,A_Array); }
    bool WarnOnArrayElementArg(ArgVector &Args) { return WarnOnArgKind(Args,A_ArrayElement); }
    bool WarnOnSubArrayArg(ArgVector &Args) { return WarnOnArgKind(Args,A_SubArray); }

    bool SetVisibleCopy(ArgVector &Args, ClauseInfo *CI);
    bool MarkArgsWithVisibleCopy(ClauseList &CList);

    bool DetectInvalidConstructNesting(DirectiveInfo *DI);

    //bool FindVisibleCopyInClause(ClauseInfo *CI, Arg *Target, SourceLocation &PrevLoc);
    //bool FindVisibleCopyInDirective(DirectiveInfo *DI, Arg *Target, SourceLocation &PrevLoc);
    //bool FindVisibleCopyInRegionStack(Arg *Target, SourceLocation &PrevLoc);

    bool isVarExpr(Expr *E);
    bool isArrayExpr(Expr *E);
    bool isArrayElementExpr(Expr *E);
    bool isSubArrayExpr(Expr *E);

};

}  // end namespace openacc
}  // end namespace clang

#endif
