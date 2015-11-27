/// Parse Centaurus Directives

#include "clang/AST/ASTContext.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Sema/SemaCentaurus.h"
#include "clang/Sema/Lookup.h"

using namespace clang;
using namespace centaurus;

static DirectiveInfo*
ActOnNewCentaurusDirective(DirectiveKind DK, SourceLocation StartLoc,
                         ASTContext &Context) {
    DirectiveInfo *DI = new (Context) DirectiveInfo(DK,StartLoc);
    AclStmt *ACC = new (Context) AclStmt(DI);
    DI->setAclStmt(ACC);
    return ACC->getDirective();
}

static ClauseInfo*
ActOnNewCentaurusClause(ClauseKind CK, SourceLocation StartLoc,
                      SourceLocation EndLoc, DirectiveInfo *DI,
                      ASTContext &Context) {
    ClauseInfo *CI = new (Context) ClauseInfo(CK,StartLoc,EndLoc,DI);
    return CI;
}

static bool isACCDirective(const Token &Tok, DirectiveKind &Kind) {
    for (unsigned i=DK_START; i<DK_END; ++i) {
        enum DirectiveKind DK = (enum DirectiveKind)i;
        if (Tok.getIdentifierInfo()->getName().equals(DirectiveInfo::Name[DK])) {
            Kind = DK;
            return true;
        }
    }
    return false;
}

static bool isACCClause(const Token &Tok, ClauseKind &Kind) {
    for (unsigned i=CK_START; i<CK_END; ++i) {
        enum ClauseKind CK = (enum ClauseKind)i;
        if (Tok.getIdentifierInfo()->getName().equals(ClauseInfo::Name[CK])) {
            Kind = CK;
            return true;
        }
    }
    return false;
}

static bool isDuplicate(ClauseList &CList, ClauseKind CK, SourceLocation &Loc) {
    for (ClauseList::iterator II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseInfo *CI = *II;
        if (CI->getKind() == CK) {
            Loc = CI->getLocStart();
            return true;
        }
    }

    return false;
}

static bool mustBeUnique(ClauseKind CK) {
    switch (CK) {
    case CK_LABEL:
    case CK_TASKID:
    case CK_SIGNIFICANT:
    case CK_APPROXFUN:
    case CK_EVALFUN:
    case CK_ESTIMATION:
    case CK_WORKERS:
    case CK_GROUPS:
    case CK_BIND:
    case CK_BIND_APPROXIMATE:
    case CK_SUGGEST:
    case CK_ENERGY_JOULE:
    case CK_RATIO:
        return true;
    default:
        return false;
    }
}

bool Parser::ParseArgScalarIntExpr(DirectiveKind DK, CommonInfo *Common) {
    ExprResult E = ParseCastExpression(false,false,NotTypeCast);
    // ExprResult E = ParseExpression();
    if (!E.isUsable())
        return false;

    const Type *ETy = E.get()->getType().getTypePtr();
    if (!ETy->isIntegerType()) {
        PP.Diag(Common->getLocStart(),diag::note_pragma_acc_parser_test)
            << "expected expression of integer type";
        return false;
    }

    Arg *A = new (Actions.getASTContext()) RawExprArg(Common,E.get(),&Actions.getASTContext());
    Common->setArg(A);
    return !Actions.getACCInfo()->WarnOnSubArrayArg(Common->getArgs());
}

bool Parser::ParseArgDoubleExpr(DirectiveKind DK, CommonInfo *Common) {
    ExprResult E = ParseCastExpression(false,false,NotTypeCast);
    // ExprResult E = ParseExpression();
    if (!E.isUsable())
        return false;

    const Type *ETy = E.get()->getType().getTypePtr();
    if (!ETy->isFloatingType()) {
        PP.Diag(Common->getLocStart(),diag::note_pragma_acc_parser_test)
            << "expected expression of floating type";
        return false;
    }

    Arg *A = new (Actions.getASTContext()) RawExprArg(Common,E.get(),&Actions.getASTContext());
    Common->setArg(A);
    return !Actions.getACCInfo()->WarnOnSubArrayArg(Common->getArgs());
}

bool Parser::ParseArgList(DirectiveKind DK, CommonInfo *Common, bool AllowSubArrays) {
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

        if (!Expr.isUsable()) {
            Actions.getACCInfo()->ProhibitSubArrays();
            return false;
        }

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

    return true;
}

//Parse Clauses

bool
Parser::ParseClauseLabel(DirectiveKind DK, ClauseInfo *CI) {
    ExprResult _E = ParseExpression();
    if (!_E.isUsable())
        return false;

    Expr *E = _E.get();

    if (StringLiteral *SL = dyn_cast<StringLiteral>(E)) {
        //PP.Diag(Tok,diag::note_pragma_acc_parser_test) << "string-literal";
        //PP.Diag(Tok,diag::note_pragma_acc_parser_test) << SL->getString();
        Arg *A = new (Actions.getASTContext()) LabelArg(CI,SL,&Actions.getASTContext());
        CI->setArg(A);
        return true;
    }

    const QualType QTy = E->getType();
    const Type *Ty = E->getType().getTypePtr();

    bool Valid = false;
    if (const ConstantArrayType *CAT = Actions.getASTContext().getAsConstantArrayType(QTy)) {
        if (CAT->getElementType()->isCharType()) {
            Valid = true;
            //PP.Diag(Tok,diag::note_pragma_acc_parser_test) << "isConstantArrayType of CharType";
        }
    }
    else if (const ArrayType *AT = Actions.getASTContext().getAsArrayType(QTy)) {
        if (AT->getElementType()->isCharType()) {
            Valid = true;
            //PP.Diag(Tok,diag::note_pragma_acc_parser_test) << "isArrayType of CharType";
        }
    }
    else if (const PointerType *PT = Ty->getAs<PointerType>()) {
        if (PT->getPointeeType()->isCharType()) {
            Valid = true;
            //PP.Diag(Tok,diag::note_pragma_acc_parser_test) << "isPointerType to CharType";
        }
    }

    if (!Valid) {
        PP.Diag(Tok,diag::note_pragma_acc_parser_test) << "invalid argument type";
        return false;
    }

    Arg *A = new (Actions.getASTContext()) LabelArg(CI,E,&Actions.getASTContext());
    CI->setArg(A);

    return true;
}

bool
Parser::ParseClauseTaskid(DirectiveKind DK, ClauseInfo *CI) {
    ExprResult _E = ParseExpression();
    if (!_E.isUsable())
        return false;

    Expr *E = _E.get();

    if (StringLiteral *SL = dyn_cast<StringLiteral>(E)) {
        //PP.Diag(Tok,diag::note_pragma_acc_parser_test) << "string-literal";
        //PP.Diag(Tok,diag::note_pragma_acc_parser_test) << SL->getString();
        Arg *A = new (Actions.getASTContext()) LabelArg(CI,SL,&Actions.getASTContext());
        CI->setArg(A);
        return true;
    }

    const QualType QTy = E->getType();
    const Type *Ty = E->getType().getTypePtr();

    bool Valid = false;
    if (const ConstantArrayType *CAT = Actions.getASTContext().getAsConstantArrayType(QTy)) {
        if (CAT->getElementType()->isCharType()) {
            Valid = true;
            //PP.Diag(Tok,diag::note_pragma_acc_parser_test) << "isConstantArrayType of CharType";
        }
    }
    else if (const ArrayType *AT = Actions.getASTContext().getAsArrayType(QTy)) {
        if (AT->getElementType()->isCharType()) {
            Valid = true;
            //PP.Diag(Tok,diag::note_pragma_acc_parser_test) << "isArrayType of CharType";
        }
    }
    else if (const PointerType *PT = Ty->getAs<PointerType>()) {
        if (PT->getPointeeType()->isCharType()) {
            Valid = true;
            //PP.Diag(Tok,diag::note_pragma_acc_parser_test) << "isPointerType to CharType";
        }
    }

    if (!Valid) {
        PP.Diag(Tok,diag::note_pragma_acc_parser_test) << "invalid argument type";
        return false;
    }

    Arg *A = new (Actions.getASTContext()) LabelArg(CI,E,&Actions.getASTContext());
    CI->setArg(A);

    return true;
}

bool
Parser::ParseClauseSignificant(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgScalarIntExpr(DK,CI))
        return false;
    return true;
}

bool
Parser::ParseClauseApproxfun(DirectiveKind DK, ClauseInfo *CI) {
    if (Tok.isNot(tok::identifier)) {
        PP.Diag(Tok,diag::note_pragma_acc_parser_test) << "not an identifier";
        return false;
    }

    StringRef FunctionName = Tok.getIdentifierInfo()->getNameStart();

    LookupResult R(Actions,&Actions.Context.Idents.get(FunctionName),
                   Tok.getLocation(),Sema::LookupOrdinaryName);
    if (!Actions.LookupName(R,Actions.TUScope,/*AllowBuiltinCreation=*/false)) {
        PP.Diag(Tok,diag::err_pragma_acc_parser_test) << "lookup failed";
        return false;
    }

    FunctionDecl *ApproxFun = R.getAsSingle<FunctionDecl>();
    if (!ApproxFun) {
        PP.Diag(Tok,diag::err_pragma_acc_function_not_found) << FunctionName;
        return false;
    }

    //PP.Diag(Tok,diag::note_pragma_acc_parser_test) << ApproxFun->getNameInfo().getName().getAsString();
    Arg *A = new (Actions.getASTContext()) FunctionArg(CI,ApproxFun);
    CI->setArg(A);

    ConsumeToken();
    return true;
}

bool
Parser::ParseClauseEvalfun(DirectiveKind DK, ClauseInfo *CI) {
    if (DK == DK_TASKWAIT) {
        ExprResult E = ParseCastExpression(false,false,NotTypeCast);
        // ExprResult E = ParseExpression();
        if (!E.isUsable())
            return false;

        Arg *A = new (Actions.getASTContext()) RawExprArg(CI,E.get(),&Actions.getASTContext());
        CI->setArg(A);

        return true;
    }

    if (Tok.isNot(tok::identifier)) {
        PP.Diag(Tok,diag::note_pragma_acc_parser_test) << "not an identifier";
        return false;
    }

    StringRef FunctionName = Tok.getIdentifierInfo()->getNameStart();

    LookupResult R(Actions,&Actions.Context.Idents.get(FunctionName),
                   Tok.getLocation(),Sema::LookupOrdinaryName);
    if (!Actions.LookupName(R,Actions.TUScope,/*AllowBuiltinCreation=*/false)) {
        PP.Diag(Tok,diag::note_pragma_acc_parser_test) << "lookup failed";
        return false;
    }

    FunctionDecl *ApproxFun = R.getAsSingle<FunctionDecl>();
    if (!ApproxFun) {
        PP.Diag(Tok,diag::err_pragma_acc_function_not_found) << FunctionName;
        return false;
    }

    //PP.Diag(Tok,diag::note_pragma_acc_parser_test) << ApproxFun->getNameInfo().getName().getAsString();
    Arg *A = new (Actions.getASTContext()) FunctionArg(CI,ApproxFun);
    CI->setArg(A);

    ConsumeToken();
    return true;
}

bool
Parser::ParseClauseEstimation(DirectiveKind DK, ClauseInfo *CI) {
    ExprResult Expr = ParseCastExpression(false,false,NotTypeCast);

    if (!Expr.isUsable())
        return false;

    const Type *ETy = Expr.get()->getType().getTypePtr();
    if (const PointerType *PT = dyn_cast<PointerType>(Expr.get()->getType()))
        ETy = PT->getPointeeType().getTypePtr();
    if (!ETy->isFloatingType()) {
        PP.Diag(CI->getLocStart(),diag::err_pragma_acc_parser_test)
            << "expected expression of floating type";
        return false;
    }

    Arg *A = Actions.getACCInfo()->CreateArg(Expr.get(),CI);
    CI->setArg(A);

    return true;
}

bool
Parser::ParseClauseBuffer(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgList(DK,CI))
        return false;
    bool status = true;
    return status;
}

bool
Parser::ParseClauseLocal_buffer(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgList(DK,CI))
        return false;
    bool status = true;
    return status;
}

bool
Parser::ParseClauseIn(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgList(DK,CI))
        return false;
    bool status = true;
    return status;
}

bool
Parser::ParseClauseOut(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgList(DK,CI))
        return false;
    bool status = true;
    return status;
}

bool
Parser::ParseClauseInout(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgList(DK,CI))
        return false;
    bool status = true;
    return status;
}

bool
Parser::ParseClauseDevice_in(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgList(DK,CI))
        return false;
    bool status = true;
    return status;
}

bool
Parser::ParseClauseDevice_out(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgList(DK,CI))
        return false;
    bool status = true;
    return status;
}

bool
Parser::ParseClauseDevice_inout(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgList(DK,CI))
        return false;
    bool status = true;
    return status;
}

bool
Parser::ParseClauseOn(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgList(DK,CI))
        return false;
    bool status = true;
    return status;
}

bool
Parser::ParseClauseWorkers(DirectiveKind DK, ClauseInfo *CI) {
    ArgVector &Args = CI->getArgs();

    bool pending_comma(false);
    while (1) {
        ExprResult Expr = ParseCastExpression(false,false,NotTypeCast);
        if (!Expr.isUsable())
            return false;

#if 0
        // moved to sema
        const Type *ETy = Expr.get()->getType().getTypePtr();
        if (!ETy->isIntegerType()) {
            PP.Diag(CI->getLocStart(),diag::note_pragma_acc_parser_test)
                << "expected expression of integer type";
            return false;
        }
#endif

        Arg *A = new (Actions.getASTContext()) RawExprArg(CI,Expr.get(),&Actions.getASTContext());
        Args.push_back(A);

        pending_comma = false;
        if (Tok.is(tok::comma)) {
            ConsumeToken();
            pending_comma = true;
        }

        if (Tok.is(tok::r_paren))
            break;
    }

    if (pending_comma)
        PP.Diag(Tok,diag::warn_pragma_acc_pending_comma);

    //ignore any construct with empty list
    if (Args.empty())
        return false;

    return true;
}

bool
Parser::ParseClauseGroups(DirectiveKind DK, ClauseInfo *CI) {
    ArgVector &Args = CI->getArgs();

    bool pending_comma(false);
    while (1) {
        ExprResult Expr = ParseCastExpression(false,false,NotTypeCast);
        if (!Expr.isUsable())
            return false;

#if 0
        // moved to sema
        const Type *ETy = Expr.get()->getType().getTypePtr();
        if (!ETy->isIntegerType()) {
            PP.Diag(CI->getLocStart(),diag::note_pragma_acc_parser_test)
                << "expected expression of integer type";
            return false;
        }
#endif

        Arg *A = new (Actions.getASTContext()) RawExprArg(CI,Expr.get(),&Actions.getASTContext());
        Args.push_back(A);

        pending_comma = false;
        if (Tok.is(tok::comma)) {
            ConsumeToken();
            pending_comma = true;
        }

        if (Tok.is(tok::r_paren))
            break;
    }

    if (pending_comma)
        PP.Diag(Tok,diag::warn_pragma_acc_pending_comma);

    //ignore any construct with empty list
    if (Args.empty())
        return false;

    return true;
}

bool
Parser::ParseClauseBind(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgScalarIntExpr(DK,CI))
        return false;
    return true;
}

bool
Parser::ParseClauseBind_approximate(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgScalarIntExpr(DK,CI))
        return false;
    return true;
}

bool
Parser::ParseClauseSuggest(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgScalarIntExpr(DK,CI))
        return false;
    return true;
}

bool
Parser::ParseClauseEnergy_joule(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgScalarIntExpr(DK,CI))
        return false;
    return true;
}

bool
Parser::ParseClauseRatio(DirectiveKind DK, ClauseInfo *CI) {
    if (!ParseArgDoubleExpr(DK,CI))
        return false;
    return true;
}

//Parse Directives

bool
Parser::ParseDirectiveTask(DirectiveInfo *DI) {
    return ParseClauses(DI);
}

bool
Parser::ParseDirectiveTaskgroup(DirectiveInfo *DI) {
    return ParseClauses(DI);
}

bool
Parser::ParseDirectiveTaskwait(DirectiveInfo *DI) {
    return ParseClauses(DI);
}

bool
Parser::ParseDirectiveSubtask(DirectiveInfo *DI) {
    return ParseClauses(DI);
}

void Parser::HandlePragmaCentaurus() {
    //FIXME: better use a helper function to clean up the eod

    //http://www.parashift.com/c++-faq-lite/pointers-to-members.html
#define PARSER_CALL(method) ((*this).*(method))

    using namespace centaurus;

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

    //Parse the rest directive
    //keep any bad directive to avoid false errors afterwards

    //get the first clause if any
    ConsumeAnyToken();

    assert(DirectiveStartLoc.isValid());
    DirectiveInfo *DI =
        ActOnNewCentaurusDirective(DK,DirectiveStartLoc,Actions.getASTContext());

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

        // We have parse errors, ignore directive
        return;
    }
    else {
        assert(Tok.is(tok::eod) && "unknown Parser state");
        DirectiveEndLoc = ConsumeToken();  //consume eod
    }

    //we consumed the eod in each case

    assert(DirectiveEndLoc.isValid());
    DI->setLocEnd(DirectiveEndLoc);
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
    if (Tok.isNot(tok::identifier)) {
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
        ActOnNewCentaurusClause(CK,ClauseStartLoc,ClauseEndLoc,DI,
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

        PP.Diag(Tok,diag::warn_pragma_expected_lparen) << ExtensionName;
        return false;
    }

    ConsumeParen();

    if (!PARSER_CALL(ParseClause[CK])(DK,CI)) {
        CList.pop_back();
        return false;
    }

    if (Tok.isNot(tok::r_paren)) {
        PP.Diag(Tok,diag::warn_pragma_expected_rparen) << ExtensionName;
        CList.pop_back();
        return false;
    }
    ClauseEndLoc = ConsumeParen();
    CI->setLocEnd(ClauseEndLoc);

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
        found_clause = ParseClauseWrapper(DI);
        if (!found_clause)
            return false;
        if (!Actions.getACCInfo()->isValidClauseWrapper(DK,CList.back()))
            return false;

        pending_comma = Tok.is(tok::comma);
        if (pending_comma)
            ConsumeToken();
    }

    if (pending_comma)
        PP.Diag(Tok,diag::warn_pragma_acc_pending_comma);

    if (!DI->hasOptionalClauses() && !found_clause) {
        PP.Diag(Tok,diag::err_pragma_acc_incomplete);
        return false;
    }

    return true;
}
