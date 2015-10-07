#include "clang/AST/ASTContext.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Sema/SemaOpenACC.h"
#include "clang/Sema/Lookup.h"

using namespace clang;
using namespace openacc;

#define BITMASK(x) ((unsigned int)1 << x)
const unsigned DirectiveInfo::ValidDirective[DK_END] = {
    //task
    BITMASK(CK_LABEL) |
        BITMASK(CK_TASKID) |
        BITMASK(CK_SIGNIFICANT) |
        BITMASK(CK_APPROXFUN) |
        BITMASK(CK_EVALFUN) |
        BITMASK(CK_ESTIMATION) |
        BITMASK(CK_BUFFER) |
        BITMASK(CK_LOCAL_BUFFER) |
        BITMASK(CK_IN) |
        BITMASK(CK_OUT) |
        BITMASK(CK_INOUT) |
        BITMASK(CK_DEVICE_IN) |
        BITMASK(CK_DEVICE_OUT) |
        BITMASK(CK_DEVICE_INOUT) |
        BITMASK(CK_WORKERS) |
        BITMASK(CK_GROUPS) |
        BITMASK(CK_BIND) |
        BITMASK(CK_BIND_APPROXIMATE) |
        BITMASK(CK_SUGGEST),

        //taskgroup
        BITMASK(CK_LABEL) |
        BITMASK(CK_ENERGY_JOULE) |
        BITMASK(CK_RATIO),

        //taskwait
        BITMASK(CK_LABEL) |
        BITMASK(CK_EVALFUN) |
        BITMASK(CK_ESTIMATION) |
        BITMASK(CK_ON),

        //subtask
        BITMASK(CK_APPROXFUN),
        };

const std::string clang::openacc::ExtensionName = "acl";

const std::string DirectiveInfo::Name[DK_END] = {
    "task",
    "taskgroup",
    "taskwait",
    "subtask",
};

const std::string ClauseInfo::Name[CK_END] = {
    "label",
    "taskid",
    "significant",
    "approxfun",
    "evalfun",
    "estimation",
    "buffer",
    "local_buffer",
    "in",
    "out",
    "inout",
    "device_in",
    "device_out",
    "device_inout",
    "on",
    "workers",
    "groups",
    "bind",
    "bind_approx",
    "suggest",
    "energy_joule",
    "ratio",
};

static std::string printArgList(const ArgVector &Args, const PrintingPolicy &Policy) {
    std::string StrList;
    for (ArgVector::const_iterator IA(Args.begin()), EA(Args.end());
         IA != EA; ++IA) {
        if (IA != Args.begin())
            StrList += ", ";
        Arg *A = *IA;
        StrList += A->getPrettyArg();
    }
    return StrList;
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

std::string Arg::getPrettyArg() const {
    const PrintingPolicy &Policy = Context->getPrintingPolicy();

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
    if (const RawExprArg *RE = dyn_cast<RawExprArg>(this)) {
        if (RE->IsImplDefault()) {
            assert(!E && "unexpected expression in ImplDefault ConstArg ICE");
            ICE.print(OS,ICE.isSigned());
            return OS.str();  //flush
        }
    }

    if (const FunctionArg *FA = dyn_cast<FunctionArg>(this))
        return FA->getFunctionDecl()->getName();

    //common case
    E->printPretty(OS,/*Helper=*/0,Policy,/*Indentation=*/0);
    return OS.str();  //flush
}

std::string ClauseInfo::getPrettyClause(const PrintingPolicy &Policy) const {
    if (!hasArgs())
        return getAsString();
    std::string StrCI(getAsString() + "(" + printArgList(getArgs(),Policy) + ")");
    return StrCI;
}

std::string DirectiveInfo::getPrettyDirective(const PrintingPolicy &Policy, const bool IgnoreImplDefaults) const {
    std::string StrCI;
    for (ClauseList::const_iterator II(CList.begin()), EE(CList.end());
         II != EE; ++II) {
        ClauseInfo *CI = *II;
        if (CI->isImplDefault() && IgnoreImplDefaults)
            continue;
        if (II != CList.begin())
            StrCI += ", ";
        StrCI += CI->getPrettyClause(Policy);
    }
    std::string StrDI("\n#pragma " + ExtensionName + " " + getAsString() + " " + StrCI + "\n");
    return StrDI;
}
