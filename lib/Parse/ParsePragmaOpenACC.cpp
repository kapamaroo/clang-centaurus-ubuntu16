/// Parse OpenACC Directives

#include "clang/Basic/OpenACC.h"
#include "clang/AST/ASTContext.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "ParsePragma.h"
#include "clang/Sema/SemaOpenACC.h"

using namespace clang;
using namespace openacc;

#define BITMASK(x) ((unsigned int)1 << x)
const unsigned DirectiveInfo::ValidDirective[DK_END] = {
    //parallel
    BITMASK(CK_IF) |
        BITMASK(CK_ASYNC) |
        BITMASK(CK_NUM_GANGS) |
        BITMASK(CK_NUM_WORKERS) |
        BITMASK(CK_VECTOR_LENGTH) |
        BITMASK(CK_REDUCTION) |
        BITMASK(CK_COPY) |
        BITMASK(CK_COPYIN) |
        BITMASK(CK_COPYOUT) |
        BITMASK(CK_CREATE) |
        BITMASK(CK_PRESENT) |
        BITMASK(CK_PCOPY) |
        BITMASK(CK_PCOPYIN) |
        BITMASK(CK_PCOPYOUT) |
        BITMASK(CK_PCREATE) |
        BITMASK(CK_DEVICEPTR) |
        BITMASK(CK_PRIVATE) |
        BITMASK(CK_FIRSTPRIVATE),

        //parallel loop

        //from parallel
        BITMASK(CK_IF) |
        BITMASK(CK_ASYNC) |
        BITMASK(CK_NUM_GANGS) |
        BITMASK(CK_NUM_WORKERS) |
        BITMASK(CK_VECTOR_LENGTH) |
        BITMASK(CK_REDUCTION) |
        BITMASK(CK_COPY) |
        BITMASK(CK_COPYIN) |
        BITMASK(CK_COPYOUT) |
        BITMASK(CK_CREATE) |
        BITMASK(CK_PRESENT) |
        BITMASK(CK_PCOPY) |
        BITMASK(CK_PCOPYIN) |
        BITMASK(CK_PCOPYOUT) |
        BITMASK(CK_PCREATE) |
        BITMASK(CK_DEVICEPTR) |
        BITMASK(CK_PRIVATE) |
        BITMASK(CK_FIRSTPRIVATE) |

        //from loop
        BITMASK(CK_COLLAPSE) |
        BITMASK(CK_GANG) |
        BITMASK(CK_WORKER) |
        BITMASK(CK_VECTOR) |
        BITMASK(CK_SEQ) |
        BITMASK(CK_INDEPENDENT) |
        BITMASK(CK_PRIVATE) |
        BITMASK(CK_REDUCTION),

        //kernels
        BITMASK(CK_IF) |
        BITMASK(CK_ASYNC) |
        BITMASK(CK_COPY) |
        BITMASK(CK_COPYIN) |
        BITMASK(CK_COPYOUT) |
        BITMASK(CK_CREATE) |
        BITMASK(CK_PRESENT) |
        BITMASK(CK_PCOPY) |
        BITMASK(CK_PCOPYIN) |
        BITMASK(CK_PCOPYOUT) |
        BITMASK(CK_PCREATE) |
        BITMASK(CK_DEVICEPTR),

        //kernels loop

        //from kernels
        BITMASK(CK_IF) |
        BITMASK(CK_ASYNC) |
        BITMASK(CK_COPY) |
        BITMASK(CK_COPYIN) |
        BITMASK(CK_COPYOUT) |
        BITMASK(CK_CREATE) |
        BITMASK(CK_PRESENT) |
        BITMASK(CK_PCOPY) |
        BITMASK(CK_PCOPYIN) |
        BITMASK(CK_PCOPYOUT) |
        BITMASK(CK_PCREATE) |
        BITMASK(CK_DEVICEPTR) |

        //from loop
        BITMASK(CK_COLLAPSE) |
        BITMASK(CK_GANG) |
        BITMASK(CK_WORKER) |
        BITMASK(CK_VECTOR) |
        BITMASK(CK_SEQ) |
        BITMASK(CK_INDEPENDENT) |
        BITMASK(CK_PRIVATE) |
        BITMASK(CK_REDUCTION),

        //data
        BITMASK(CK_IF) |
        BITMASK(CK_COPY) |
        BITMASK(CK_COPYIN) |
        BITMASK(CK_COPYOUT) |
        BITMASK(CK_CREATE) |
        BITMASK(CK_PRESENT) |
        BITMASK(CK_PCOPY) |
        BITMASK(CK_PCOPYIN) |
        BITMASK(CK_PCOPYOUT) |
        BITMASK(CK_PCREATE) |
        BITMASK(CK_DEVICEPTR),

        //host data
        BITMASK(CK_USE_DEVICE),

        //loop
        BITMASK(CK_COLLAPSE) |
        BITMASK(CK_GANG) |
        BITMASK(CK_WORKER) |
        BITMASK(CK_VECTOR) |
        BITMASK(CK_SEQ) |
        BITMASK(CK_INDEPENDENT) |
        BITMASK(CK_PRIVATE) |
        BITMASK(CK_REDUCTION),

        //cache
        // no clauses allowed
        0,

        //declare
        BITMASK(CK_COPY) |
        BITMASK(CK_COPYIN) |
        BITMASK(CK_COPYOUT) |
        BITMASK(CK_CREATE) |
        BITMASK(CK_PRESENT) |
        BITMASK(CK_PCOPY) |
        BITMASK(CK_PCOPYIN) |
        BITMASK(CK_PCOPYOUT) |
        BITMASK(CK_PCREATE) |
        BITMASK(CK_DEVICEPTR) |
        BITMASK(CK_DEVICE_RESIDENT),

        //update
        BITMASK(CK_HOST) |
        BITMASK(CK_DEVICE) |
        BITMASK(CK_IF) |
        BITMASK(CK_ASYNC),

        //wait
        // no clauses allowed
        0
        };

const std::string DirectiveInfo::Name[DK_END] = {
    "parallel",
    "parallel loop",
    "kernels",
    "kernels loop",
    "data",
    "host_data",
    "loop",
    "cache",
    "declare",
    "update",
    "wait"
};

const std::string ClauseInfo::Name[CK_END] = {
    "if",
    "async",
    "num_gangs",
    "num_workers",
    "vector_length",
    "reduction",
    "copy",
    "copyin",
    "copyout",
    "create",
    "present",
    "pcopy",
    "pcopyin",
    "pcopyout",
    "pcreate",
    "deviceptr",
    "private",
    "firstprivate",
    "use_device",
    "collapse",
    "gang",
    "worker",
    "vector",
    "seq",
    "independent",
    "device_resident",
    "host",
    "device"
};

static DirectiveInfo*
ActOnNewOpenAccDirective(DirectiveKind DK, SourceLocation StartLoc,
                         ASTContext &Context) {
    DirectiveInfo *DI = new (Context) DirectiveInfo(DK,StartLoc);
    assert(DI);
    AccStmt *ACC = new (Context) AccStmt(DI);
    assert(ACC);
    DI->setAccStmt(ACC);
    return ACC->getDirective();
}

static ClauseInfo*
ActOnNewOpenAccClause(ClauseKind CK, SourceLocation StartLoc,
                      SourceLocation EndLoc, DirectiveInfo *DI,
                      ASTContext &Context) {
    ClauseInfo *CI = new (Context) ClauseInfo(CK,StartLoc,EndLoc,DI);
    return CI;
}

ClauseInfo*
CommonInfo::getAsClause() {
    if (!isClauseInfo)
        return 0;
    ClauseInfo *CI = reinterpret_cast<ClauseInfo*>(this);
    return CI;
}

DirectiveInfo*
CommonInfo::getAsDirective() {
    if (isClauseInfo)
        return 0;
    DirectiveInfo *DI = reinterpret_cast<DirectiveInfo*>(this);
    return DI;
}

std::string Arg::getPrettyArg(const PrintingPolicy &Policy) const {
    std::string StrExpr;
    llvm::raw_string_ostream OS(StrExpr);

    if (const SubArrayArg *SA = dyn_cast<SubArrayArg>(this)) {
        ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(E);
        ASE->getBase()->printPretty(OS,/*Helper=*/0,Policy,/*Indentation=*/0);
        OS << "[";
        ASE->getIdx()->printPretty(OS,/*Helper=*/0,Policy,/*Indentation=*/0);
        OS << ":";
        SA->getLength()->printPretty(OS,/*Helper=*/0,Policy,/*Indentation=*/0);
        OS << "]";
        return OS.str();  //flush
    }

    //special case: implementation default values do not have an expression
    if (const ConstArg *CA = dyn_cast<ConstArg>(this)) {
        if (CA->IsImplDefault()) {
            assert(!E && "unexpected expression in ImplDefault ConstArg ICE");
            ICE.print(OS,ICE.isSigned());
            return OS.str();  //flush
        }
    }

    //common case
    E->printPretty(OS,/*Helper=*/0,Policy,/*Indentation=*/0);
    return OS.str();  //flush
}

static std::string printArgList(const ArgVector &Args, const PrintingPolicy &Policy) {
    std::string StrList;
    for (ArgVector::const_iterator IA(Args.begin()), EA(Args.end());
         IA != EA; ++IA) {
        if (IA != Args.begin())
            StrList += ", ";
        Arg *A = *IA;
        StrList += A->getPrettyArg(Policy);
    }
    return StrList;
}

std::string ClauseInfo::getPrettyClause(const PrintingPolicy &Policy) const {
    std::string StrCI(getAsString());
    if (!hasArgs())
        return StrCI;

    if (CK == CK_VECTOR) {
        const CommonInfo *CC = reinterpret_cast<const CommonInfo*>(this);
        if (ConstArg *CA = dyn_cast<ConstArg>(CC->getArg()))
            if (CA->IsImplDefault())
                return getAsString();
    }

    StrCI += "(";
    if (CK == CK_REDUCTION)
        StrCI += printReductionOperator() + ":";
    StrCI += printArgList(getArgs(),Policy) + ")";
    return StrCI;
}

std::string DirectiveInfo::getPrettyDirective(const PrintingPolicy &Policy, const bool IgnoreImplDefaults) const {
    std::string StrDI("\n#pragma acc ");
    StrDI += getAsString();
    StrDI += " ";

    if (DK == DK_CACHE || DK == DK_WAIT) {
        const CommonInfo *CC = reinterpret_cast<const CommonInfo*>(this);
        StrDI += "(" + printArgList(CC->getArgs(),Policy) + ")\n";
        return StrDI;
    }

    for (ClauseList::const_iterator II(CList.begin()), EE(CList.end());
         II != EE; ++II) {
        ClauseInfo *CI = *II;
        if (CI->isImplDefault() && IgnoreImplDefaults)
            continue;
        if (II != CList.begin())
            StrDI += ", ";
        StrDI += CI->getPrettyClause(Policy);
    }
    StrDI += "\n";
    return StrDI;
}

#define ACC_IF(Tok, Kind, PREFIX, str, STR) if (Tok.getIdentifierInfo()->isStr(#str)) { Kind = PREFIX ##_## STR; return true; }
#define ACC_ELSE_IF(Tok, Kind, PREFIX, str, STR) else ACC_IF(Tok, Kind, PREFIX, str, STR)

static bool isACCDirective(const Token &Tok, DirectiveKind &Kind) {
    ACC_IF(Tok,Kind,DK,parallel,PARALLEL)
    ACC_ELSE_IF(Tok,Kind,DK,kernels,KERNELS)
    ACC_ELSE_IF(Tok,Kind,DK,data,DATA)
    ACC_ELSE_IF(Tok,Kind,DK,host_data,HOST_DATA)
    ACC_ELSE_IF(Tok,Kind,DK,loop,LOOP)
    ACC_ELSE_IF(Tok,Kind,DK,cache,CACHE)
    ACC_ELSE_IF(Tok,Kind,DK,declare,DECLARE)
    ACC_ELSE_IF(Tok,Kind,DK,update,UPDATE)
    ACC_ELSE_IF(Tok,Kind,DK,wait,WAIT)
    return false;
}

static bool isACCClause(const Token &Tok, ClauseKind &Kind) {
    if (Tok.is(tok::kw_if)) {
        Kind = CK_IF;
        return true;
    }

    //if we are in C++ mode, 'private' is a keyword
    if (Tok.is(tok::kw_private)) {
        Kind = CK_PRIVATE;
        return true;
    }

    ACC_IF(Tok,Kind,CK,async,ASYNC)

    ACC_ELSE_IF(Tok,Kind,CK,num_gangs,NUM_GANGS)
    ACC_ELSE_IF(Tok,Kind,CK,num_workers,NUM_WORKERS)
    ACC_ELSE_IF(Tok,Kind,CK,vector_length,VECTOR_LENGTH)
    ACC_ELSE_IF(Tok,Kind,CK,reduction,REDUCTION)

    //recheck 'private' as identifier this time
    ACC_ELSE_IF(Tok,Kind,CK,private,PRIVATE)

    ACC_ELSE_IF(Tok,Kind,CK,copy,COPY)
    ACC_ELSE_IF(Tok,Kind,CK,copyin,COPYIN)
    ACC_ELSE_IF(Tok,Kind,CK,copyout,COPYOUT)
    ACC_ELSE_IF(Tok,Kind,CK,create,CREATE)
    ACC_ELSE_IF(Tok,Kind,CK,present,PRESENT)

    ACC_ELSE_IF(Tok,Kind,CK,pcopy,PCOPY)
    ACC_ELSE_IF(Tok,Kind,CK,pcopyin,PCOPYIN)
    ACC_ELSE_IF(Tok,Kind,CK,pcopyout,PCOPYOUT)
    ACC_ELSE_IF(Tok,Kind,CK,pcreate,PCREATE)

    ACC_ELSE_IF(Tok,Kind,CK,present_or_copy,PCOPY)
    ACC_ELSE_IF(Tok,Kind,CK,present_or_copyin,PCOPYIN)
    ACC_ELSE_IF(Tok,Kind,CK,present_or_copyout,PCOPYOUT)
    ACC_ELSE_IF(Tok,Kind,CK,present_or_create,PCREATE)

    ACC_ELSE_IF(Tok,Kind,CK,deviceptr,DEVICEPTR)
    ACC_ELSE_IF(Tok,Kind,CK,firstprivate,FIRSTPRIVATE)
    ACC_ELSE_IF(Tok,Kind,CK,use_device,USE_DEVICE)
    ACC_ELSE_IF(Tok,Kind,CK,collapse,COLLAPSE)

    ACC_ELSE_IF(Tok,Kind,CK,gang,GANG)
    ACC_ELSE_IF(Tok,Kind,CK,worker,WORKER)
    ACC_ELSE_IF(Tok,Kind,CK,vector,VECTOR)

    ACC_ELSE_IF(Tok,Kind,CK,seq,SEQ)
    ACC_ELSE_IF(Tok,Kind,CK,independent,INDEPENDENT)

    ACC_ELSE_IF(Tok,Kind,CK,device_resident,DEVICE_RESIDENT)
    ACC_ELSE_IF(Tok,Kind,CK,host,HOST)
    ACC_ELSE_IF(Tok,Kind,CK,device,DEVICE)

    return false;
}

static bool isReductionOperator(const Token &Tok, enum ReductionOperator &Kind) {
    if (Tok.is(tok::plus))          { Kind = ROP_PLUS;        return true;  }
    else if (Tok.is(tok::star))     { Kind = ROP_MULT;        return true;  }
    else if (Tok.is(tok::amp))      { Kind = ROP_BITWISE_AND; return true;  }
    else if (Tok.is(tok::pipe))     { Kind = ROP_BITWISE_OR;  return true;  }
    else if (Tok.is(tok::caret))    { Kind = ROP_BITWISE_XOR; return true;  }
    else if (Tok.is(tok::ampamp))   { Kind = ROP_LOGICAL_AND; return true;  }
    else if (Tok.is(tok::pipepipe)) { Kind = ROP_LOGICAL_OR;  return true;  }

    if (Tok.isNot(tok::identifier))
        return false;

    ACC_IF(Tok,Kind,ROP,max,MAX)
    ACC_ELSE_IF(Tok,Kind,ROP,min,MIN)

    return false;
}

static bool isDuplicate(ClauseList &CList, ClauseKind CK, SourceLocation &Loc) {
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseInfo *CI = *II;
        if (CI->getKind() == CK) {
            Loc = CI->getStartLocation();
            return true;
        }
    }

    return false;
}

static bool mustBeUnique(ClauseKind CK) {
    switch (CK) {
    case CK_IF:
    case CK_ASYNC:
    case CK_NUM_GANGS:
    case CK_NUM_WORKERS:
    case CK_VECTOR_LENGTH:
    case CK_COLLAPSE:
    case CK_GANG:
    case CK_WORKER:
    case CK_VECTOR:
    case CK_SEQ:
    case CK_INDEPENDENT:
        return true;
    default:
        return false;
    }
}

bool Parser::ParseArgScalarIntExpr(DirectiveKind DK, CommonInfo *Common) {
    ExprResult E = ParseExpression();

    if (!E.isUsable())
        return false;

    if (ClauseInfo *CI = Common->getAsClause()) {
        if (CI->getKind() == CK_IF)
            E = Actions.ActOnBooleanCondition(getCurScope(), E.get()->getExprLoc(), E.get());
    }

    if (!E.isUsable())
        return false;

    Arg *A = Actions.getACCInfo()->CreateArg(E.get(),Common);
    Common->setArg(A);

    return !Actions.getACCInfo()->WarnOnSubArrayArg(Common->getArgs());
}

bool Parser::ParseArgList(DirectiveKind DK, CommonInfo *Common) {
    // a list is a comma-separated list of
    //variable names,
    //array names,
    //subarrays with subscript ranges

    //a simplified version of ParseExpressionList()

    ArgVector &Args = Common->getArgs();

    bool pending_comma(false);
    Actions.getACCInfo()->AllowSubArrays();
    while (1) {
        ExprResult Expr = ParseCastExpression(false,false,NotTypeCast);

        if (!Expr.isUsable())
            return false;

        Arg *A = Actions.getACCInfo()->CreateArg(Expr.get(),Common);
        Args.push_back(A);

        pending_comma = false;
        if (Tok.is(tok::comma)) {
            ConsumeToken();
            pending_comma = true;
        }

        if (Tok.is(tok::r_paren))
            break;
    }
    Actions.getACCInfo()->ProhibitSubArrays();

    if (pending_comma)
        PP.Diag(Tok,diag::warn_pragma_acc_pending_comma);

    //ignore any construct with empty list
    if (Args.empty())
        return false;

    bool status = true;
    if (Actions.getACCInfo()->WarnOnConstArg(Args))
        status = false;
    if (Actions.getACCInfo()->WarnOnArgKind(Args,A_RawExpr))
        status = false;
    return status;
}

//Parse Clauses

bool
Parser::ParseClauseIf(DirectiveKind DK, ClauseInfo *CI) {
    //create two versions of the construct, one for the host
    //and one for the device

    //at most one if for each directive

    //nonzero means true, zero means false
    //if true, execute the device version, else the host version

    CI->getParentDirective()->setIfClause(CI);
    return ParseArgScalarIntExpr(DK,CI);
}

bool
Parser::ParseClauseAsync(DirectiveKind DK, ClauseInfo *CI) {
    //optional arguments

    /// The update directive is executable. It must not appear in place of the statement
    /// following an if, while, do, switch, or label in C or C++, or in place of the statement
    /// following a logical if in Fortran.

    return ParseArgScalarIntExpr(DK,CI);
}

bool
Parser::ParseClauseNum_gangs(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgScalarIntExpr(DK,CI);
}

bool
Parser::ParseClauseNum_workers(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgScalarIntExpr(DK,CI);
}

bool
Parser::ParseClauseVector_length(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgScalarIntExpr(DK,CI);
}

bool
Parser::ParseClauseReduction(DirectiveKind DK, ClauseInfo *CI) {
    //reduction ( operator : list )

    enum ReductionOperator Kind;

    if (!isReductionOperator(Tok,Kind)) {
        PP.Diag(Tok,diag::err_pragma_acc_expected_reduction_operator);
        //consume the bad token to avoid false errors afterwards
        return false;
    }

    CI->setReductionOperator(Kind);

    ConsumeAnyToken();

    if (Tok.isNot(tok::colon)) {
        PP.Diag(Tok,diag::err_pragma_acc_expected_colon);
        return false;
    }
    ConsumeToken();

    return ParseArgList(DK,CI);
}

bool
Parser::ParseClauseCopy(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClauseCopyin(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClauseCopyout(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClauseCreate(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClausePresent(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClausePcopy(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClausePcopyin(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClausePcopyout(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClausePcreate(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClauseDeviceptr(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClausePrivate(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClauseFirstprivate(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClauseUse_device(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClauseCollapse(DirectiveKind DK, ClauseInfo *CI) {
    //collapse( n )
    // constant positive integer expression

    return ParseArgScalarIntExpr(DK,CI);
}

bool
Parser::ParseClauseGang(DirectiveKind DK, ClauseInfo *CI) {
    //if DK == parallel then no argument is allowed
    //arguments allowed only if DK == kernels

    return ParseArgScalarIntExpr(DK,CI);
}

bool
Parser::ParseClauseWorker(DirectiveKind DK, ClauseInfo *CI) {
    //if DK == parallel then no argument is allowed
    //arguments allowed only if DK == kernels
    return ParseArgScalarIntExpr(DK,CI);
}

bool
Parser::ParseClauseVector(DirectiveKind DK, ClauseInfo *CI) {
    //SIMD mode
    //optional argument

    return ParseArgScalarIntExpr(DK,CI);
}

bool
Parser::ParseClauseSeq(DirectiveKind DK, ClauseInfo *CI) {
    //no arguments
    return true;
}

bool
Parser::ParseClauseIndependent(DirectiveKind DK, ClauseInfo *CI) {
    //no arguments
    return true;
}

bool
Parser::ParseClauseDevice_resident(DirectiveKind DK, ClauseInfo *CI) {
    //variables in list must be file static or local to a function
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClauseHost(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

bool
Parser::ParseClauseDevice(DirectiveKind DK, ClauseInfo *CI) {
    return ParseArgList(DK,CI);
}

//Parse Directives

bool
Parser::ParseDirectiveParallel(DirectiveInfo *DI) {
    return ParseClauses(DI);
}

bool
Parser::ParseDirectiveParallelLoop(DirectiveInfo *DI) {
    return ParseClauses(DI);
}

bool
Parser::ParseDirectiveKernels(DirectiveInfo *DI) {
    return ParseClauses(DI);
}

bool
Parser::ParseDirectiveKernelsLoop(DirectiveInfo *DI) {
    return ParseClauses(DI);
}

bool
Parser::ParseDirectiveData(DirectiveInfo *DI) {
    return ParseClauses(DI);
}

bool
Parser::ParseDirectiveHostData(DirectiveInfo *DI) {
    return ParseClauses(DI);
}

bool
Parser::ParseDirectiveLoop(DirectiveInfo *DI) {
    return ParseClauses(DI);
}

bool
Parser::ParseDirectiveCache(DirectiveInfo *DI) {
    //entries in list must be single array elements or simple subarray
    //
    //e.g. arr[lower:length]
    //
    //lower bound is constant, loop invariant or the
    //for loop index plus/minus a constant or loop invariant
    //
    //length is constant

    //we expect a left paren '(' here
    if (Tok.isNot(tok::l_paren)) {
        PP.Diag(Tok,diag::warn_pragma_expected_lparen) << "acc";
        return false;
    }
    ConsumeParen();

    if (!ParseArgList(DI->getKind(),DI))
        return false;

    //expect right ')'
    if (Tok.isNot(tok::r_paren)) {
        PP.Diag(Tok,diag::warn_pragma_expected_rparen) << "acc";
        return false;
    }
    ConsumeParen();

    return true;
}

bool
Parser::ParseDirectiveDeclare(DirectiveInfo *DI) {
    //following an variable declaration
    return ParseClauses(DI);
}

bool
Parser::ParseDirectiveUpdate(DirectiveInfo *DI) {
    return ParseClauses(DI);
}

bool
Parser::ParseDirectiveWait(DirectiveInfo *DI) {
    //optional arguments
    //wait for all async

    //we expect a left paren '(' here
    if (Tok.isNot(tok::l_paren)) {
        PP.Diag(Tok,diag::warn_pragma_expected_lparen) << "acc";
        return false;
    }
    ConsumeParen();

    if (!ParseArgScalarIntExpr(DI->getKind(),DI))
        return false;

    //expect right ')'
    if (Tok.isNot(tok::r_paren)) {
        PP.Diag(Tok,diag::warn_pragma_expected_rparen) << "acc";
        return false;
    }
    ConsumeParen();

    return true;
}

void
Parser::MaybeParseCombinedDirective(DirectiveKind &Kind) {
    if (NextToken().isNot(tok::identifier))
        return;

    if (!NextToken().getIdentifierInfo()->isStr("loop"))
        return;

    if (Kind == DK_PARALLEL) {
        Kind = DK_PARALLEL_LOOP;
        ConsumeAnyToken();
    }
    else if (Kind == DK_KERNELS) {
        Kind = DK_KERNELS_LOOP;
        ConsumeAnyToken();
    }
}

void Parser::HandlePragmaOpenACC() {
    //FIXME: better use a helper function to clean up the eod

//http://www.parashift.com/c++-faq-lite/pointers-to-members.html
#define PARSER_CALL(method) ((*this).*(method))

    using namespace openacc;

    assert(Tok.is(tok::annot_pragma_acc));

    //SkipUntil() does not work always here. For example if we abort after
    //consuming a left '(', SkipUntil() stops at the right ')' if any, even out
    //of the directive, which means it skips valid code. We always want to skip
    //until eod (and also consume it).

    //There is a similar problem with DiscardUntilEndOfDirective() in case we
    //find an error and we just parsed the eod (current token is eod). In this
    //case DiscardUntilEndOfDirective() tries to find the next eod in the code
    //discarding any code between. If it cannot find any other eod, it hits an
    //assertion and exits.

    //Parse Directive
    SourceLocation DirectiveStartLoc = ConsumeAnyToken();

    if (Tok.is(tok::eod)) {
        PP.Diag(Tok,diag::err_pragma_acc_expected_directive);
        ConsumeAnyToken();  //consume eod
        return;
    }

    //set it if Tok is a valid Directive
    DirectiveKind DK;
    if (Tok.isNot(tok::identifier) || !isACCDirective(Tok, DK)) {
        PP.Diag(Tok,diag::err_pragma_acc_unknown_directive);
        PP.DiscardUntilEndOfDirective();
        ConsumeAnyToken();  //consume eod
        return;
    }

    //check for combined directives
    MaybeParseCombinedDirective(DK);

    //Parse the rest directive
    //keep any bad directive to avoid false errors afterwards

    //get the first clause if any
    ConsumeAnyToken();

    DirectiveInfo *DI =
        ActOnNewOpenAccDirective(DK,DirectiveStartLoc,Actions.getASTContext());

    SourceLocation DirectiveEndLoc;
    if (DI->hasOptionalClauses() && Tok.is(tok::eod))
        //early success!
        DirectiveEndLoc = ConsumeToken();  //consume eod
    else if (!PARSER_CALL(ParseDirective[DK])(DI)) {
        if (Tok.isNot(tok::eod)) {
            //we may stopped at any place
            //the Preprocessor does not update the Parser's Token
            PP.DiscardUntilEndOfDirective();
            PP.Lex(Tok);  //consume eod
            DirectiveEndLoc = Tok.getLocation();
        }
        else
            DirectiveEndLoc = ConsumeToken();  //consume eod
    }
    else if (Tok.is(tok::eod))
        DirectiveEndLoc = ConsumeToken();  //consume eod
    else
        assert(0 && "unknown Parser state");

    //we consumed the eod in each case

    DI->setEndLocation(DirectiveEndLoc);
    Actions.getACCInfo()->isValidDirectiveWrapper(DI);
}

bool
Parser::ParseClauseWrapper(DirectiveInfo *DI) {
//http://www.parashift.com/c++-faq-lite/pointers-to-members.html
#define PARSER_CALL(method) ((*this).*(method))

    //Consumes possible tok::comma at the end of clause
    //current token points to clause identifier

    const DirectiveKind DK = DI->getKind();
    ClauseList &CList = DI->getClauseList();

    //early fail
    if (Tok.isNot(tok::identifier) && Tok.isNot(tok::kw_if) && Tok.isNot(tok::kw_private)) {
        PP.Diag(Tok,diag::err_pragma_acc_unknown_clause);
        //consume bad token to avoid false errors afterwards
        ConsumeAnyToken();
        return false;
    }

    //find ClauseKind
    ClauseKind CK;
    if (!isACCClause(Tok,CK)) {
        PP.Diag(Tok,diag::err_pragma_acc_unknown_clause);
        return false;
    }

    if (!DI->hasValidClause(CK)) {
        PP.Diag(Tok,diag::err_pragma_acc_invalid_clause);
        return false;
    }

    SourceLocation Loc;
    if (mustBeUnique(CK) && isDuplicate(CList,CK,Loc)) {
        PP.Diag(Loc,diag::warn_pragma_acc_unique_clause);
        return false;
    }

    SourceLocation ClauseStartLoc = Tok.getLocation();
    int Offset = Tok.getIdentifierInfo()->getLength();
    SourceLocation ClauseEndLoc = ClauseStartLoc.getLocWithOffset(Offset);

    ClauseInfo *CI =
        ActOnNewOpenAccClause(CK,ClauseStartLoc,ClauseEndLoc,DI,
                              Actions.getASTContext());
    CList.push_back(CI);

    if (CI->hasNoArgs()) {
        //early success!
        //PP.Diag(ClauseStartLoc,diag::note_pragma_acc_found_clause);
        ConsumeToken();
        return true;
    }

    // Lex the left '('.
    ConsumeAnyToken();

    if (Tok.isNot(tok::l_paren)) {
        if (CI->hasOptionalArgs()) {
            //early success!
            //PP.Diag(ClauseStartLoc,diag::note_pragma_acc_found_clause);
            return true;
        }

        PP.Diag(Tok,diag::warn_pragma_expected_lparen) << "acc";
        return false;
    }

    ConsumeParen();

    if (!PARSER_CALL(ParseClause[CK])(DK,CI))
        return false;

    if (Tok.isNot(tok::r_paren)) {
        PP.Diag(Tok,diag::warn_pragma_expected_rparen) << "acc";
        return false;
    }
    ClauseEndLoc = ConsumeParen();
    CI->setEndLocation(ClauseEndLoc);

    //PP.Diag(ClauseStartLoc,diag::note_pragma_acc_found_clause);

    return true;
}

bool
Parser::ParseClauses(DirectiveInfo *DI) {
    bool pending_comma = false;
    bool found_clause = false;

    const DirectiveKind DK = DI->getKind();
    ClauseList &CList = DI->getClauseList();

    while (1) {
        if (Tok.is(tok::eod))
            break;

        if (!ParseClauseWrapper(DI))
            return false;

        if (!Actions.getACCInfo()->isValidClauseWrapper(DK,CList.back()))
            return false;

        pending_comma = false;
        if (Tok.is(tok::comma)) {
            ConsumeToken();
            pending_comma = true;
        }

        found_clause = true;
    }

    if (pending_comma)
        PP.Diag(Tok,diag::warn_pragma_acc_pending_comma);

#if 0
    if (!DI->hasOptionalClauses() && !found_clause) {
        PP.Diag(Tok,diag::err_pragma_acc_incomplete);
        return false;
    }
#else
    if (found_clause)
        return true;

    if (DI->hasOptionalClauses())
        return true;

    switch (DI->getKind()) {
    case DK_PARALLEL:
    case DK_PARALLEL_LOOP:
    case DK_KERNELS:
    case DK_KERNELS_LOOP:
        return true;
    case DK_DATA:
    case DK_HOST_DATA:
        return false;
    case DK_LOOP:
        return true;
    case DK_CACHE:
    case DK_DECLARE:
    case DK_UPDATE:
        return false;
    case DK_WAIT:
        return true;
    }
#endif

    return true;
}
