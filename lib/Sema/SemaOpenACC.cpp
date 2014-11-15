#include "clang/Basic/OpenACC.h"
#include "clang/Sema/SemaOpenACC.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Stmt.h"
#include "llvm/ADT/APSInt.h"

using namespace clang;
using namespace openacc;

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

    assert(getKind() != A_RawExpr && "bad call on Arg::Matches()");

    if (isa<ConstArg>(this))
        return this->getICE() == Target->getICE();

    //different VarDecls
    if (Target->getVarDecl() != this->getVarDecl())
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
    assert(getKind() != A_Const && "bad call on Arg::Contains()");
    assert(Target->getKind() != A_RawExpr && "bad call on Arg::Contains()");
    assert(Target->getKind() != A_Const && "bad call on Arg::Contains()");

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
        case A_Const:         return false;
        }
    }
    else if (isa<ArrayArg>(this)) {
        switch (Target->getKind()) {
        case A_RawExpr:       return false;
        case A_Var:           return false;
        case A_Array:         return true;
        case A_ArrayElement:  return true;
        case A_SubArray:      return true;
        case A_Const:         return false;
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
        case A_Const:         return false;
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
        case A_Const:         return false;
        }
    }
    else
        llvm_unreachable("unknown Arg Kind");

    llvm_unreachable("never reach here");
    return false;
}

OpenACC::OpenACC(Sema &s) : S(s), PendingDirective(0), Valid(false),
                            ICEPool(0), ValidArg(false), LengthTmp(0) {
    isValidClause[CK_IF] =            &OpenACC::isValidClauseIf;
    isValidClause[CK_ASYNC] =         &OpenACC::isValidClauseAsync;
    isValidClause[CK_NUM_GANGS] =     &OpenACC::isValidClauseNum_gangs;
    isValidClause[CK_NUM_WORKERS] =   &OpenACC::isValidClauseNum_workers;
    isValidClause[CK_VECTOR_LENGTH] = &OpenACC::isValidClauseVector_length;
    isValidClause[CK_REDUCTION] =     &OpenACC::isValidClauseReduction;
    isValidClause[CK_COPY] =          &OpenACC::isValidClauseCopy;
    isValidClause[CK_COPYIN] =        &OpenACC::isValidClauseCopyin;
    isValidClause[CK_COPYOUT] =       &OpenACC::isValidClauseCopyout;
    isValidClause[CK_CREATE] =        &OpenACC::isValidClauseCreate;
    isValidClause[CK_PRESENT] =       &OpenACC::isValidClausePresent;
    isValidClause[CK_PCOPY] =         &OpenACC::isValidClausePcopy;
    isValidClause[CK_PCOPYIN] =       &OpenACC::isValidClausePcopyin;
    isValidClause[CK_PCOPYOUT] =      &OpenACC::isValidClausePcopyout;
    isValidClause[CK_PCREATE] =       &OpenACC::isValidClausePcreate;
    isValidClause[CK_DEVICEPTR] =     &OpenACC::isValidClauseDeviceptr;
    isValidClause[CK_PRIVATE] =       &OpenACC::isValidClausePrivate;
    isValidClause[CK_FIRSTPRIVATE] =  &OpenACC::isValidClauseFirstprivate;
    isValidClause[CK_USE_DEVICE] =    &OpenACC::isValidClauseUse_device;
    isValidClause[CK_COLLAPSE] =      &OpenACC::isValidClauseCollapse;
    isValidClause[CK_GANG] =          &OpenACC::isValidClauseGang;
    isValidClause[CK_WORKER] =        &OpenACC::isValidClauseWorker;
    isValidClause[CK_VECTOR] =        &OpenACC::isValidClauseVector;
    isValidClause[CK_SEQ] =           &OpenACC::isValidClauseSeq;
    isValidClause[CK_INDEPENDENT] =   &OpenACC::isValidClauseIndependent;
    isValidClause[CK_DEVICE_RESIDENT] = &OpenACC::isValidClauseDevice_resident;
    isValidClause[CK_HOST] =          &OpenACC::isValidClauseHost;
    isValidClause[CK_DEVICE] =        &OpenACC::isValidClauseDevice;

    isValidDirective[DK_PARALLEL] =      &OpenACC::isValidDirectiveParallel;
    isValidDirective[DK_PARALLEL_LOOP] = &OpenACC::isValidDirectiveParallelLoop;
    isValidDirective[DK_KERNELS] =       &OpenACC::isValidDirectiveKernels;
    isValidDirective[DK_KERNELS_LOOP] =  &OpenACC::isValidDirectiveKernelsLoop;
    isValidDirective[DK_DATA] =          &OpenACC::isValidDirectiveData;
    isValidDirective[DK_HOST_DATA] =     &OpenACC::isValidDirectiveHostData;
    isValidDirective[DK_LOOP] =          &OpenACC::isValidDirectiveLoop;
    isValidDirective[DK_CACHE] =         &OpenACC::isValidDirectiveCache;
    isValidDirective[DK_DECLARE] =       &OpenACC::isValidDirectiveDeclare;
    isValidDirective[DK_UPDATE] =        &OpenACC::isValidDirectiveUpdate;
    isValidDirective[DK_WAIT] =          &OpenACC::isValidDirectiveWait;
}

Arg::Arg(ArgKind K, CommonInfo *p, Expr *expr, clang::ASTContext *Context) :
    Kind(K), Parent(p), ICE(/*Bitwidth=*/64), ValidICE(false), E(expr), Context(Context) {
    assert(E && "null expression");
    assert(Context);
    //assert(Parent && "null Parent");

    StartLoc = E->getLocStart();
    //StartLoc = E->getExprLoc();
    EndLoc = E->getLocEnd();

    //set ICE if we know the value
    if (isa<VarArg>(this) || isa<ArrayElementArg>(this) ||
        isa<ConstArg>(this) || isa<RawExprArg>(this))
        if (E->isIntegerConstantExpr(getICE(), *Context))
            setValidICE(true);

    Expr *BaseExpr = E;

    switch (K) {
    case A_RawExpr:
        assert(Parent);
        //assert(Parent->getAsClause());
        //assert(Parent->getAsClause()->getKind() == CK_IF);
        break;
    case A_ArrayElement:
    case A_SubArray:
        if (ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(BaseExpr))
            BaseExpr = ASE->getBase()->IgnoreParenCasts();
        else
            llvm_unreachable("Bad Constructor");
        //fall through
    case A_Var:
    case A_Array:
        {
            while (MemberExpr *ME = dyn_cast<MemberExpr>(BaseExpr->IgnoreParenImpCasts())) {
                if (FieldDecl *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
                    Fields.push_back(FD);
                    //RecordDecl *RD = FD->getParent();
                    BaseExpr = ME->getBase()->IgnoreParenImpCasts();
                    if (ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(BaseExpr))
                        BaseExpr = ASE->getBase()->IgnoreParenCasts();
                    continue;
                }
                llvm_unreachable("Unsupported C++ feature");
            }

            //if (BaseExpr->getType()->isStructureType())
            //    llvm_unreachable("isStructureType()");

            if (DeclRefExpr *DF = dyn_cast<DeclRefExpr>(BaseExpr)) {
                if (VarDecl *VD = dyn_cast<VarDecl>(DF->getDecl())) {
                    V = VD;
                    break;
                }
                llvm_unreachable("we have a DeclRef");
            }

            llvm_unreachable("Bad VarDecl");
        }
    case A_Const:
        assert(isICE() && "Bad ConstArg, not ICE");
        break;
    default:
        llvm_unreachable("Bad Arg Kind");
    }
}

Arg::Arg(ArgKind K, CommonInfo *p, unsigned Value) :
    Kind(K), Parent(p), ICE(/*Bitwidth=*/64), E(0), Context(0)
{
    ICE = Value;
}

Arg::Arg(ArgKind K, CommonInfo *p, llvm::APSInt Value) :
    Kind(K), Parent(p), ICE(/*Bitwidth=*/64), E(0), Context(0)
{
    ICE = Value;
}

RawExprArg::RawExprArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context) :
    Arg(A_RawExpr,Parent,E,Context) {}

VarArg::VarArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context) :
    Arg(A_Var,Parent,E,Context) {}

ArrayArg::ArrayArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context) :
    Arg(A_Array,Parent,E,Context) {}

ArrayElementArg::ArrayElementArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context) :
    Arg(A_ArrayElement,Parent,E,Context) {}

SubArrayArg::SubArrayArg(CommonInfo *Parent, Expr *firstASE, Expr *length, clang::ASTContext *Context) :
    Arg(A_SubArray,Parent,firstASE,Context), Length(length) {}

ConstArg::ConstArg(CommonInfo *Parent, Expr *E, clang::ASTContext *Context) :
    Arg(A_Const,Parent,E,Context), ImplDefault(false) {}

ConstArg::ConstArg(CommonInfo *Parent, unsigned Value) :
    Arg(A_Const,Parent,Value), ImplDefault(true) {}

ConstArg::ConstArg(CommonInfo *Parent, llvm::APSInt Value) :
    Arg(A_Const,Parent,Value), ImplDefault(true) {}


bool
OpenACC::isVarExpr(Expr *E) {
    //ignore arrays, they must be recognized as ArrayArg only
    if (isArrayExpr(E))
        return false;

    //Step 1: check if is declaration reference
    //Step 2: check if it is a variable reference
    if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(E))
        if (isa<VarDecl>(DeclRef->getDecl()))
            return true;
    return false;
}

bool
OpenACC::isArrayExpr(Expr *E) {
    //this finds all the array variables (not elements)
    //if (const ArrayType *ATy = S.getASTContext().getAsArrayType(E->getType()))
    //S.Diag(E->getExprLoc(),diag::note_pragma_acc_test) << ATy->getTypeClassName();
    if (S.getASTContext().getAsArrayType(E->getType()))
        return true;
    return false;
}

bool
OpenACC::isArrayElementExpr(Expr *E) {
    return dyn_cast<ArraySubscriptExpr>(E);
}

bool
OpenACC::isSubArrayExpr(Expr *E) {
    return LengthTmp && dyn_cast<ArraySubscriptExpr>(E);
}

Arg*
OpenACC::CreateArg(Expr *E, CommonInfo *Common) {
    //S.Diag(E->getExprLoc(),diag::note_pragma_acc_test)
    //    << E->getType()->getTypeClassName();

    //take any existing ArrayElementArg or SubArrayArg
    Arg *A = 0;

    if (Common->getAsClause() &&
        Common->getAsClause()->getKind() == CK_IF)
        A = new RawExprArg(Common,E,&S.getASTContext());
    else if (isSubArrayExpr(E))
        A = new SubArrayArg(Common,E,LengthTmp,&S.getASTContext());
    else if (isArrayElementExpr(E))
        A = new ArrayElementArg(Common,E,&S.getASTContext());
    else if (isArrayExpr(E))
        A = new ArrayArg(Common,E,&S.getASTContext());
    else if (isVarExpr(E))
        A = new VarArg(Common,E,&S.getASTContext());
    else if (!E->isIntegerConstantExpr(S.getASTContext()))
        A = new RawExprArg(Common,E,&S.getASTContext());
    else
        A = new ConstArg(Common,E,&S.getASTContext());

    LengthTmp = 0;
    return A;
}

void
OpenACC::FindAndKeepSubArrayLength(SourceLocation ColonLoc, Expr *ase, Expr *Length) {
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
    assert((DI->getKind() == DK_PARALLEL || DI->getKind() == DK_PARALLEL_LOOP)
           && "Bad Call");

    bool MissingNum_Gangs(true);
    bool MissingNum_Workers(true);
    bool MissingVector_length(true);
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseInfo *CI = *II;
        if (CI->getKind() == CK_NUM_GANGS)
            MissingNum_Gangs = false;
        else if (CI->getKind() == CK_NUM_WORKERS)
            MissingNum_Workers = false;
        else if (CI->getKind() == CK_VECTOR_LENGTH)
            MissingVector_length = false;
    }

    if (MissingNum_Gangs) {
        ClauseInfo *CI = new DefaultClauseInfo(CK_NUM_GANGS,DI);
        Arg *A = new ConstArg(CI,1);
        CI->setArg(A);
        CList.push_back(CI);
    }
    if (MissingNum_Workers) {
        ClauseInfo *CI = new DefaultClauseInfo(CK_NUM_WORKERS,DI);
        Arg *A = new ConstArg(CI,1);
        CI->setArg(A);
        CList.push_back(CI);
    }
    if (MissingVector_length) {
        ClauseInfo *CI = new DefaultClauseInfo(CK_VECTOR_LENGTH,DI);
        Arg *A = new ConstArg(CI,1);
        CI->setArg(A);
        CList.push_back(CI);
    }
}

bool
OpenACC::isValidDirectiveWrapper(DirectiveInfo *DI) {
//http://www.parashift.com/c++-faq-lite/pointers-to-members.html
#define ACC_CALL(method) ((*this).*(method))

    if (PendingDirective)
        DiscardAndWarn();

    if (!ACC_CALL(isValidDirective[DI->getKind()])(DI))
        return false;

    //we reach here only if the Directive is valid
    PendingDirective = DI;
    return true;
}

bool
OpenACC::isValidClauseWrapper(DirectiveKind DK, ClauseInfo *CI) {
//http://www.parashift.com/c++-faq-lite/pointers-to-members.html
#define ACC_CALL(method) ((*this).*(method))

    return ACC_CALL(isValidClause[CI->getKind()])(DK,CI);
}

bool
OpenACC::isValidClauseIf(DirectiveKind DK, ClauseInfo *CI) {
    //At most one if clause may appear. In C or C++, the condition must
    //evaluate to a scalar integer value.

    Expr *CondExpr = CI->getArg()->getExpr();
    //FullExprArg FullCondExpr(S.MakeFullExpr(CondExpr));
    //ExprResult CondResult = FullCondExpr.release();
    ExprResult CondResult = S.MakeFullExpr(CondExpr).release();

    CondExpr = CondResult.takeAs<Expr>();
    CondResult = S.ActOnBooleanCondition(S.getCurScope(), CondExpr->getExprLoc(), CondExpr);

    if (CondResult.isInvalid()) {
        S.Diag(CI->getStartLocation(),diag::err_pragma_acc_invalid_clause_if_condition);
        return false;
    }

    return true;
}

bool
OpenACC::isValidClauseAsync(DirectiveKind DK, ClauseInfo *CI) {
    if (DK == DK_PARALLEL || DK == DK_KERNELS) {
        /*
          The async clause is optional on the parallel and kernels constructs;

          when there is no
          async clause, the host process will wait until the parallel or kernels region is complete
          before executing any of the code that follows the construct.

          When there is an async clause,
          the parallel or kernels region will be executed by the accelerator device asynchronously while
          the host process continues with the code following the region.

          If present, the argument to the async must be an integer expression (int for C or C++,
          integer for Fortran). The same integer expression value may be used in a wait directive
          or various runtime routines to have the host process test for or wait for completion of the
          region. An async clause may also be used with no argument, in which case the
          implementation will use a value distinct from all explicit async arguments in the program.
         */
        if (CI->hasArgs())
            return FindValidICE(CI->getArg());
        needBackPatch(CI->getArg());
        return true;
    }

    if (DK == DK_UPDATE) {
        /*
          If the async clause has an argument, that argument must be the name of an integer variable
          (int for C or C++, integer for Fortran). The variable may be used in a wait directive or
          various runtime routines to make the host process test or wait for completion of the update.

          An async clause may also be used with no argument, in which case the implementation will
          use a value distinct from all explicit async arguments in the program.

          Two asynchronous activities with the same argument value will be executed on the device in
          the order they are encountered by the host process. Two asynchronous activities with
          different handle values may be executed on the device in any order relative to each other. If
          there are two or more host threads executing and sharing the same accelerator device, two
          asynchronous activities with the same argument value will execute on the device one after the
          other, though the relative order is not determined.
         */

        if (!CI->hasArgs()) {
            needBackPatch(CI->getArg());
            return true;
        }

        //check for integer variable
        Arg *A = CI->getArg();
        if ((isa<VarArg>(A) || isa<ArrayElementArg>(A)) &&
            A->getExpr()->getType()->isIntegralType(S.getASTContext())) {
            needBackPatch(CI->getArg());
            return true;
        }

#if 1
        //comply with OpenACC 2.0

        if (CI->hasArgs())
            return FindValidICE(CI->getArg());
        needBackPatch(CI->getArg());
        return true;
#endif

        S.Diag(A->getStartLocation(),diag::err_pragma_acc_expected_integer_variable);
        return false;
    }

    return false;
}

bool
OpenACC::isValidClauseNum_gangs(DirectiveKind DK, ClauseInfo *CI) {
    if (CI->hasArgs())
        return FindValidICE(CI->getArg());
    SetDefaultICE(CI->getArg());
    return true;
}

bool
OpenACC::isValidClauseNum_workers(DirectiveKind DK, ClauseInfo *CI) {
    if (CI->hasArgs())
        return FindValidICE(CI->getArg());
    SetDefaultICE(CI->getArg());
    return true;
}

bool
OpenACC::isValidClauseVector_length(DirectiveKind DK, ClauseInfo *CI) {
    if (CI->hasArgs())
        return FindValidICE(CI->getArg());
    SetDefaultICE(CI->getArg());
    return true;
}

bool
OpenACC::isValidClauseReduction(DirectiveKind DK, ClauseInfo *CI) {
    if (DK == DK_PARALLEL) {
        //For each variable, a private copy is created for each parallel gang
        //and initialized for that operator. At the end of the region, the values
        //for each gang are combined using the reduction operator, and the result
        //combined with the value of the original variable and stored in the
        //original variable. The reduction result is available after the region.
    }

    if (DK == DK_LOOP) {
        /*
          The reduction clause is allowed on a loop construct with the gang,
          worker or vector clauses. It specifies a reduction operator and one
          or more scalar variables. For each reduction variable, a private copy
          is created for each iteration of the associated loop or loops and
          initialized for that operator; see the table in section 2.4.10. At the
          end of the loop, the values for each iteration are combined using the
          specified reduction operator, and the result stored in the original
          variable at the end of the parallel or kernels region.

          In a parallel region, if the reduction clause is used on a loop with
          the vector or worker clauses (and no gang clause), and the scalar
          variable also appears in a private clause on the parallel construct,
          the value of the private copy of the scalar will be updated at the
          exit of the loop. Otherwise, variables that appear in a reduction
          clause on a loop in a parallel region will not be updated until the
          end of the region.

         */
    }

    //Supported data types are the numerical data types in C and C++
    //(int, float, double, complex)

    if (WarnOnInvalidReductionList(CI->getArgs()))
        return false;

    bool status = true;
    ArgVector &Args = CI->getArgs();
    for (ArgVector::iterator II(Args.begin()), EE(Args.end()); II != EE; ++II) {
        Arg *A = *II;
        if (RStack.FindReductionInRegionStack(A)) {
            SourceLocation Loc =A->getStartLocation();
            S.Diag(Loc,diag::err_pragma_acc_duplicate_vardecl);
            status = false;
        }
    }

    return status;
}

bool
OpenACC::isValidClauseCopy(DirectiveKind DK, ClauseInfo *CI) {
    //The data is copied to the device memory before entry to the region,
    //and data copied back to the host memory when the region is complete.

    return true;
}

bool
OpenACC::isValidClauseCopyin(DirectiveKind DK, ClauseInfo *CI) {
    //The data is copied to the device memory upon entry to the region.

    return true;
}

bool
OpenACC::isValidClauseCopyout(DirectiveKind DK, ClauseInfo *CI) {
    //the data need not be copied to the device memory from the host memory,
    //even if those values are used on the accelerator. The data is copied back
    //to the host memory upon exit from the region.

    return true;
}

bool
OpenACC::isValidClauseCreate(DirectiveKind DK, ClauseInfo *CI) {
    //the variables, arrays or subarrays in the list need to be allocated
    //(created) in the device memory, but the values in the host memory are not
    //needed on the accelerator, and any values computed and assigned on the
    //accelerator are not needed on the host. No data in this clause will be
    //copied between the host and device memories.

    //just allocate space in device memory, no data transfers
    //possibly free that space afterwards

    return true;
}

bool
OpenACC::isValidClausePresent(DirectiveKind DK, ClauseInfo *CI) {
    //the variables or arrays in the list are already present in the accelerator
    //memory due to data regions that contain this region, perhaps from
    //procedures that call the procedure containing this construct. The
    //implementation will find and use that existing accelerator data.

    //If there is no containing data region that has placed any of the variables
    //or arrays on the accelerator, the program will halt with an error.

    //If the containing data region specifies a subarray, the present clause must
    //specify the same subarray, or a subarray that is a proper subset of the
    //subarray in the data region. It is a runtime error if the subarray in the
    //present clause includes array elements that are not part of the subarray
    //specified in the data region.

    //the programmer is responsible for the correctness of the source code
    //if (WarnOnMissingVisibleCopy(CI->getArgs(),/*IgnoreDeviceResident=*/false))
    //    return false;

    return true;
}

bool
OpenACC::isValidClausePcopy(DirectiveKind DK, ClauseInfo *CI) {
    //test whether each of the variables or arrays on the list is already
    //present in the accelerator memory. If it is already present, that
    //accelerator data is used.

    //else same as 'copy' clause
    return true;
}

bool
OpenACC::isValidClausePcopyin(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
OpenACC::isValidClausePcopyout(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
OpenACC::isValidClausePcreate(DirectiveKind DK, ClauseInfo *CI) {
    return true;
}

bool
OpenACC::isValidClauseDeviceptr(DirectiveKind DK, ClauseInfo *CI) {
    //is used to declare that the pointers in the list are device pointers, so
    //the data need not be allocated or moved between the host and device for
    //this pointer.

    //In C and C++, the variables in list must be pointers

    bool status(true);

    ArgVector &Args = CI->getArgs();
    for (ArgVector::iterator II(Args.begin()), EE(Args.end()); II != EE; ++II) {
        Arg *A = *II;
        switch (A->getKind()) {
        case A_RawExpr:
            llvm_unreachable("unexpected Arg Kind");
        case A_Var: {
            //S.Diag(A->getStartLocation(),diag::note_pragma_acc_test) << "test for VarArg";
            ValueDecl *VD = cast<ValueDecl>(A->getVarDecl());
            if (!VD->getType()->isPointerType()) {
                SourceLocation Loc = A->getStartLocation();
                S.Diag(Loc,diag::err_pragma_acc_expected_pointer);
                status = false;
            }
            break;
        }
        case A_Array: {
            //consider array declarations as pointers
            //S.Diag(A->getStartLocation(),diag::note_pragma_acc_test) << "test for ArrayArg";
            break;
        }
        case A_ArrayElement: {
            //S.Diag(A->getStartLocation(),diag::note_pragma_acc_test) << "test for ArrayElementArg";
            //check element type
            if (!A->getExpr()->getType()->isPointerType()) {
                SourceLocation Loc = A->getStartLocation();
                S.Diag(Loc,diag::err_pragma_acc_expected_pointer);
                status = false;
            }
            break;
        }
        case A_SubArray: {
            //S.Diag(A->getStartLocation(),diag::note_pragma_acc_test) << "test for SubArrayArg";
            status = false;
            break;
        }
        case A_Const:
            //S.Diag(A->getStartLocation(),diag::note_pragma_acc_test) << "test for ConstArg";
            status = false;
            break;
        }
    }
    return status;
}

bool
OpenACC::isValidClausePrivate(DirectiveKind DK, ClauseInfo *CI) {
    if (DK == DK_PARALLEL) {
        //it declares that a copy of each item on the list will be created
        //for each parallel gang.

        //the copy will be initialized with the value of that item on the host
        //when the parallel construct is encountered.
    }

    if (DK == DK_LOOP) {
        //The private clause on a loop directive specifies that a copy of each
        //item on the list will be created for each iteration of the associated
        //loop or loops.
    }

    //seperate check for duplicates only inside the private clause's arguments
    ArgVector Tmp;
    if (WarnOnDuplicatesInList(Tmp,CI->getArgs()))
        return false;

    //creating a copy of a pointer inside an OpenCL kernel changes the address
    //space, and it is a compiler error, catch it here first

    bool status = true;
    ArgVector &Args = CI->getArgs();
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (!isa<VarArg>(A) && !isa<ArrayElementArg>(A)) {
            S.Diag(A->getStartLocation(),diag::err_pragma_acc_illegal_arg_kind)
                << A->getKindAsString()
                << CI->getParentDirective()->getAsString() + " Directive";
            status = false;
        }
    }

    return status;
}

bool
OpenACC::isValidClauseFirstprivate(DirectiveKind DK, ClauseInfo *CI) {
    //only in parallel directive

    //it declares that a copy of each item on the list will be created
    //for each parallel gang.

    //seperate check for duplicates only inside the firstprivate clause's arguments
    ArgVector Tmp;
    if (WarnOnDuplicatesInList(Tmp,CI->getArgs()))
        return false;

    //creating a copy of a pointer inside an OpenCL kernel changes the address
    //space, and it is a compiler error, catch it here first

    bool status = true;
    ArgVector &Args = CI->getArgs();
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (!isa<VarArg>(A) && !isa<ArrayElementArg>(A)) {
            S.Diag(A->getStartLocation(),diag::err_pragma_acc_illegal_arg_kind)
                << A->getKindAsString()
                << CI->getParentDirective()->getAsString() + " Directive";
            status = false;
        }
    }

    return status;
}

bool
OpenACC::isValidClauseUse_device(DirectiveKind DK, ClauseInfo *CI) {
    //The variables or arrays in list must be present in the accelerator
    //memory due to data regions that contain this construct

    if (WarnOnSubArrayArg(CI->getArgs()))
        return false;

    if (WarnOnMissingVisibleCopy(CI->getArgs(),/*IgnoreDeviceResident=*/false))
        return false;

    return true;
}

bool
OpenACC::isValidClauseCollapse(DirectiveKind DK, ClauseInfo *CI) {
    /* The collapse clause is used to specify how many tightly nested loops are
       associated with the loop construct. The argument to the collapse clause
       must be a constant positive integer expression. If no collapse clause is
       present, only the immediately following loop is associated with the loop
       directive.

       If more than one loop is associated with the loop construct, the
       iterations of all the associated loops are all scheduled according to the
       rest of the clauses. The trip count for all loops associated with the
       collapse clause must be computable and invariant in all the loops.

       It is implementation-defined whether a gang, worker or vector clause on the directive is
       applied to each loop, or to the linearized iteration space.
     */

    return FindValidICE(CI->getArg());
}

bool
OpenACC::isValidClauseGang(DirectiveKind DK, ClauseInfo *CI) {
    if (RStack.InRegion(DK_PARALLEL) || DK == DK_PARALLEL_LOOP) {

        //In an accelerator parallel region, the gang clause specifies that
        //the iterations of the associated loop or loops are to be executed in
        //parallel by distributing the iterations among the gangs created by the
        //parallel construct.
        //
        //No argument is allowed.
        //
        //The loop iterations must
        //be data independent, except for variables specified in a reduction
        //clause.

        if (CI->hasArgs()) {
            S.Diag(CI->getStartLocation(),diag::err_pragma_acc_illegal_arg_location);
            return false;
        }

        return true;
    }

    if (CI->hasArgs())
        return FindValidICE(CI->getArg());
    SetDefaultICE(CI->getArg());
    return true;
}

bool
OpenACC::isValidClauseWorker(DirectiveKind DK, ClauseInfo *CI) {
    if (RStack.InRegion(DK_PARALLEL) || DK == DK_PARALLEL_LOOP) {

        //In an accelerator parallel region, the worker clause specifies that
        //the iterations of the associated loop or loops are to be executed in
        //parallel by distributing the iterations among the multiple workers
        //within a single gang.
        //
        //No argument is allowed.
        //
        //The loop iterations must
        //be data independent, except for variables specified in a reduction
        //clause.

        //It is implementation-defined whether a loop with the worker clause may
        //contain a loop containing the gang clause.

        if (CI->hasArgs()) {
            S.Diag(CI->getStartLocation(),diag::err_pragma_acc_illegal_arg_location);
            return false;
        }

        return true;
    }

    if (CI->hasArgs())
        return FindValidICE(CI->getArg());
    SetDefaultICE(CI->getArg());
    return true;
}

bool
OpenACC::isValidClauseVector(DirectiveKind DK, ClauseInfo *CI) {
    if (RStack.InRegion(DK_PARALLEL) || DK == DK_PARALLEL_LOOP) {
        if (CI->hasArgs()) {
            //this is an error, the argument from vector_length clause must be used
            S.Diag(CI->getStartLocation(),diag::err_pragma_acc_illegal_arg_location);
            return false;
        }
    }
    else if (RStack.InRegion(DK_KERNELS) || DK == DK_KERNELS_LOOP) {
        if (CI->hasArgs())
            return FindValidICE(CI->getArg());
        SetDefaultICE(CI->getArg());
    }
    return true;
}

bool
OpenACC::isValidClauseSeq(DirectiveKind DK, ClauseInfo *CI) {
    //the associated loop or loops are to be executed sequentially by the
    //accelerator; this is the default in an accelerator parallel region

    return true;
}

bool
OpenACC::isValidClauseIndependent(DirectiveKind DK, ClauseInfo *CI) {
    //The independent clause is allowed on loop directives in kernels regions, and tells the
    //compiler that the iterations of this loop are data-independent with respect to each other. This
    //allows the compiler to generate code to execute the iterations in parallel with no
    //synchronization.

    if (RStack.InRegion(DK_KERNELS) || DK == DK_KERNELS_LOOP)
        return true;

    S.Diag(CI->getStartLocation(),diag::err_pragma_acc_illegal_clause_location)
        << CI->getAsString();
    return false;
}

bool
OpenACC::isValidClauseDevice_resident(DirectiveKind DK, ClauseInfo *CI) {
    //The device_resident specifies that the memory for the named variables
    //should be allocated in the accelerator device memory, not in the host
    //memory. In C and C++, this means the host may not be able to access these
    //variables.

    //Variables in list must be file static or local to a function.

    bool status(true);

    ArgVector &Args = CI->getArgs();
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (isa<VarArg>(A) || isa<ArrayArg>(A)) {
            VarDecl *V = A->getVarDecl();
            const bool Valid = V->isLocalVarDecl() ||
                (V->hasGlobalStorage() && V->getStorageClass() == SC_Static);
            if (!Valid) {
                SourceLocation Loc = A->getStartLocation();
                S.Diag(Loc,diag::err_pragma_acc_expected_local_or_filestatic);
                status = false;
            }
        }
    }
    return status;
}

bool
OpenACC::isValidClauseHost(DirectiveKind DK, ClauseInfo *CI) {
    //The host clause specifies that the variables, arrays or subarrays in the
    //list are to be copied from the accelerator device memory to the host
    //memory.

    //There must be a visible device copy of the variables or arrays
    //that appear in the list
    if (WarnOnMissingVisibleCopy(CI->getArgs(),/*IgnoreDeviceResident=*/true))
        return false;

    return true;
}

bool
OpenACC::isValidClauseDevice(DirectiveKind DK, ClauseInfo *CI) {
    //The device clause specifies that the variables, arrays or subarrays in the
    //list are to be copied from the accelerator host memory to the accelerator
    //device memory.

    //There must be a visible device copy of the variables or arrays
    //that appear in the list
    if (WarnOnMissingVisibleCopy(CI->getArgs(),/*IgnoreDeviceResident=*/true))
        return false;

    return true;
}

bool
OpenACC::isValidDirectiveParallel(DirectiveInfo *DI) {
    //check for missing clauses and apply implementation defaults
    MaybeSetImplementationDefaultClauses(DI);

    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        //ClauseInfo *CI = *II;
    }

    return true;
}

bool
OpenACC::isValidDirectiveParallelLoop(DirectiveInfo *DI) {
    //check for missing clauses and apply implementation defaults
    MaybeSetImplementationDefaultClauses(DI);

    ClauseInfo *CI_Vector_length = 0;
    ClauseInfo *CI_Vector = 0;

    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseInfo *CI = *II;
        if (CI->getKind() == CK_VECTOR_LENGTH)
            CI_Vector_length = CI;
        else if (CI->getKind() == CK_VECTOR)
            CI_Vector = CI;
    }

    if (CI_Vector)
        CI_Vector->setArg(new ConstArg(CI_Vector,
                                       CI_Vector_length->getArg()->getICE()));

    return true;
}

bool
OpenACC::isValidDirectiveKernels(DirectiveInfo *DI) {
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        //ClauseInfo *CI = *II;
    }

    return true;
}

bool
OpenACC::isValidDirectiveKernelsLoop(DirectiveInfo *DI) {
    return true;
}

bool
OpenACC::isValidDirectiveData(DirectiveInfo *DI) {
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        //ClauseInfo *CI = *II;
    }

    return true;
}

bool
OpenACC::isValidDirectiveHostData(DirectiveInfo *DI) {
    //makes the address of device data available on the host

    //The variables or arrays in list must be present in the accelerator
    //memory due to data regions that contain this construct.

    bool status = true;

    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II)
        if (WarnOnMissingVisibleCopy((*II)->getArgs(),/*IgnoreDeviceResident=*/false))
            status = false;

    return status;
}

bool
OpenACC::isValidDirectiveLoop(DirectiveInfo *DI) {
    return true;
}

bool
OpenACC::isValidDirectiveCache(DirectiveInfo *DI) {
    //The cache directive may appear at the top of (inside of) a loop.
    //It specifies array elements or subarrays that should be fetched
    //into the highest level of the cache for the body of the loop.

    //The entries in list must be single array elements or simple subarray.

    bool status = true;

    ArgVector &Args = DI->getArgs();
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (!isa<ArrayElementArg>(A) && !isa<SubArrayArg>(A)) {
            S.Diag(A->getStartLocation(),diag::err_pragma_acc_illegal_arg_kind)
                << A->getKindAsString()
                << "cache Directive";
            status = false;
        }
        else if (SubArrayArg *Sub = dyn_cast<SubArrayArg>(A)) {
            //FIXME: add checks for LowBound to the external tool

            //the lower bound is a constant, loop invariant, or the for loop
            //index variable plus or minus a constant or loop invariant

            //the length is a constant

            Expr *Length = Sub->getLength();
            if (!Length || !Length->isIntegerConstantExpr(S.getASTContext())) {
                S.Diag(Sub->getStartLocation(),diag::err_pragma_acc_missing_subarray_length);
                status = false;
            }
        }
    }

    return status;
}

bool
OpenACC::isValidDirectiveDeclare(DirectiveInfo *DI) {
    //following an variable declaration in C or C++

    //It can specify that a variable or array is to be allocated in the device
    //memory for the duration of the implicit data region of a function,
    //subroutine or program, and specify whether the data values are to be
    //transferred from the host to the device memory upon entry to the
    //implicit data region, and from the device to the host memory upon exit
    //from the implicit data region. These directives create a visible device
    //copy of the variable or array.

    //The associated region is the implicit region associated with the function,
    //subroutine, or program in which the directive appears.

    //Restrictions
    //
    //A variable or array may appear at most once in all the clauses of declare
    //directives for a function, subroutine, program, or module.

    if (MarkArgsWithVisibleCopy(DI->getClauseList()))
        return false;

    //
    //Subarrays are not allowed in declare directives.
    //
    //If a variable or array appears in a declare directive, the same variable
    //or array may not appear in a data clause for any construct where the
    //declaration of the variable is visible.
    //
    //The compiler may pad dimensions of arrays on the accelerator to improve
    //memory alignment and program performance.

    bool status(true);
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseInfo *CI = *II;
        if (WarnOnSubArrayArg(CI->getArgs()))
            status = false;
    }
    return status;
}

bool
OpenACC::isValidDirectiveUpdate(DirectiveInfo *DI) {
    //The update directive is used within an explicit or implicit data region to
    //update all or part of a host memory array with values from the
    //corresponding array in device memory, or to update all or part of a device
    //memory array with values from the corresponding array in host memory.

    //The updates are done in the order in which they appear on the directive.
    //There must be a visible device copy of the variables or arrays that appear
    //in the host or device clauses. At least one host or device clause must
    //appear.

    /* Restrictions

       The update directive is executable. It must not appear in place of the statement
       following an if, while, do, switch, or label in C or C++,

       A variable or array which appears in the list of an update directive must have a
       visible device copy.
    */

    bool status(false);
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseKind CK = (*II)->getKind();
        if (CK == CK_HOST || CK == CK_DEVICE)
            status = true;
    }

    if (!status)
        S.Diag(DI->getStartLocation(),diag::err_pragma_acc_incomplete_update);

    return status;
}

bool
OpenACC::isValidDirectiveWait(DirectiveInfo *DI) {
    //The wait directive causes the program to wait for completion of an
    //asynchronous activity, such as an accelerator parallel or kernels region
    //or update directive.

    //The argument, if specified, must be an integer expression
    //(int for C or C++). The host thread will wait until all asynchronous
    //activities that had an async clause with an argument with the same value
    //have completed. If no argument is specified, the host process will wait
    //until all asynchronous activities have completed.

    //If there are two or more host threads executing and sharing the same
    //accelerator device, a wait directive will cause the host thread to wait
    //until at least all of the appropriate asynchronous activities initiated by
    //that host thread have completed. There is no guarantee that all the
    //similar asynchronous activities initiated by some other host thread will
    //have completed.

    if (!DI->hasArgs())
    //wait for all
    return true;

    //check for integer variable
    Arg *A = DI->getArg();
    if ((isa<VarArg>(A) || isa<ArrayElementArg>(A)) &&
        A->getExpr()->getType()->isIntegralType(S.getASTContext()))
        return true;

    return FindValidICE(DI->getArg());
}

void
OpenACC::WarnOnDirective(DirectiveInfo *DI) {
    if (DI)
        S.Diag(DI->getStartLocation(),diag::err_pragma_acc_illegal_directive_location)
            << DI->getAsString();
}

void
OpenACC::DiscardAndWarn() {
    WarnOnDirective(PendingDirective);
    PendingDirective = 0;
}

DirectiveInfo*
OpenACC::MayConsumeDataOrComputeDirective() {
    //we call this method before the next statement, so we do not know its kind
    //we must check for each statement, if we have a valid directive or not

    if (!PendingDirective)
        return 0;

    if (PendingDirective->isStartOfLoopRegion())
        return 0;

    assert(!PendingDirective->isExecutableOrCacheOrDeclareDirective());
    assert(PendingDirective->isComputeDirective() || PendingDirective->isDataDirective());

    DirectiveInfo *DI = PendingDirective;
    PendingDirective = 0;
    return DI;
}

DirectiveInfo*
OpenACC::MayConsumeLoopDirective() {
    if (!PendingDirective)
        return 0;

    assert(!PendingDirective->isDataDirective() && "Unhandled Implicit Region");

    if (PendingDirective->isComputeDirective()) {
        S.Diag(PendingDirective->getStartLocation(),diag::warn_pragma_acc_typo_loop);
        //ignore without error message
        PendingDirective = 0;
        //DiscardAndWarn();
        return 0;
    }
    assert(PendingDirective->isStartOfLoopRegion());
    DirectiveInfo *DI = PendingDirective;
    PendingDirective = 0;
    return DI;
}

DirectiveInfo*
OpenACC::MayConsumeExecutableOrCacheOrDeclareDirective() {
    if (!PendingDirective)
        return 0;
    if (!PendingDirective->isExecutableOrCacheOrDeclareDirective())
        return 0;
    DirectiveInfo *DI = PendingDirective;
    PendingDirective = 0;
    return DI;
}

void
OpenACC::WarnOnInvalidUpdateDirective(Stmt *Body) {
    if (!Body)
        return;

    else if (AccStmt *ACC = dyn_cast<AccStmt>(Body)) {
        DirectiveInfo *DI = ACC->getDirective();
        //FIXME: test the non exec case, should be removed?
        //assert(DI->isExecutableOrCacheOrDeclareDirective() && "Unhandled Exec case");
        if (DI->getKind() == DK_UPDATE)
            S.Diag(ACC->getLocStart(),diag::err_pragma_acc_illegal_update);
    }

    else if (CompoundStmt *Compound = dyn_cast<CompoundStmt>(Body)) {
    if (Compound->body_empty())
        return;

    CompoundStmt::body_iterator II = Compound->body_begin();
        if (AccStmt *ACC = dyn_cast<AccStmt>(*II))
            if (ACC->getDirective()->getKind() == DK_UPDATE)
                S.Diag(ACC->getLocStart(),diag::err_pragma_acc_illegal_update);
    }
}

bool
OpenACC::WarnOnInvalidCacheDirective(Stmt *Body, bool InsideLoop) {
    //return true on error

    //simple statement
    if (!isa<CompoundStmt>(Body))
        return false;

    CompoundStmt *Compound = cast<CompoundStmt>(Body);
    if (Compound->body_empty())
        return false;

    CompoundStmt::body_iterator II = Compound->body_begin();
    CompoundStmt::body_iterator EE = Compound->body_end();

    if (InsideLoop) {
        if (Compound->size() == 1)
            if (AccStmt *ACC = dyn_cast<AccStmt>(*II)) {
                if (ACC->getDirective()->getKind() == DK_CACHE) {
                    S.Diag(ACC->getLocStart(),diag::err_pragma_acc_trivial_cache);
                    return true;
                }
            }
        ++II;
    }

    bool status(false);
    for (; II != EE; ++II) {
        if (AccStmt *ACC = dyn_cast<AccStmt>(*II)) {
            if (ACC->getDirective()->getKind() == DK_CACHE) {
                S.Diag(ACC->getLocStart(),diag::err_pragma_acc_illegal_cache);
                status = true;
            }
        }
    }
    return status;
}

StmtResult
OpenACC::CreateDataOrComputeRegion(Stmt *SubStmt, DirectiveInfo *DI) {
    //we may have an implicit Region

    assert(DI && "Bad Call");
    //S.Diag(SubStmt->getLocStart(),diag::note_pragma_acc_test) << "this statement";
    //S.Diag(DI->getStartLocation(),diag::note_pragma_acc_test) << "has this directive";
    assert((DI->isDataDirective() || DI->isComputeDirective()) && "Bad Call");

    if (!isa<CompoundStmt>(SubStmt)) {
        //is Implicit
        S.Diag(DI->getStartLocation(),diag::warn_pragma_acc_implicit_region);
        S.Diag(SubStmt->getLocStart(),diag::note_pragma_acc_implicit_region);
        if (AccStmt *ACC = dyn_cast<AccStmt>(SubStmt))
            S.Diag(ACC->getDirective()->getStartLocation(),diag::note_pragma_acc_test) << "including this directive";
    }

    if (RStack.DetectInvalidConstructNesting(DI)) {
        SourceLocation Loc = DI->getStartLocation();
        S.Diag(Loc,diag::err_pragma_acc_invalid_nesting);
        if (!RStack.empty()) {
            SourceLocation ParentLoc = RStack.back()->getStartLocation();
            S.Diag(ParentLoc,diag::note_pragma_acc_invalid_nesting);
        }
        return StmtEmpty();
    }

    if (WarnOnInvalidLists(DI))
        return StmtEmpty();

    AccStmt *ACC = DI->getAccStmt();
    ACC->setSubStmt(SubStmt);
    return S.Owned(ACC);
}

StmtResult
OpenACC::CreateLoopRegion(ForStmt *SubStmt, DirectiveInfo *DI) {
    assert(SubStmt && "null SubStmt");
    assert(DI->isStartOfLoopRegion() && "Bad Call");

    if (RStack.DetectInvalidConstructNesting(DI)) {
        SourceLocation Loc = DI->getStartLocation();
        S.Diag(Loc,diag::err_pragma_acc_invalid_nesting);
        if (!RStack.empty()) {
            SourceLocation ParentLoc = RStack.back()->getStartLocation();
            S.Diag(ParentLoc,diag::note_pragma_acc_invalid_nesting);
        }
        return StmtEmpty();
    }

    if (WarnOnInvalidCacheDirective(SubStmt->getBody(),/*InsideLoop=*/true))
        return StmtEmpty();

    if (WarnOnInvalidLists(DI))
        return StmtEmpty();

    //S.Diag(SubStmt->getLocStart(),diag::note_pragma_acc_test) << "this for statement";
    //S.Diag(DI->getStartLocation(),diag::note_pragma_acc_test) << "has this directive";

    AccStmt *ACC = DI->getAccStmt();
    ACC->setSubStmt(SubStmt);
    return S.Owned(ACC);
}

StmtResult
OpenACC::MayCreateStatementFromPendingExecutableOrCacheOrDeclareDirective() {
    DirectiveInfo *DI = MayConsumeExecutableOrCacheOrDeclareDirective();
    if (!DI)
        return StmtEmpty();

    if (WarnOnInvalidLists(DI))
        return StmtEmpty();

    //declare directive just changes the state of a Decl
    //we already checked for validity
    if (DI->getKind() != DK_DECLARE)
        if (RStack.DetectInvalidConstructNesting(DI)) {
            SourceLocation Loc = DI->getStartLocation();
            S.Diag(Loc,diag::err_pragma_acc_invalid_nesting);
            if (!RStack.empty()) {
                SourceLocation ParentLoc = RStack.back()->getStartLocation();
            S.Diag(ParentLoc,diag::note_pragma_acc_invalid_nesting);
            }
            return StmtEmpty();
        }

    return S.Owned(DI->getAccStmt());
}

bool
OpenACC::FindValidICE(Arg *A) {
    if (A->isICE() && A->getICE().isNonNegative())
        return true;

    S.Diag(A->getStartLocation(),diag::err_pragma_acc_expected_ice);
    return false;
}

void
OpenACC::SetDefaultICE(Arg *A, unsigned Default) {
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
OpenACC::WarnOnDuplicatesInList(ArgVector &Tmp, ArgVector &Args) {
    //return true on error

    bool status(false);
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (isa<VarArg>(A) || isa<ArrayArg>(A)) {
            if (FindVarDecl(Tmp,A)) {
                SourceLocation Loc =A->getStartLocation();
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
RegionStack::FindVisibleCopyInClause(ClauseInfo *CI, Arg *Target, bool IgnoreDeviceResident) {
    const bool IsDeviceResident = CI->getKind() == CK_DEVICE_RESIDENT;

    if (!CI->isDataClause() && !IsDeviceResident)
        return 0;

    if (IgnoreDeviceResident && IsDeviceResident)
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
RegionStack::FindVisibleCopyInDirective(DirectiveInfo *DI, Arg *Target, bool IgnoreDeviceResident) {
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II)
        if (Arg *A = FindVisibleCopyInClause(*II,Target,IgnoreDeviceResident))
            return A;
    return 0;
}

Arg*
RegionStack::FindVisibleCopyInRegionStack(Arg *Target, bool IgnoreDeviceResident) {
    //return the most recent Clause

    for (reverse_iterator II(rbegin()), EE(rend()); II != EE; ++II)
        if (Arg *A = FindVisibleCopyInDirective(*II,Target,IgnoreDeviceResident))
            return A;

    ArgVector &Args = Target->getVarDecl()->getOpenAccArgs();
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        ClauseInfo *ParentCI = A->getParent()->getAsClause();
        assert(ParentCI);
        if (IgnoreDeviceResident && ParentCI->getKind() == CK_DEVICE_RESIDENT)
            continue;
        if (A->Matches(Target))
            return A;
    }

    return 0;
}

/////////////////////////////////////////////////////////////////////////////
static
Arg*
FindDataOnDeviceFromClause(ClauseInfo *CI, Arg *Target, bool IgnoreDeviceResident) {
    const bool IsDeviceResident = CI->getKind() == CK_DEVICE_RESIDENT;

    if (!CI->isDataClause() && !IsDeviceResident)
        return 0;

    if (IgnoreDeviceResident && IsDeviceResident)
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
FindDataOnDeviceFromDirective(DirectiveInfo *DI, Arg *Target, bool IgnoreDeviceResident) {
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II)
        if (Arg *A = FindDataOnDeviceFromClause(*II,Target,IgnoreDeviceResident))
            return A;
    return 0;
}

Arg*
RegionStack::FindDataOnDevice(Arg *Target, bool IgnoreDeviceResident) {
    //return the most recent Clause

    for (reverse_iterator II(rbegin()), EE(rend()); II != EE; ++II)
        if (Arg *A = FindDataOnDeviceFromDirective(*II,Target,IgnoreDeviceResident))
            return A;

    ArgVector &Args = Target->getVarDecl()->getOpenAccArgs();
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        ClauseInfo *ParentCI = A->getParent()->getAsClause();
        assert(ParentCI);
        if (IgnoreDeviceResident && ParentCI->getKind() == CK_DEVICE_RESIDENT)
            continue;
        if (A->Contains(Target))
            return A;
    }

    return 0;
}
/////////////////////////////////////////////////////////////////////////////

Arg*
RegionStack::FindMatchingPrivateOrFirstprivateInClause(ClauseInfo *CI, Arg *Target) {
    if (!CI->isPrivateClause())
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
RegionStack::FindMatchingPrivateOrFirstprivateInDirective(DirectiveInfo *DI, Arg *Target) {
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II)
        if (Arg *A = FindMatchingPrivateOrFirstprivateInClause(*II,Target))
            return A;
    return 0;
}

Arg*
RegionStack::FindMatchingPrivateOrFirstprivateInRegionStack(Arg *Target) {
    //return the most recent Arg

    for (reverse_iterator II(rbegin()), EE(rend()); II != EE; ++II)
        if (Arg *A = FindMatchingPrivateOrFirstprivateInDirective(*II,Target))
            return A;
    return 0;
}

Arg*
RegionStack::FindReductionInClause(ClauseInfo *CI, Arg *Target) {
    if (CI->getKind() != CK_REDUCTION)
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
RegionStack::FindReductionInDirective(DirectiveInfo *DI, Arg *Target) {
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II)
        if (Arg *A = FindReductionInClause(*II,Target))
            return A;
    return 0;
}

Arg*
RegionStack::FindReductionInRegionStack(Arg *Target) {
    //return the most recent Arg

    for (reverse_iterator II(rbegin()), EE(rend()); II != EE; ++II)
        if (Arg *A = FindReductionInDirective(*II,Target))
            return A;
    return 0;
}

bool
OpenACC::WarnOnVisibleCopyFromDeclareDirective(ArgVector &Args) {
    //return true on error

    bool status(false);
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (isa<VarArg>(A) || isa<ArrayArg>(A)) {
            ArgVector &Args = A->getVarDecl()->getOpenAccArgs();
            for (ArgVector::iterator RR = Args.begin(), SS = Args.end(); RR != SS; ++RR) {
                Arg *PrevArg = *RR;
                if (A->Matches(PrevArg)) {
                    S.Diag(A->getStartLocation(),diag::err_pragma_acc_has_visible_copy);
                    S.Diag(PrevArg->getStartLocation(),diag::note_pragma_acc_visible_copy_location);
                    status = true;
                }
            }
        }
    }
    return status;
}

bool
OpenACC::WarnOnMissingVisibleCopy(ArgVector &Args, bool IgnoreDeviceResident, DirectiveInfo *ExtraDI) {
    //return true on error

    bool status(false);

    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        ClauseInfo *CI = A->getParent()->getAsClause();
        if (ExtraDI &&
            RStack.FindVisibleCopyInDirective(ExtraDI,A,IgnoreDeviceResident))
            continue;  //do nothing
        else if (RStack.FindVisibleCopyInRegionStack(A,IgnoreDeviceResident))
            continue;  //do nothing
        else if (CI && CI->isPrivateClause()) {
            //FIXME: this is a hack!, do some proper checking kid

            assert(isa<ArraySubscriptExpr>(A->getExpr()));

            DirectiveInfo TmpDI(DK_DECLARE,SourceLocation());
            ClauseInfo TmpCI(CK_DEVICE_RESIDENT,&TmpDI);

            ASTContext *Context = &S.getASTContext();
            Expr *BaseExpr = A->getExpr();
            bool Next = false;
            while (ArraySubscriptExpr *ASE =
                   dyn_cast<ArraySubscriptExpr>(BaseExpr)) {
                //if (ASE->getIdx()->IgnoreParenCasts()
                //    ->isIntegerConstantExpr(*Context))
                //    break;
                BaseExpr = ASE->getBase()->IgnoreParenCasts();

                Arg *Target = 0;  //shadow the function global Target
                if (Context->getAsArrayType(BaseExpr->getType()))
                    Target = new ArrayArg(&TmpCI,BaseExpr,Context);
                else if (isa<MemberExpr>(BaseExpr))
                    Target = new VarArg(&TmpCI,BaseExpr,Context);
                else if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(BaseExpr))
                    if (isa<VarDecl>(DeclRef->getDecl()))
                        Target = new VarArg(&TmpCI,BaseExpr,Context);
                assert(Target && "null Target");

                if (ExtraDI &&
                    RStack.FindVisibleCopyInDirective(ExtraDI,Target,
                                                      IgnoreDeviceResident)) {
                    delete Target;
                    Next = true;
                    break;
                }
                else if (RStack.FindVisibleCopyInRegionStack(Target,
                                                           IgnoreDeviceResident)) {
                    delete Target;
                    Next = true;
                    break;
                }
                delete Target;
            }
            if (Next)
                continue;
        }

        SourceLocation Loc = A->getStartLocation();
        S.Diag(Loc,diag::err_pragma_acc_missing_visible_copy);
        //do not return immediately, find all VarDecls without visible copy
        status = true;
    }
    return status;
}

bool
OpenACC::WarnOnInvalidLists(DirectiveInfo *DI) {
    //return true on error

    ArgVector TmpCopy;
    ArgVector TmpCreateOrPresent;
    ArgVector TmpDeviceptr;
    ArgVector TmpPrivate;
    ArgVector TmpReduction;
    ArgVector TmpOther;

    bool status(false);
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator
             II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseInfo *CI = *II;
        ArgVector &Args = CI->getArgs();
        //ignore args in reduction clause, we check them independently
        if (!CI->hasArgList())
            continue;

        //perform separate check for private/firstprivate, reduction,
        //copy, create or present clauses
            ArgVector *Tmp = 0;
            if (CI->isPrivateClause())
                Tmp = &TmpPrivate;
            else if (CI->getKind() == CK_REDUCTION)
                Tmp = &TmpReduction;
        else if (CI->isCopyClause())
            Tmp = &TmpCopy;
        else if (CI->isCreateOrPresentClause())
            Tmp = &TmpCreateOrPresent;
        else if (CI->getKind() == CK_DEVICEPTR)
            Tmp = &TmpDeviceptr;
            else
            Tmp = &TmpOther;

            if (WarnOnDuplicatesInList(*Tmp,Args))
                status = true;
        //extra checks
        if (CI->getKind() == CK_DEVICEPTR) {
            if (WarnOnDuplicatesInList(TmpCopy,Args))
                status = true;
            if (WarnOnDuplicatesInList(TmpCreateOrPresent,Args))
                status = true;
            if (WarnOnDuplicatesInList(TmpOther,Args))
                status = true;
        }

        if (CI->isDataClause() && DI->getKind() != DK_DECLARE)
            if (WarnOnVisibleCopyFromDeclareDirective(Args))
                status = true;
        if (CI->isPrivateClause() && DI->getKind() != DK_LOOP) {
            if (WarnOnMissingVisibleCopy(Args,/*IgnoreDeviceResident=*/false,DI))
                status = true;
        }
    }
    return status;
}

bool
OpenACC::WarnOnArgKind(ArgVector &Args, ArgKind AK) {
    //return true on error

    bool status(false);
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        if (A->getKind() == AK) {
            S.Diag(A->getStartLocation(),diag::err_pragma_acc_illegal_arg_kind)
                << A->getKindAsString()
                << "list";
            status = true;
        }
    }
    return status;
}

bool
OpenACC::WarnOnInvalidReductionList(ArgVector &Args) {
    //return true on error

    //Supported data types are the numerical data types in C and C++
    //(int, float, double, complex)

    //the first element sets the data type of the reduction
    //we have at least one element, see ParsePragmaOpenACC.cpp:ParseArgList()
    ArgVector::iterator II = Args.begin();
    ArgVector::iterator EE = Args.end();

    Arg *A = *II;
    const QualType RQTy = A->getExpr()->getType();
    const Type *RTy = RQTy.getTypePtr();

    if (!RTy->isArithmeticType() && !RTy->isAnyComplexType()) {
        S.Diag(A->getStartLocation(),diag::err_pragma_acc_reduction_invalid_datatype);
        return true;
    }

    //S.Diag(A->getStartLocation(),diag::note_pragma_acc_reduction_datatype_mismatch)
    //    << RQTy.getAsString();

    bool status(false);
    for (++II; II != EE; ++II) {
        Arg *A = *II;
        const QualType QTy = A->getExpr()->getType();
        const Type *Ty = QTy.getTypePtr();
        if (!Ty->isArithmeticType() && !Ty->isAnyComplexType()) {
            S.Diag(A->getStartLocation(),diag::err_pragma_acc_reduction_invalid_datatype);
            status = true;
        }
        if (RTy != Ty) {
            S.Diag(A->getStartLocation(),diag::err_pragma_acc_reduction_datatype_mismatch)
                << RQTy.getAsString() << QTy.getAsString();
            status = true;
        }
    }
    return status;
}

bool
OpenACC::SetVisibleCopy(ArgVector &Args, ClauseInfo *CI) {
    //return true on error

    if (!CI->hasArgList())
        return false;

    bool status(false);
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        SourceLocation Loc = A->getStartLocation();
        //do not set visible copy for array elements or subarrays
        if (isa<VarArg>(A) || isa<ArrayArg>(A)) {
            if (Arg *PrevArg = RStack.FindVisibleCopyInRegionStack(A)) {
                SourceLocation PrevLoc = PrevArg->getStartLocation();
                S.Diag(Loc,diag::err_pragma_acc_redeclared_vardecl);
                S.Diag(PrevLoc,diag::note_pragma_acc_visible_copy_location);
                status = true;
            }
            else
                A->getVarDecl()->addOpenAccArg(A);
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
OpenACC::MarkArgsWithVisibleCopy(ClauseList &CList) {
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
    //if (empty())
        //no regions yet
    //    return false;

    bool isInvalid(false);
    DirectiveKind DK = DI->getKind();
    if (DI->isComputeDirective() || DI->isCombinedDirective()) {
        if (InComputeRegion())
            isInvalid = true;
    }
    else if (DK == DK_LOOP) {
        //allow nested loop directives
        //if (InComputeRegion() || InLoopRegion())
        if (InComputeRegion())
            ; //valid
        else
            isInvalid = true;
    }
    else if (DI->isExecutableDirective() || DI->getKind() == DK_DECLARE) {
        //allow them only in host code
        if (InLoopRegion() || InRegion(DK_PARALLEL))
            isInvalid = true;
    }
    else if (DI->getKind() == DK_CACHE) {
        //allow them only in device code

        //FIXME: not tested, quick & dirty hack
        //consider a kernel directive with a for loop substmt
        //we support this case therefore we must recognize it as valid here
        //if (!InLoopRegion() && !InRegion(DK_PARALLEL))
        if (!InLoopRegion() && !InComputeRegion())
            isInvalid = true;
    }
    else if (DI->isDataDirective()) {
        //allow them only in host code
        if (InLoopRegion() || InRegion(DK_PARALLEL))
            isInvalid = true;
    }
    else {
        llvm_unreachable("Unhandled case of invalid nesting");
    }

    return isInvalid;
}

void
RegionStack::EnterRegion(DirectiveInfo *EnterDI) {
    assert(EnterDI && "Null EnterDI");

    //ignore non region directives
    if (EnterDI->isExecutableOrCacheOrDeclareDirective())
        return;

    push_back(EnterDI);
}

void
RegionStack::ExitRegion(DirectiveInfo *ExpectedDI) {
    assert(ExpectedDI && "Null ExpectedDI");

    //ignore non region directives
    if (ExpectedDI->isExecutableOrCacheOrDeclareDirective())
        return;

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
RegionStack::InComputeRegion() const {
    for (const_reverse_iterator II(rbegin()), EE(rend()); II != EE; ++II) {
        DirectiveInfo *DI = *II;
        if (DI->isComputeDirective() || DI->isCombinedDirective())
            return true;
    }
    return false;
}

bool
RegionStack::InLoopRegion() const {
    for (const_reverse_iterator II(rbegin()), EE(rend()); II != EE; ++II) {
        DirectiveInfo *DI = *II;
        if (DI->isStartOfLoopRegion())
            return true;
    }
    return false;
}

bool
RegionStack::CurrentRegionIs(DirectiveKind DK) const {
    if (empty())
        return false;
    if (back()->getKind() == DK)
        return true;
    return false;
}

DirectiveInfo*
RegionStack::getTopComputeOrCombinedRegion() const {
    for (const_reverse_iterator II(rbegin()), EE(rend()); II != EE; ++II) {
        DirectiveInfo *DI = *II;
        if (DI->isComputeDirective() || DI->isCombinedDirective())
            return DI;
    }
    return 0;
}

Arg*
RegionStack::FindBufferObjectInClause(ClauseInfo *CI, Arg *Target) {
    if (CI->getKind() != CK_CREATE)
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

    ArgVector &Args = Target->getVarDecl()->getOpenAccArgs();
    for (ArgVector::iterator II = Args.begin(), EE = Args.end(); II != EE; ++II) {
        Arg *A = *II;
        ClauseInfo *ParentCI = A->getParent()->getAsClause();
        assert(ParentCI);
        (void)ParentCI;
        if (A->Matches(Target))
            return A;
    }

    for (ArgVector::reverse_iterator II = InterRegionMemBuffers.rbegin(),
             EE = InterRegionMemBuffers.rend(); II != EE; ++II) {
        Arg *A = *II;

        ClauseInfo *ParentCI = A->getParent()->getAsClause();
        assert(ParentCI);
        (void)ParentCI;
        assert(ParentCI && ParentCI->getKind() == CK_CREATE);
        if (A->Matches(Target))
            return A;
    }

    //if we reach here the buffer creation is implicit
    //(no 'create' Clause for the Target)

    return 0;
}
