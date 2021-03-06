#include "clang/Basic/Centaurus.h"
#include "clang/Sema/SemaCentaurus.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Stmt.h"
#include "llvm/ADT/APSInt.h"
#include <clang/Lex/Preprocessor.h>

using namespace clang;
using namespace centaurus;

namespace {
    bool HelperMatchExpressions(Expr *E1, Expr *E2, Centaurus *ACC) {
        DirectiveInfo TmpDI(DK_TASK,E1->getLocStart());  //the directive type here is not important
        ClauseInfo TmpCI(CK_IN,&TmpDI);  //the clause type here is not important
        Arg *A1 = ACC->CreateArg(E1,&TmpCI);
        if (A1) {
            bool status = A1->Matches(E2,ACC);
            delete A1;
            return status;
        }
        return false;
    }
}

bool
Arg::Matches(Expr *E, Centaurus *ACC) {
    DirectiveInfo TmpDI(DK_TASK,E->getLocStart());  //the directive type here is not important
    ClauseInfo TmpCI(CK_IN,&TmpDI);  //the clause type here is not important
    Arg *Target = ACC->CreateArg(E,&TmpCI);
    if (!Target)
        return false;

    bool status = true;
    if (isa<RawExprArg>(this) && isa<RawExprArg>(Target)) {
        UnaryOperator *UO = dyn_cast<UnaryOperator>(getExpr()->IgnoreParenCasts());
        UnaryOperator *TargetUO = dyn_cast<UnaryOperator>(Target->getExpr()->IgnoreParenCasts());

        if (!UO && !TargetUO) {

        }
        else if (!UO || !TargetUO)
            status = false;
        else if (UO->getOpcode() != TargetUO->getOpcode())
            status = false;
        else if (UO->getOpcode() != UO_AddrOf && UO->getOpcode() != UO_Deref)
            status = false;
        else
            status = HelperMatchExpressions(UO->getSubExpr()->IgnoreParenCasts()
                                            ,TargetUO->getSubExpr()->IgnoreParenCasts()
                                            ,ACC);
    }
    else
        status = Matches(Target);

    delete Target;
    return status;
}

bool
Arg::Matches(Arg *Target) {
    //Exact Match
    //
    //check only the base variable we are refering to.
    //in the VarArg and ArrayArg cases, this is a complete check,
    //but in the ArrayElementArg and SubArrayArg this is not enough.
    //if the refered elements do not have a visible copy (different
    //array element or different subarray), this will trigger a runtime error

    //we can do more if the indices are constants but this is not required by
    //the standard to be done at compile time

    SubArrayArg *SA = 0;
    if (this->getKind() == A_SubArray && Target->getKind() == A_Array)
        SA = cast<SubArrayArg>(this);
    else if (Target->getKind() == A_SubArray && getKind() == A_Array)
        SA = cast<SubArrayArg>(Target);

    if (SA) {
        if (this->getVarDecl() != Target->getVarDecl())
            return false;

        //the start of the SubArray must be 0 and the length must be equal to the
        // size of the whole array

        Expr *Length = SA->getLength();

        ArraySubscriptExpr *ASE = cast<ArraySubscriptExpr>(SA->getExpr());
        Expr *Base = ASE->getBase()->IgnoreParenCasts();
        Expr *Start = ASE->getIdx();
        llvm::APSInt ArrayLength(64);
        if (const ConstantArrayType *ArrayTy =
            Context->getAsConstantArrayType(Base->getType())) {
            ArrayLength = ArrayTy->getSize().getZExtValue();
        }
        else {
            //dynamic array with unknown size
            return false;
        }

        llvm::APSInt StartICE(64);
        llvm::APSInt LengthICE(64);
        if (Start->isIntegerConstantExpr(StartICE,*Context) && StartICE.getZExtValue() == 0 &&
            Length->isIntegerConstantExpr(LengthICE,*Context) && LengthICE.getZExtValue() == ArrayLength.getZExtValue()) {
            return true;
        }
    }

    //early exit on different Kinds
    if (getKind() != Target->getKind())
        return false;

    if (LabelArg *LA = dyn_cast<LabelArg>(this)) {
        LabelArg *LT = cast<LabelArg>(Target);
        return LA->getLabel().compare(LT->getLabel()) == 0;
    }

    if (FunctionArg *FA = dyn_cast<FunctionArg>(this)) {
        FunctionArg *FT = cast<FunctionArg>(Target);
        return FA->getFunctionDecl() == FT->getFunctionDecl();
    }

    if (isa<RawExprArg>(this) && isICE() && Target->isICE())
        return this->getICE() == Target->getICE();

    //different VarDecls
    if (Target->getVarDecl() != this->getVarDecl())
        return false;

    if (!getVarDecl())
        return false;

    FieldNesting &AFN = this->getFieldNesting();
    FieldNesting &TFN = Target->getFieldNesting();

    //different nesting
    if (AFN.size() != TFN.size())
        return false;

    if (!TFN.empty())
        for (FieldNesting::reverse_iterator AI = AFN.rbegin(), AE = AFN.rend(),
                 TI = TFN.rbegin(), TE = TFN.rend(); AI != AE; ++AI, ++TI)
            if (*AI != *TI)
                return false;

    if (isa<ArrayElementArg>(this)) {
        //check indices
        const Expr *ABase = cast<ArraySubscriptExpr>(getExpr())->getIdx()->IgnoreParenCasts();
        const Expr *TBase = cast<ArraySubscriptExpr>(Target->getExpr())->getIdx()->IgnoreParenCasts();

        //both must be either constant or variables
        //if not constants, just return false
        llvm::APSInt AICE(64);
        llvm::APSInt TICE(64);
        return (ABase->isIntegerConstantExpr(AICE,*Context) &&
                TBase->isIntegerConstantExpr(TICE,*Context) &&
                AICE == TICE);
    }

    return true;
}

bool
Arg::Contains(Arg *Target) {
    //Data Overlap check
    //
    //'this' is "bigger" than Target

    assert(getKind() != A_RawExpr && "bad call on Arg::Contains()");
    assert(getKind() != A_Label && "bad call on Arg::Contains()");
    assert(getKind() != A_Function && "bad call on Arg::Contains()");
    assert(Target->getKind() != A_RawExpr && "bad call on Arg::Contains()");
    assert(Target->getKind() != A_Label && "bad call on Arg::Contains()");
    assert(Target->getKind() != A_Function && "bad call on Arg::Contains()");

    //common

    //different VarDecls
    if (Target->getVarDecl() != this->getVarDecl())
        return false;

    FieldNesting &AFN = this->getFieldNesting();
    FieldNesting &TFN = Target->getFieldNesting();

    if (!TFN.empty()) {
        FieldNesting::reverse_iterator AI = AFN.rbegin();
        FieldNesting::reverse_iterator TI = TFN.rbegin();
        int MinNesting = (AFN.size() < TFN.size()) ? AFN.size() : TFN.size();
        for (int i = 0; i < MinNesting; ++i, ++AI, ++TI)
            if (*AI != *TI)
                return false;
        if (AFN.size() > TFN.size())
            return false;
        else if (AFN.size() < TFN.size())
            return true;
    }

    if (isa<VarArg>(this)) {
        switch (Target->getKind()) {
        case A_RawExpr:       return false;
        case A_Var:           return true;
        case A_Array:         return false;
        case A_ArrayElement:  return false;
        case A_SubArray:      return false;
        case A_Label:
        case A_Function:
            llvm_unreachable("bad call");
        }
    }
    else if (isa<ArrayArg>(this)) {
        switch (Target->getKind()) {
        case A_RawExpr:       return false;
        case A_Var:           return false;
        case A_Array:         return true;
        case A_ArrayElement:  return true;
        case A_SubArray:      return true;
        case A_Label:
        case A_Function:
            llvm_unreachable("bad call");
        }
    }
    else if (isa<ArrayElementArg>(this)) {
        switch (Target->getKind()) {
        case A_RawExpr:       return false;
        case A_Var:           return false;
        case A_Array:         return false;
        case A_ArrayElement: {
            //check indices
            const Expr *ABase =
                cast<ArraySubscriptExpr>
                (getExpr())->getIdx()->IgnoreParenCasts();
            const Expr *TBase =
                cast<ArraySubscriptExpr>
                (Target->getExpr())->getIdx()->IgnoreParenCasts();

            //both must be either constant or variables
            //if not constants, just return false
            llvm::APSInt AICE(64);
            llvm::APSInt TICE(64);
            return (ABase->isIntegerConstantExpr(AICE,*Context) &&
                    TBase->isIntegerConstantExpr(TICE,*Context) &&
                    AICE == TICE);
        }
        case A_SubArray:      return false;
        case A_Label:
       case A_Function:
            llvm_unreachable("bad call");
        }
    }
    else if (isa<SubArrayArg>(this)) {
        switch (Target->getKind()) {
        case A_RawExpr:       return false;
        case A_Var:           return false;
        case A_Array:         return false;  //needs analysis
        case A_ArrayElement:  return false;  //needs analysis
        case A_SubArray: {
            //FIXME: check start and length
            return false;  //needs analysis
        }
        case A_Label:
        case A_Function:
            llvm_unreachable("bad call");
        }
    }
    else if (isa<LabelArg>(this) || isa<FunctionArg>(this))
        llvm_unreachable("bad call");
    else
        llvm_unreachable("unknown Arg Kind");

    llvm_unreachable("never reach here");
    return false;
}

Centaurus::Centaurus(Sema &s) : S(s), PendingDirective(0), Valid(false),
                            ICEPool(0), ValidArg(false), LengthTmp(0) {
    isValidClause[CK_LABEL] = &Centaurus::isValidClauseLabel;
    isValidClause[CK_TASKID] = &Centaurus::isValidClauseTaskid;
    isValidClause[CK_SIGNIFICANT] = &Centaurus::isValidClauseSignificant;
    isValidClause[CK_APPROXFUN] = &Centaurus::isValidClauseApproxfun;
    isValidClause[CK_EVALFUN] = &Centaurus::isValidClauseEvalfun;
    isValidClause[CK_ESTIMATION] = &Centaurus::isValidClauseEstimation;
    isValidClause[CK_BUFFER] = &Centaurus::isValidClauseBuffer;
    isValidClause[CK_LOCAL_BUFFER] = &Centaurus::isValidClauseLocal_buffer;
    isValidClause[CK_IN] = &Centaurus::isValidClauseIn;
    isValidClause[CK_OUT] = &Centaurus::isValidClauseOut;
    isValidClause[CK_INOUT] = &Centaurus::isValidClauseInout;
    isValidClause[CK_DEVICE_IN] = &Centaurus::isValidClauseIn;
    isValidClause[CK_DEVICE_OUT] = &Centaurus::isValidClauseOut;
    isValidClause[CK_DEVICE_INOUT] = &Centaurus::isValidClauseInout;
    isValidClause[CK_ON] = &Centaurus::isValidClauseOn;
    isValidClause[CK_WORKERS] = &Centaurus::isValidClauseWorkers;
    isValidClause[CK_GROUPS] = &Centaurus::isValidClauseGroups;
    isValidClause[CK_BIND] = &Centaurus::isValidClauseBind;
    isValidClause[CK_BIND_APPROXIMATE] = &Centaurus::isValidClauseBind_approximate;
    isValidClause[CK_SUGGEST] = &Centaurus::isValidClauseSuggest;
    isValidClause[CK_ENERGY_JOULE] = &Centaurus::isValidClauseEnergy_joule;
    isValidClause[CK_RATIO] = &Centaurus::isValidClauseRatio;

    isValidDirective[DK_TASK] = &Centaurus::isValidDirectiveTask;
    isValidDirective[DK_TASKGROUP] = &Centaurus::isValidDirectiveTaskgroup;
    isValidDirective[DK_TASKWAIT] = &Centaurus::isValidDirectiveTaskwait;
    isValidDirective[DK_SUBTASK] = &Centaurus::isValidDirectiveSubtask;
}

void Centaurus::SetOpenCL(bool value) { S.getPreprocessor().SetOpenCL(value); }

Arg::Arg(ArgKind K, CommonInfo *p, Expr *expr, clang::ASTContext *Context) :
    Kind(K), Parent(p), ICE(/*Bitwidth=*/64), ValidICE(false), E(expr), Context(Context) {
    assert(E && "null expression");
    assert(Parent);
    assert(Context);

    StartLoc = E->getLocStart();
    //StartLoc = E->getExprLoc();
    EndLoc = E->getLocEnd();

    //set ICE if we know the value
    if (isa<RawExprArg>(this) || isa<VarArg>(this) || isa<ArrayElementArg>(this))
        if (E->isIntegerConstantExpr(ICE, *Context))
            setValidICE(true);

    //if (isa<ArrayElementArg>(this) || isa<SubArrayArg>(this) ||
    //    isa<VarArg>(this) || isa<ArrayArg>(this)) {

    Expr *BaseExpr = E->IgnoreParenImpCasts();

    while (UnaryOperator *UO = dyn_cast<UnaryOperator>(BaseExpr))
        BaseExpr = UO->getSubExpr()->IgnoreParenImpCasts();

    if (isa<ArrayElementArg>(this) || isa<SubArrayArg>(this)) {
        ArraySubscriptExpr *ASE = cast<ArraySubscriptExpr>(BaseExpr);
        BaseExpr = ASE->getBase()->IgnoreParenImpCasts();
    }

    while (UnaryOperator *UO = dyn_cast<UnaryOperator>(BaseExpr))
        BaseExpr = UO->getSubExpr()->IgnoreParenImpCasts();

    while (MemberExpr *ME = dyn_cast<MemberExpr>(BaseExpr)) {
        FieldDecl *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
        Fields.push_back(FD);
        //RecordDecl *RD = FD->getParent();
        BaseExpr = ME->getBase()->IgnoreParenImpCasts();
        if (ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(BaseExpr))
            BaseExpr = ASE->getBase()->IgnoreParenImpCasts();
    }

    if (DeclRefExpr *DF = dyn_cast<DeclRefExpr>(BaseExpr))
        if (VarDecl *VD = dyn_cast<VarDecl>(DF->getDecl()))
            V = VD;
        //}
}

Arg::Arg(ArgKind K, CommonInfo *p, unsigned Value) :
    Kind(K), Parent(p), ICE(/*Bitwidth=*/64), ValidICE(false), E(0), V(0), Context(0)
{
    ICE = Value;
}

Arg::Arg(ArgKind K, CommonInfo *p, llvm::APSInt Value) :
    Kind(K), Parent(p), ICE(/*Bitwidth=*/64), ValidICE(false), E(0), V(0), Context(0)
{
    ICE = Value;
}

Arg::Arg(ArgKind K, CommonInfo *p, Expr *expr) :
        Kind(K), Parent(p), ICE(/*Bitwidth=*/64), ValidICE(false), E(expr), V(0), Context(0)
{
    assert((Kind == A_Label && E) || Kind == A_Function);
}

RawExprArg::RawExprArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context) :
    Arg(A_RawExpr,Parent,E,Context), ImplDefault(false) {}

RawExprArg::RawExprArg(CommonInfo *Parent, unsigned Value) :
    Arg(A_RawExpr,Parent,Value), ImplDefault(true) {}

RawExprArg::RawExprArg(CommonInfo *Parent, llvm::APSInt Value) :
    Arg(A_RawExpr,Parent,Value), ImplDefault(true) {}

VarArg::VarArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context) :
    Arg(A_Var,Parent,E,Context) {}

ArrayArg::ArrayArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context) :
    Arg(A_Array,Parent,E,Context) {}

ArrayElementArg::ArrayElementArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context) :
    Arg(A_ArrayElement,Parent,E,Context) {}

SubArrayArg::SubArrayArg(CommonInfo *Parent, Expr *firstASE, Expr *length, clang::ASTContext *Context) :
    Arg(A_SubArray,Parent,firstASE,Context), Length(length) {}

LabelArg::LabelArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context) :
    Arg(A_Label,Parent,E,Context) {}

FunctionArg::FunctionArg(CommonInfo *Parent, FunctionDecl *FD) :
        Arg(A_Function,Parent), FD(FD) {}

std::string
LabelArg::getLabel() const {
    if (StringLiteral *SL = dyn_cast<StringLiteral>(getExpr()->IgnoreParenImpCasts()))
        return SL->getString().str();
    return getPrettyArg();
}

std::string
LabelArg::getQuotedLabel() const {
    if (StringLiteral *SL = dyn_cast<StringLiteral>(getExpr()->IgnoreParenImpCasts())) {
        (void)SL;
        return "\"" + getLabel() + "\"";
    }
    return getPrettyArg();
}

static bool isStaticVarExpr(Expr *E, Centaurus *ACC) {
    E = E->IgnoreParenImpCasts();

    if (UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
        if (UO->getOpcode() == UO_AddrOf) {
            E = UO->getSubExpr()->IgnoreParenImpCasts();
            if (ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
                E = ASE->getBase()->IgnoreParenImpCasts();
                if (!ACC->isArrayExpr(E))
                    return true;
                //S.Diag(E->getLocStart(),diag::err_pragma_acc_test)
                //    << "expression is not dynamically-allocated\n";
            }
            else if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(E)) {
                if (isa<VarDecl>(DeclRef->getDecl()))
                    return true;
            }
        }
    }
    return false;
}

bool
Centaurus::isVarExpr(Expr *E) {
    E = E->IgnoreParenImpCasts();

    //ignore arrays, they must be recognized as ArrayArg only
    if (isArrayExpr(E))
        return false;

    if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(E)) {
        if (!isa<VarDecl>(DeclRef->getDecl()))
            return false;
        if (DeclRef->getType()->isPointerType())
            return true;
    }
    // CENTAURUS FIXME: add a new kind of Arg
    else if (isStaticVarExpr(E,this))
        return true;
    else if (E->getType()->isPointerType())
        return true;

    return false;
}

bool
Centaurus::isArrayExpr(Expr *E) {
    //this finds all the array variables (not elements)
    //if (const ArrayType *ATy = S.getASTContext().getAsArrayType(E->getType()))
    //S.Diag(E->getExprLoc(),diag::note_pragma_acc_test) << ATy->getTypeClassName();
    if (S.getASTContext().getAsArrayType(E->getType()) ||
        S.getASTContext().getAsConstantArrayType(E->getType()))
        return true;
    if (E->getType()->isConstantArrayType() ||
        E->getType()->isArrayType() ||
        E->getType()->isIncompleteArrayType() ||
        E->getType()->isVariableArrayType())
        return true;

    return false;
}

bool
Centaurus::isArrayElementExpr(Expr *E) {
    return dyn_cast<ArraySubscriptExpr>(E);
}

bool
Centaurus::isSubArrayExpr(Expr *E, Expr *Length) {
    return Length && dyn_cast<ArraySubscriptExpr>(E);
}

Arg*
Centaurus::CreateArg(Expr *E, CommonInfo *Common) {
    //S.Diag(E->getExprLoc(),diag::note_pragma_acc_test)
    //    << E->getType()->getTypeClassName();

    //take any existing ArrayElementArg or SubArrayArg
    Arg *A = 0;
    Expr *CurrentLength = LengthTmp;
    LengthTmp = 0;

    if (isSubArrayExpr(E,CurrentLength))
        A = new SubArrayArg(Common,E,CurrentLength,&S.getASTContext());
    else if (isArrayElementExpr(E))
        A = new ArrayElementArg(Common,E,&S.getASTContext());
    else if (isArrayExpr(E))
        A = new ArrayArg(Common,E,&S.getASTContext());
    else if (isVarExpr(E))
        A = new VarArg(Common,E,&S.getASTContext());
    else
        A = new RawExprArg(Common,E,&S.getASTContext());

    return A;
}

void
Centaurus::FindAndKeepSubArrayLength(SourceLocation ColonLoc, Expr *ase, Expr *Length) {
    assert(LengthTmp == 0);
    assert(ase);

    LengthTmp = Length;

    if (LengthTmp)
        return;

    assert(dyn_cast<ArraySubscriptExpr>(ase));
    ArraySubscriptExpr *ASE = cast<ArraySubscriptExpr>(ase);

    Expr *Base = ASE->getBase()->IgnoreParenCasts();
    Expr *Idx = ASE->getIdx();

    if (const ConstantArrayType *ArrayTy = S.getASTContext().getAsConstantArrayType(Base->getType())) {
        //If the length is missing and the array has known size,
        //the difference between the lower bound and the declared
        //size of the array is used; otherwise the length is required

        ExprResult Size = S.ActOnIntegerConstant(ColonLoc,ArrayTy->getSize().getZExtValue());
        ExprResult L = S.ActOnBinOp(S.getCurScope(),ColonLoc,tok::minus,Size.get(),Idx);
        LengthTmp = L.get();
        return;
    }

    S.Diag(ColonLoc,diag::err_pragma_acc_missing_subarray_length);
}

static void MaybeSetImplementationDefaultClauses(DirectiveInfo *DI) {
    assert(DI->getKind() == DK_TASK && "Bad Call");

    bool MissingWorkers(true);
    bool MissingGroups(true);
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseInfo *CI = *II;
        if (CI->getKind() == CK_WORKERS)
            MissingWorkers = false;
        else if (CI->getKind() == CK_GROUPS)
            MissingGroups = false;
    }

    if (MissingWorkers) {
        ClauseInfo *CI = new DefaultClauseInfo(CK_WORKERS,DI);
        Arg *A = new RawExprArg(CI,1);
        CI->setArg(A);
        CList.push_back(CI);
    }
    if (MissingGroups) {
        ClauseInfo *CI = new DefaultClauseInfo(CK_GROUPS,DI);
        Arg *A = new RawExprArg(CI,1);
        CI->setArg(A);
        CList.push_back(CI);
    }
}

bool
Centaurus::isValidDirectiveWrapper(DirectiveInfo *DI) {
//http://www.parashift.com/c++-faq-lite/pointers-to-members.html
#define ACC_CALL(method) ((*this).*(method))
#if 0
    // false positive on nested subtasks
    if (DI->getKind() == DK_SUBTASK && !S.getLangOpts().OpenCL) {
        WarnOnDirective(DI);
        return false;
    }
#endif

    if (PendingDirective)
        DiscardAndWarn();

    if (!ACC_CALL(isValidDirective[DI->getKind()])(DI))
        return false;

    //we reach here only if the Directive is valid
    PendingDirective = DI;
    return true;
}

bool
Centaurus::isValidClauseWrapper(DirectiveKind DK, ClauseInfo *CI) {
//http://www.parashift.com/c++-faq-lite/pointers-to-members.html
#define ACC_CALL(method) ((*this).*(method))

    if (!ACC_CALL(isValidClause[CI->getKind()])(DK,CI))
        return false;

    if (DK == DK_TASKWAIT)
        return true;

    bool status = true;
    if (CI->isDataClause()) {
        ArgVector &Args = CI->getArgs();
        for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
            if ((!isa<SubArrayArg>(*II) && !isa<VarArg>(*II) && !(isa<ArrayElementArg>(*II) && (*II)->getExpr()->getType()->isPointerType())) ||
                (isa<VarArg>(*II) && isStaticVarExpr((*II)->getExpr(),this))) {
                std::string _msg = "pass-by-value";
                if (isa<ArrayArg>(*II))
                    _msg = "non-dynamically allocated";
                else if (UnaryOperator *UO = dyn_cast<UnaryOperator>((*II)->getExpr()->IgnoreParenImpCasts())) {
                    if (UO->getOpcode() == UO_AddrOf)
                        _msg = "non-dynamically allocated";
                }
                std::string msg = "invalid '" + (*II)->getParent()->getAsClause()->getAsString()
                    + "' data dependency for " + _msg + " argument '"
                    + (*II)->getPrettyArg() + "' (" + (*II)->getKindAsString() + ")\n";
                S.Diag((*II)->getExpr()->getLocStart(),diag::err_pragma_acc_test) << msg;
                status = false;
                continue;
            }
        }
    }

    return status;
}

bool
Centaurus::isValidClauseLabel(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseTaskid(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseSignificant(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseApproxfun(DirectiveKind DK, ClauseInfo *CI) {
    Arg *A = CI->getArg();
    FunctionArg *FA = dyn_cast<FunctionArg>(A);
    FunctionDecl *FD = FA->getFunctionDecl();
    if (DK == DK_TASK) {
        if (!S.getASTContext().isOpenCLKernel(FD)) {
            S.Diag(CI->getLocStart(),diag::err_pragma_acc_test)
                << "expected OpenCL kernel";
            return false;
        }
    }
    else if (DK == DK_SUBTASK) {
        if (S.getASTContext().isOpenCLKernel(FD)) {
            S.Diag(CI->getLocStart(),diag::err_pragma_acc_test)
                << "expected non OpenCL kernel";
            return false;
        }
    }
    return true;
}

bool
Centaurus::isValidClauseEvalfun(DirectiveKind DK, ClauseInfo *CI) {
    if (DK == DK_TASK) {
        Arg *A = CI->getArg();
        FunctionArg *FA = dyn_cast<FunctionArg>(A);
        FunctionDecl *FD = FA->getFunctionDecl();
        if (!S.getASTContext().isOpenCLKernel(FD)) {
            S.Diag(CI->getLocStart(),diag::err_pragma_acc_test)
                << "expected OpenCL kernel";
            return false;
        }
        if (!FD->getNumParams()) {
            S.Diag(CI->getLocStart(),diag::err_pragma_acc_test)
                << "expected at least one parameter of type 'pointer to double'";
            return false;
        }
        if (ParmVarDecl *PVD = FD->getParamDecl(FD->getNumParams() - 1)) {
            QualType QTy = PVD->getOriginalType();
            if (const PointerType *PTy = dyn_cast<PointerType>(QTy)) {
                if (!PTy->getPointeeType()->isFloatingType()) {
                    S.Diag(CI->getLocStart(),diag::err_pragma_acc_test)
                        << "last parameter of evalfun expected to be of type 'pointer to double'";
                    return false;
                }
            }
            else {
                S.Diag(CI->getLocStart(),diag::err_pragma_acc_test)
                    << "last parameter of evalfun expected to be of type 'pointer to double'";
                return false;
            }
        }
       return true;
    }
    else if (DK == DK_TASKWAIT) {
        Expr *E = CI->getArg()->getExpr()->IgnoreParenImpCasts();
        FunctionDecl *FD = NULL;

        if (CallExpr *CE = dyn_cast<CallExpr>(E)) {
            FD = CE->getDirectCallee();
        }
        else if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
            Expr *RHS = BO->getRHS()->IgnoreParenCasts();
            CE = dyn_cast<CallExpr>(RHS);
            if (CE)
                FD = CE->getDirectCallee();

            if (!BO->isAssignmentOp()) {
                S.Diag(BO->getLocStart(),diag::err_pragma_acc_test)
                    << "expected assignment operator";
                return false;
            }

            Expr *LHS = BO->getLHS()->IgnoreParenCasts();
            if (!LHS->getType()->isFloatingType()) {
                S.Diag(E->getLocStart(),diag::err_pragma_acc_test)
                    << "expected assingment to variable/member of double data type";
                return false;
            }
        }

        if (!FD) {
            S.Diag(E->getLocStart(),diag::err_pragma_acc_test)
                << "expected a C function call expression or assignment statement from a C function call";
            return false;
        }
        else if (S.getASTContext().isOpenCLKernel(FD)) {
            S.Diag(E->getLocStart(),diag::err_pragma_acc_test)
                << "expected a regular C function";
            return false;
        }

        QualType ReturnType = FD->getReturnType();
        if (!FD->getNumParams() && ReturnType->isVoidType()) {
            S.Diag(CI->getLocStart(),diag::err_pragma_acc_test)
                << "expected at least one parameter of type 'pointer to double' or return value of double type";
            return false;
        }
        else if (ReturnType->isVoidType()) {
            // check for at least one pointer to double param
            bool hasDoublePtrParam = false;
            for (FunctionDecl::param_iterator
                     II = FD->param_begin(), EE = FD->param_end(); II != EE; II++) {
                ParmVarDecl *PVD = *II;
                QualType QTy = PVD->getOriginalType();
                if (const PointerType *PTy = dyn_cast<PointerType>(QTy)) {
                    if (PTy->getPointeeType()->isFloatingType()) {
                        hasDoublePtrParam = true;
                        break;
                    }
                }
            }
            if (!hasDoublePtrParam) {
                S.Diag(CI->getLocStart(),diag::err_pragma_acc_test)
                    << "expected at least one parameter of evalfun to be of type 'pointer to double'";
                return false;
            }
        }
        else if (!ReturnType->isFloatingType()) {
            S.Diag(CI->getLocStart(),diag::err_pragma_acc_test)
                << "expectet evalfun with return type of double";
            return false;
        }

        return true;
    }

    return false;
}

bool
Centaurus::isValidClauseEstimation(DirectiveKind DK, ClauseInfo *CI) {
    if (DK == DK_TASK)
        // let the clause wrapper handle this as common data clause
        return true;
    else if (DK == DK_TASKWAIT) {
        if (isa<VarArg>(CI->getArg()))
            ;  //ok
        else if (isa<RawExprArg>(CI->getArg())) {
            if (UnaryOperator *UO = dyn_cast<UnaryOperator>(CI->getArg()->getExpr())) {
                if (UO->getOpcode() != UO_AddrOf && UO->getOpcode() != UO_Deref) {
                    S.Diag(UO->getOperatorLoc(),diag::err_pragma_acc_test)
                        << "invalid unary operator, expected '*' or '&'";
                    return false;
                }
            }
        }
        else {
            std::string msg = "invalid '" + std::string(CI->getArg()->getKindAsString())
                + "' argument for estimation() clause, expected address";
            S.Diag(CI->getArg()->getExpr()->getLocStart(),diag::err_pragma_acc_test) << msg;
            return false;
        }
    }

    return true;
}

bool
Centaurus::isValidClauseBuffer(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseLocal_buffer(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseIn(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseOut(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseInout(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseDevice_in(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseDevice_out(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseDevice_inout(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseOn(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseWorkers(DirectiveKind DK, ClauseInfo *CI) {
    if (CI->getArgs().size() > 3) {
        S.Diag(CI->getLocStart(),diag::err_pragma_acc_test)
            << "support up to 3 dimensions";
        return false;
    }

    bool status = true;
    ArgVector &Pool = CI->getArgs();
    for (ArgVector::iterator II = Pool.begin(), EE = Pool.end(); II != EE; ++II) {
        Arg *A = *II;
        if (!A->getExpr()->getType()->isIntegerType()) {
            S.Diag(A->getLocStart(),diag::err_pragma_acc_test)
                << "expected expression of integer type";
            status = false;
        }
    }

    return status;
}

bool
Centaurus::isValidClauseGroups(DirectiveKind DK, ClauseInfo *CI) {
    if (CI->getArgs().size() > 3) {
        S.Diag(CI->getLocStart(),diag::err_pragma_acc_test)
            << "support up to 3 dimensions";
        return false;
    }

    bool status = true;
    ArgVector &Pool = CI->getArgs();
    for (ArgVector::iterator II = Pool.begin(), EE = Pool.end(); II != EE; ++II) {
        Arg *A = *II;
        if (!A->getExpr()->getType()->isIntegerType()) {
            S.Diag(A->getLocStart(),diag::err_pragma_acc_test)
                << "expected expression of integer type";
            status = false;
        }
    }

    return status;
}

bool
Centaurus::isValidClauseBind(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseBind_approximate(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseSuggest(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseEnergy_joule(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidClauseRatio(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
Centaurus::isValidDirectiveTask(DirectiveInfo *DI) {
    //check for missing clauses and apply implementation defaults
    MaybeSetImplementationDefaultClauses(DI);

    ClauseInfo *Workers = NULL;
    ClauseInfo *Groups = NULL;

    ClauseInfo *EvalFun = NULL;
    ClauseInfo *Estimation = NULL;

    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseInfo *CI = *II;
        if (!Workers && CI->getKind() == CK_WORKERS)
            Workers = CI;
        else if (!Groups && CI->getKind() == CK_GROUPS)
            Groups = CI;
        else if (!EvalFun && CI->getKind() == CK_EVALFUN)
            EvalFun = CI;
        else if (!Estimation && CI->getKind() == CK_ESTIMATION)
            Estimation = CI;
    }

    bool status = true;
    if (Workers->getArgs().size() != Groups->getArgs().size()) {
        S.Diag(DI->getLocStart(),diag::err_pragma_acc_test)
            << "workers and groups dimensions mismatch";
        status = false;
    }

    if (!EvalFun && !Estimation)
        return true;  //ok
#if 0
    // postpone check for CreateRgion() method, when SubStmt is known
    else if (!EvalFun) {
        S.Diag(DI->getLocStart(),diag::err_pragma_acc_test)
            << "missing evalfun() clause";
        S.Diag(Estimation->getLocStart(),diag::note_pragma_acc_test)
            << "to be combined with this clause";
        status = false;
    }
#endif
    else if (!Estimation) {
        S.Diag(DI->getLocStart(),diag::err_pragma_acc_test)
            << "missing estimation() clause";
        S.Diag(EvalFun->getLocStart(),diag::note_pragma_acc_test)
            << "to be combined with evalfun() clause";
        status = false;
    }

    return status;
}

bool
Centaurus::isValidDirectiveTaskgroup(DirectiveInfo *DI) {
    if (DI->getClauseList().empty())
        return true;

    bool hasLabelClause(false);
    bool hasRatioClause(false);
    bool hasEnergy_jouleClause(false);

    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseInfo *CI = *II;
        if (CI->getKind() == CK_RATIO)
            hasRatioClause = true;
        else if (CI->getKind() == CK_LABEL)
            hasLabelClause = true;
        else if (CI->getKind() == CK_ENERGY_JOULE)
            hasEnergy_jouleClause = true;
    }

    if (!hasLabelClause && !hasRatioClause && !hasEnergy_jouleClause) {
        S.Diag(DI->getLocStart(),diag::err_pragma_acc_test)
            << "invalid taskgroup declaration without clauses";
        return false;
    }

    // reminder: ratio() clause is unique
    if (hasRatioClause && hasEnergy_jouleClause) {
        S.Diag(DI->getLocStart(),diag::err_pragma_acc_test)
            << "invalid combination of clauses";
        return false;
    }

    return true;
}

bool
Centaurus::isValidDirectiveTaskwait(DirectiveInfo *DI) {
    assert(DI->getKind() == DK_TASKWAIT);

    if (DI->getClauseList().empty())
        return true;

    bool hasOnClause(false);
    bool hasLabelClause(false);

    ClauseInfo *EvalFun = NULL;
    ClauseInfo *Estimation = NULL;

    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseInfo *CI = *II;
        if (CI->getKind() == CK_ON)
            hasOnClause = true;
        else if (CI->getKind() == CK_LABEL)
            hasLabelClause = true;
        else if (CI->getKind() == CK_EVALFUN)
            EvalFun = CI;
        else if (CI->getKind() == CK_ESTIMATION)
            Estimation = CI;
    }

    if (hasOnClause && hasLabelClause) {
        //suggest spliting this taskwait directive into two distinct directives
        //one that has all the on() clauses, and another that has all the label()

        S.Diag(DI->getLocStart(),diag::err_pragma_acc_taskwait_clauses)
            << "invalid combination of clauses";
        return false;
    }

    ////////////////////////////////////////////////
    if (!EvalFun && !Estimation)
        return true;  //ok
    else if (!EvalFun) {
        S.Diag(DI->getLocStart(),diag::err_pragma_acc_test)
            << "missing evalfun() clause";
        //S.Diag(Estimation->getLocStart(),diag::note_pragma_acc_test)
        //    << "to be combined with estimation() clause";
        return false;
    }
    else if (!Estimation) {
        S.Diag(DI->getLocStart(),diag::err_pragma_acc_test)
            << "missing estimation() clause";
        //S.Diag(EvalFun->getLocStart(),diag::note_pragma_acc_test)
        //    << "to be combined with evalfun() clause";
        return false;
    }

    ////////////////////////////////////////////////

    //EvalFun->getArg()->getExpr()->dump();
    Expr *AssignEst = NULL;
    Expr *E = EvalFun->getArg()->getExpr()->IgnoreParenImpCasts();
    CallExpr *CE = dyn_cast<CallExpr>(E);
    if (!CE)
        if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
            if (!BO->isAssignmentOp()) {
                S.Diag(BO->getLocStart(),diag::err_pragma_acc_test)
                    << "expected assignment operator";
                return false;
            }

            Expr *LHS = BO->getLHS()->IgnoreParenCasts();
            Expr *RHS = BO->getRHS()->IgnoreParenCasts();
            CE = dyn_cast<CallExpr>(RHS);

            AssignEst = LHS;
            if (UnaryOperator *UO = dyn_cast<UnaryOperator>(AssignEst)) {
                if (UO->getOpcode() == UO_Deref) {
                    AssignEst = UO->getSubExpr()->IgnoreParenImpCasts();
                }
            }

            Expr *Est = Estimation->getArg()->getExpr()->IgnoreParenImpCasts();
            if (UnaryOperator *UO = dyn_cast<UnaryOperator>(Est)) {
                // see estimation clause semantic checks for the allowd unary operators
                Est = UO->getSubExpr()->IgnoreParenImpCasts();
            }

            if (!HelperMatchExpressions(AssignEst,Est,this)) {
                S.Diag(EvalFun->getLocStart(),diag::err_pragma_acc_test)
                    << "assignment to different variable than estimation() argument";
                // do not check for parameters
                return false;
            }
        }

    if (!CE) {
        if (isValidClauseEstimation(DI->getKind(),Estimation)) {
            std::string msg = "expected call to evalfun that sets value to '"
                + Estimation->getArg()->getPrettyArg() + "'";
            S.Diag(EvalFun->getLocStart(),diag::err_pragma_acc_test) << msg;
        }
        return false;
    }

    Expr *PassByRefEst = NULL;
    for (CallExpr::arg_iterator II = CE->arg_begin(), EE = CE->arg_end(); II != EE; ++II) {
        Expr *E = (*II)->IgnoreParenImpCasts();
        if (Estimation->getArg()->Matches(E,this)) {
            PassByRefEst = *II;
            break;
        }
    }
    if (!PassByRefEst && !AssignEst) {
        std::string msg = "evaluation function does not set estimation() argument  --  "
            + std::string(Estimation->getArg()->getKindAsString());
        S.Diag(EvalFun->getLocStart(),diag::err_pragma_acc_test) << msg;
        return false;
    }

    return true;
}

bool
Centaurus::isValidDirectiveSubtask(DirectiveInfo *DI) {
    return true;
}

void
Centaurus::WarnOnDirective(DirectiveInfo *DI) {
    if (DI)
        S.Diag(DI->getLocStart(),diag::err_pragma_acc_illegal_directive_location)
            << DI->getAsString();
}

void
Centaurus::DiscardAndWarn() {
    WarnOnDirective(PendingDirective);
    PendingDirective = 0;
}

DirectiveInfo *
Centaurus::getPendingDirectiveOrNull(enum DirectiveKind DK) {
    if (!PendingDirective)
        return 0;
    if (PendingDirective->getKind() != DK)
        return 0;

    DirectiveInfo *DI = PendingDirective;
    PendingDirective = 0;
    return DI;
}

StmtResult
Centaurus::CreateRegion(DirectiveInfo *DI, Stmt *SubStmt) {
    assert(DI && "Bad Call");
    //S.Diag(SubStmt->getLocStart(),diag::note_pragma_acc_test) << "this statement";
    //S.Diag(DI->getLocStart(),diag::note_pragma_acc_test) << "has this directive";

    if (!DI)
        return StmtEmpty();

    if (RStack.DetectInvalidConstructNesting(DI)) {
        SourceLocation Loc = DI->getLocStart();
        S.Diag(Loc,diag::err_pragma_acc_invalid_nesting);
        if (!RStack.empty()) {
            SourceLocation ParentLoc = RStack.back()->getLocStart();
            S.Diag(ParentLoc,diag::note_pragma_acc_invalid_nesting);
        }
        return StmtEmpty();
    }

    if (WarnOnInvalidLists(DI))
        return StmtEmpty();

    AclStmt *ACC = DI->getAclStmt();

    switch (DI->getKind()) {
    case DK_TASK: {
        assert(SubStmt);
        if (!SubStmt)
            return StmtEmpty();

        CallExpr *CE = dyn_cast<CallExpr>(SubStmt);
        if (!CE) {
            WarnOnDirective(DI);
            return StmtEmpty();
        }

        FunctionDecl *AccurateFun = CE->getDirectCallee();
        if (!S.getASTContext().isOpenCLKernel(AccurateFun)) {
            S.Diag(CE->getLocStart(),diag::err_pragma_acc_test) << "expected OpenCL kernel function";
            return StmtEmpty();
        }

        FunctionArg *FA = 0;
        {
            ClauseList &CList = DI->getClauseList();
            for (ClauseList::iterator
                     II = CList.begin(), EE = CList.end(); II != EE; ++II)
                if ((*II)->getKind() == CK_APPROXFUN) {
                    if ((FA = dyn_cast<FunctionArg>((*II)->getArg())))
                        break;
                }
        }
        if (FA) {
            FunctionDecl *ApproxFun = FA->getFunctionDecl();
            if (!S.getASTContext().isOpenCLKernel(ApproxFun)) {
                S.Diag(FA->getLocStart(),diag::err_pragma_acc_test) << "expected OpenCL kernel function";
                return StmtEmpty();
            }
        }
        ACC->setSubStmt(SubStmt);
        break;
    }
    case DK_SUBTASK: {
        assert(SubStmt);
        if (!SubStmt)
            return StmtEmpty();

        //allow any statement that contains a CallExpr
        if (CallExpr *CE = dyn_cast<CallExpr>(SubStmt)) {
            if (S.getASTContext().isOpenCLKernel(CE->getDirectCallee())) {
                S.Diag(SubStmt->getLocStart(),diag::err_pragma_acc_test)
                    << "nested parallelism is not supported";
                return StmtEmpty();
            }
        }
        else if (BinaryOperator *BO = dyn_cast<BinaryOperator>(SubStmt)) {
            Expr *RHS = BO->getRHS()->IgnoreParenCasts();
            if (CallExpr *CE = dyn_cast<CallExpr>(RHS))
                (void)CE;  //ok
            else {
                S.Diag(SubStmt->getLocStart(),diag::err_pragma_acc_test)
                    << "expected call expression after '=' operator";
                return StmtEmpty();
            }
        }
        else {
            S.Diag(SubStmt->getLocStart(),diag::err_pragma_acc_test)
                << "unsupported statement kind after subtask directive";
            return StmtEmpty();
        }

        ACC->setSubStmt(SubStmt);
        S.getASTContext().markAsFunctionWithSubtasks(S.getCurFunctionDecl());
        break;
    }
    case DK_TASKGROUP:
    case DK_TASKWAIT: {
        assert(!SubStmt);

        //ACC->setSubStmt(SubStmt);
        break;
    }
    }

    return ACC;
}

bool
Centaurus::FindValidICE(Arg *A) {
    if (A->isICE() && A->getICE().isNonNegative())
        return true;

    S.Diag(A->getLocStart(),diag::err_pragma_acc_expected_ice);
    return false;
}

void
Centaurus::SetDefaultICE(Arg *A, unsigned Default) {
    A->getICE() = Default;
}

static bool FindVarDecl(ArgVector &Pool, Arg *Target) {
    for (ArgVector::iterator II = Pool.begin(), EE = Pool.end(); II != EE; ++II) {
        Arg *A = *II;
        if (isa<VarArg>(A) || isa<ArrayArg>(A))
            if (A->Matches(Target))
                return true;
    }
    return false;
}

bool
Centaurus::WarnOnDuplicatesInList(ArgVector &Tmp, ArgVector &Args) {
    //return true on error

    bool status(false);
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (isa<VarArg>(A) || isa<ArrayArg>(A)) {
            if (FindVarDecl(Tmp,A)) {
                SourceLocation Loc =A->getLocStart();
                S.Diag(Loc,diag::err_pragma_acc_duplicate_vardecl);
                status = true;
                continue;
            }
            Tmp.push_back(A);
        }
    }
    return status;
}

Arg*
RegionStack::FindVisibleCopyInClause(ClauseInfo *CI, Arg *Target) {
    if (!CI->isDataClause())
        return 0;

    ArgVector &Args = CI->getArgs();
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (A->Matches(Target))
            return A;
    }
    return 0;
}

Arg*
RegionStack::FindVisibleCopyInDirective(DirectiveInfo *DI, Arg *Target) {
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II)
        if (Arg *A = FindVisibleCopyInClause(*II,Target))
            return A;
    return 0;
}

Arg*
RegionStack::FindVisibleCopyInRegionStack(Arg *Target) {
    //return the most recent Clause

    for (reverse_iterator II(rbegin()), EE(rend()); II != EE; ++II)
        if (Arg *A = FindVisibleCopyInDirective(*II,Target))
            return A;

    ArgVector &Args = Target->getVarDecl()->getCentaurusArgs();
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (A->Matches(Target))
            return A;
    }

    return 0;
}

/////////////////////////////////////////////////////////////////////////////
static
Arg*
FindDataOnDeviceFromClause(ClauseInfo *CI, Arg *Target) {
    if (!CI->isDataClause())
        return 0;

    ArgVector &Args = CI->getArgs();
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (A->Contains(Target))
            return A;
    }
    return 0;
}

static
Arg*
FindDataOnDeviceFromDirective(DirectiveInfo *DI, Arg *Target) {
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II)
        if (Arg *A = FindDataOnDeviceFromClause(*II,Target))
            return A;
    return 0;
}

Arg*
RegionStack::FindDataOnDevice(Arg *Target) {
    //return the most recent Clause

    for (reverse_iterator II(rbegin()), EE(rend()); II != EE; ++II)
        if (Arg *A = FindDataOnDeviceFromDirective(*II,Target))
            return A;

    ArgVector &Args = Target->getVarDecl()->getCentaurusArgs();
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (A->Contains(Target))
            return A;
    }

    return 0;
}
/////////////////////////////////////////////////////////////////////////////

bool
Centaurus::WarnOnVisibleCopyFromDeclareDirective(ArgVector &Args) {
    //return true on error

    bool status(false);
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (isa<VarArg>(A) || isa<ArrayArg>(A)) {
            ArgVector &Args = A->getVarDecl()->getCentaurusArgs();
            for (ArgVector::iterator RR = Args.begin(), SS = Args.end(); RR != SS; ++RR) {
                Arg *PrevArg = *RR;
                if (A->Matches(PrevArg)) {
                    S.Diag(A->getLocStart(),diag::err_pragma_acc_has_visible_copy);
                    S.Diag(PrevArg->getLocStart(),diag::note_pragma_acc_visible_copy_location);
                    status = true;
                }
            }
        }
    }
    return status;
}

bool
Centaurus::WarnOnMissingVisibleCopy(ArgVector &Args, DirectiveInfo *ExtraDI) {
    //return true on error

    bool status(false);

    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (ExtraDI &&
            RStack.FindVisibleCopyInDirective(ExtraDI,A))
            continue;  //do nothing
        else if (RStack.FindVisibleCopyInRegionStack(A))
            continue;  //do nothing

        SourceLocation Loc = A->getLocStart();
        S.Diag(Loc,diag::err_pragma_acc_missing_visible_copy);
        //do not return immediately, find all VarDecls without visible copy
        status = true;
    }
    return status;
}

bool
Centaurus::WarnOnInvalidLists(DirectiveInfo *DI) {
    //return true on error

    ArgVector ArgDataIn;
    ArgVector ArgDataOut;
    ArgVector ArgDataOn;

    bool status(false);
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator
             II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseInfo *CI = *II;
        ArgVector &Args = CI->getArgs();

        if (!CI->hasArgList())
            continue;

        if (CI->getKind() == CK_IN)
            if (WarnOnDuplicatesInList(ArgDataIn,Args))
                status = true;

        if (CI->getKind() == CK_OUT)
            if (WarnOnDuplicatesInList(ArgDataOut,Args))
                status = true;

        if (CI->getKind() == CK_ON)
            if (WarnOnDuplicatesInList(ArgDataOn,Args))
                status = true;
    }
    return status;
}

bool
Centaurus::WarnOnArgKind(ArgVector &Args, ArgKind AK) {
    //return true on error

    bool status(false);
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (A->getKind() == AK) {
            S.Diag(A->getLocStart(),diag::err_pragma_acc_illegal_arg_kind)
                << A->getKindAsString()
                << "list";
            status = true;
        }
    }
    return status;
}

bool
Centaurus::SetVisibleCopy(ArgVector &Args, ClauseInfo *CI) {
    //return true on error

    if (!CI->hasArgList())
        return false;

    bool status(false);
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        SourceLocation Loc = A->getLocStart();
        //do not set visible copy for array elements or subarrays
        if (isa<VarArg>(A) || isa<ArrayArg>(A)) {
            if (Arg *PrevArg = RStack.FindVisibleCopyInRegionStack(A)) {
                SourceLocation PrevLoc = PrevArg->getLocStart();
                S.Diag(Loc,diag::err_pragma_acc_redeclared_vardecl);
                S.Diag(PrevLoc,diag::note_pragma_acc_visible_copy_location);
                status = true;
            }
            else
                A->getVarDecl()->addCentaurusArg(A);
        }
        else {
            S.Diag(Loc,diag::err_pragma_acc_illegal_arg_kind)
                << A->getKindAsString()
                << "declare directive";
            status = true;
        }
    }
    return status;
}

bool
Centaurus::MarkArgsWithVisibleCopy(ClauseList &CList) {
    //return true on error

    bool status(false);
    for (ClauseList::iterator
             II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseInfo *CI = *II;
        if (SetVisibleCopy(CI->getArgs(),CI))
            status = true;
    }
    return status;

}

bool
RegionStack::DetectInvalidConstructNesting(DirectiveInfo *DI) {
    //no nesting is allowed
    return !empty();
}

void
RegionStack::EnterRegion(DirectiveInfo *EnterDI) {
    assert(EnterDI && "Null EnterDI");

    //ignore non region directives
    if (EnterDI->getKind() == DK_TASKWAIT) {
#if 0
        centaurus::ClauseInfo *Estimation = NULL;
        ClauseList &CList = EnterDI->getClauseList();
        for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II)
            if ((*II)->getKind() == CK_ESTIMATION) {
                Estimation = *II;
                break;
            }
        if (!Estimation)
            return;
#else
        return;
#endif
    }

    push_back(EnterDI);
}

void
RegionStack::ExitRegion(DirectiveInfo *ExpectedDI) {
    assert(ExpectedDI && "Null ExpectedDI");

    //ignore non region directives
    if (ExpectedDI->getKind() == DK_TASKWAIT) {
#if 0
        centaurus::ClauseInfo *Estimation = NULL;
        ClauseList &CList = ExpectedDI->getClauseList();
        for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II)
            if ((*II)->getKind() == CK_ESTIMATION) {
                Estimation = *II;
                break;
            }
        if (!Estimation)
            return;
#else
        return;
#endif
    }

    assert(!empty() && "Empty RegionStack");

    DirectiveInfo *DI = pop_back_val();
    (void)DI;
    assert(DI && "Null DI");
    assert((ExpectedDI == DI) && "Unbalanced Regions");
}

bool
RegionStack::InRegion(DirectiveKind DK) const {
    for (const_reverse_iterator II(rbegin()), EE(rend()); II != EE; ++II) {
        DirectiveInfo *DI = *II;
        if (DI->getKind() == DK)
            return true;
    }
    return false;
}

bool
RegionStack::InRegion(DirectiveInfo *TargetDI) const {
    for (const_reverse_iterator II(rbegin()), EE(rend()); II != EE; ++II) {
        DirectiveInfo *DI = *II;
        if (DI == TargetDI)
            return true;
    }
    return false;
}

bool
RegionStack::CurrentRegionIs(DirectiveKind DK) const {
    return (!empty() && back()->getKind() == DK);
}

Arg*
RegionStack::FindBufferObjectInClause(ClauseInfo *CI, Arg *Target) {
    ArgVector &Args = CI->getArgs();
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (A->Matches(Target))
            return A;
    }
    return 0;
}

Arg*
RegionStack::FindBufferObjectInDirective(DirectiveInfo *DI, Arg *Target) {
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II)
        if (Arg *A = FindBufferObjectInClause(*II,Target))
            return A;
    return 0;
}

Arg*
RegionStack::FindBufferObjectInRegionStack(Arg *Target) {
    //return the most recent create or present Clause

    for (reverse_iterator II(rbegin()), EE(rend()); II != EE; ++II)
        if (Arg *A = FindBufferObjectInDirective(*II,Target))
            return A;

    ArgVector &Args = Target->getVarDecl()->getCentaurusArgs();
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        assert(A->getParent()->getAsClause());
        if (A->Matches(Target))
            return A;
    }

    for (ArgVector::reverse_iterator II = InterRegionMemBuffers.rbegin(),
             EE = InterRegionMemBuffers.rend(); II != EE; ++II) {
        Arg *A = *II;
        if (A->Matches(Target))
            return A;
    }

    //if we reach here the buffer creation is implicit
    //(no 'create' Clause for the Target)

    return 0;
}
