#ifndef ACCLL_STAGES_HPP_
#define ACCLL_STAGES_HPP_

#include "llvm/ADT/DenseMap.h"

#include "clang/Basic/OpenACC.h"
#include "clang/Tooling/Refactoring.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"

//copy from RecursiveASTVisitor.h
#define TRY_TO(CALL_EXPR)                                       \
    do { if (!getDerived().CALL_EXPR) return false; } while (0)

//http://www.parashift.com/c++-faq-lite/pointers-to-members.html
#define VISITOR_CALL(method) ((*this).*(method))

namespace accll {

extern std::string KernelHeader;

std::pair<llvm::APSInt,llvm::APSInt>
getGeometry(clang::openacc::DirectiveInfo *DI);

std::string getNewNameFromOrigName(std::string OrigName);

clang::openacc::Arg*
CreateNewArgFrom(clang::Expr *E, clang::openacc::ClauseInfo *ImplicitCI,
                 clang::ASTContext *Context);

///////////////////////////////////////////////////////////////////////////////
//                        Stage0
///////////////////////////////////////////////////////////////////////////////

class Stage0_ASTVisitor : public clang::RecursiveASTVisitor<Stage0_ASTVisitor> {
private:
    std::vector<std::string> &InputFiles;
    std::vector<std::string> &KernelFiles;
    bool hasDirectives;
    bool hasDeviceCode;
    bool hasRuntimeCalls;

public:

    std::string RemoveDotExtension(const std::string &filename);
    std::string GetDotExtension(const std::string &filename);

    void Finish(clang::ASTContext *Context);

    explicit Stage0_ASTVisitor(std::vector<std::string> &InputFiles,
                               std::vector<std::string> &KernelFiles) :
        InputFiles(InputFiles), KernelFiles(KernelFiles),
        hasDirectives(false), hasDeviceCode(false), hasRuntimeCalls(false) {}

    bool VisitAccStmt(clang::AccStmt *ACC);
    bool VisitCallExpr(clang::CallExpr *CE);

};

class Stage0_ASTConsumer : public clang::ASTConsumer {
private:
    Stage0_ASTVisitor Visitor;

public:
    explicit Stage0_ASTConsumer(std::vector<std::string> &InputFiles,
                                std::vector<std::string> &KernelFiles) :
        Visitor(InputFiles,KernelFiles) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        clang::SourceManager &SM = Context.getSourceManager();
        std::string FileName = SM.getFileEntryForID(SM.getMainFileID())->getName();
        std::string Ext = Visitor.GetDotExtension(FileName);
        if (Ext.compare(".c")) {
            llvm::outs() << "skip invalid input file: '" << FileName << "'\n";
            return;
        }

        clang::TranslationUnitDecl *TU = Context.getTranslationUnitDecl();
        Visitor.TraverseDecl(TU);
        Visitor.Finish(&Context);
    }
};

//This doesn't need to be an interface,
//simply having the method newASTConsumer will be enough...
class Stage0_ConsumerFactory {
private:
    std::vector<std::string> &InputFiles;
    std::vector<std::string> &KernelFiles;

public:
    Stage0_ConsumerFactory(std::vector<std::string> &InputFiles,
                           std::vector<std::string> &KernelFiles) :
        InputFiles(InputFiles), KernelFiles(KernelFiles) {}

    clang::ASTConsumer *newASTConsumer() {
        return new Stage0_ASTConsumer(InputFiles,KernelFiles);
    }
};

///////////////////////////////////////////////////////////////////////////////
//                        Stage1
///////////////////////////////////////////////////////////////////////////////

class VarDeclVector : public llvm::SmallVector<clang::VarDecl*,32> {
public:
    class VarDeclVector *Prev;

    VarDeclVector() : Prev(0) {}

    bool has(clang::VarDecl* VD) const {
        for (const_iterator II(begin()), EE(end()); II != EE; ++II) {
            clang::VarDecl *IVD = *II;
            if (IVD == VD)
                return true;
        }

        if (Prev)
            return Prev->has(VD);

        return false;
    };
};

class PrivateArgVector : public llvm::SmallVector<clang::openacc::Arg*,32> {
public:
    class PrivateArgVector *Prev;

    PrivateArgVector() : Prev(0) {}

    bool has(clang::openacc::Arg* Target) const {
        for (const_iterator II(begin()), EE(end()); II != EE; ++II) {
            clang::openacc::Arg *A = *II;
            if (Target == A)
                return true;
        }

        if (Prev)
            return Prev->has(Target);

        return false;
    };
};

class Stage1_ASTVisitor : public clang::RecursiveASTVisitor<Stage1_ASTVisitor> {
private:
    clang::tooling::Replacements &Replaces;
    clang::ASTContext *Context;

    typedef std::pair<std::string, std::string> ArgNames;
    typedef llvm::DenseMap<clang::openacc::Arg*,ArgNames> NameMap;
    NameMap Map;

    typedef std::pair<clang::openacc::ClauseInfo*, std::string> AsyncEvent;
    typedef llvm::SmallVector<AsyncEvent,8> AsyncEventVector;
    AsyncEventVector AsyncEvents;

    clang::openacc::RegionStack RStack;
    clang::FunctionDecl *CurrentFunction;

    VarDeclVector *IgnoreVars;

    std::string addVarDeclForDevice(clang::openacc::Arg *A);
    std::string addMoveHostToDevice(clang::openacc::Arg *A,
                                    clang::openacc::ClauseInfo *AsyncCI = 0,
                                    std::string Event = std::string());
    std::string addMoveDeviceToHost(clang::openacc::Arg *A,
                                    clang::openacc::ClauseInfo *AsyncCI = 0,
                                    std::string Event = std::string());
    std::string addDeallocDeviceMemory(clang::openacc::Arg *A);

    std::string CreateNewNameFor(clang::openacc::Arg *A);
    std::string getOrigNameFor(clang::openacc::Arg *A);
    std::string getNewNameFor(clang::openacc::Arg *A);

    std::string CreateNewUniqueEventNameFor(clang::openacc::Arg *A);

    void applyReplacement(clang::tooling::Replacement &R);
    std::string getPrettyExpr(clang::Expr *E);

    void EmitCodeForDataClause(clang::openacc::DirectiveInfo *DI,
                               clang::openacc::ClauseInfo *CI,
                               clang::Stmt *SubStmt);

    void EmitCodeForDataClausesWrapper(clang::openacc::DirectiveInfo *DI,
                                       clang::Stmt *SubStmt);

    void EmitCodeForExecutableDirective(clang::openacc::DirectiveInfo *DI,
                                        clang::Stmt *SubStmt);
    std::string TaskWaitOn(clang::openacc::DirectiveInfo *DI,
                           clang::openacc::ClauseInfo *CI);
    std::string EmitCodeForDirectiveWait(clang::openacc::DirectiveInfo *DI,
                                         clang::Stmt *SubStmt);

    clang::openacc::Arg *EmitImplicitDataMoveCodeFor(clang::Expr *E);
    bool Rename(clang::Expr *E);

    bool Stage1_TraverseTemplateArgumentLocsHelper(const clang::TemplateArgumentLoc *TAL,unsigned Count);

public:

    void Init(clang::ASTContext *C);

    explicit Stage1_ASTVisitor(clang::tooling::Replacements &Replaces) :
        Replaces(Replaces), Context(0), IgnoreVars(0)
        /*, DeviceOnlyVisibleVars(0)*/ {}

    //////

    bool TraverseAccStmt(clang::AccStmt *S);
    bool TraverseMemberExpr(clang::MemberExpr *S);
    bool TraverseArraySubscriptExpr(clang::ArraySubscriptExpr *S);
    bool TraverseFunctionDecl(clang::FunctionDecl *FD);

    bool VisitAccStmt(clang::AccStmt *ACC);
    bool VisitVarDecl(clang::VarDecl *VD);
    bool VisitDeclRefExpr(clang::DeclRefExpr *DRE);
    bool VisitMemberExpr(clang::MemberExpr *ME);
    bool VisitArraySubscriptExpr(clang::ArraySubscriptExpr *ASE);
    bool VisitCallExpr(clang::CallExpr *CE);
    bool VisitReturnStmt(clang::ReturnStmt *S);
};

class Stage1_ASTConsumer : public clang::ASTConsumer {
private:
    Stage1_ASTVisitor Visitor;

public:
    explicit Stage1_ASTConsumer(clang::tooling::Replacements &Replaces) :
        Visitor(Replaces) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        clang::TranslationUnitDecl *TU = Context.getTranslationUnitDecl();
        Visitor.Init(&Context);
        Visitor.TraverseDecl(TU);
    }
};

//This doesn't need to be an interface,
//simply having the method newASTConsumer will be enough...
class Stage1_ConsumerFactory {
private:
    clang::tooling::Replacements &Replaces;

public:
    Stage1_ConsumerFactory(clang::tooling::Replacements &Replaces) :
        Replaces(Replaces) {}
    clang::ASTConsumer *newASTConsumer() {
        return new Stage1_ASTConsumer(Replaces);
    }
};

///////////////////////////////////////////////////////////////////////////////
//                        Stage2
///////////////////////////////////////////////////////////////////////////////

class Stage2_ASTVisitor : public clang::RecursiveASTVisitor<Stage2_ASTVisitor> {
private:
    clang::tooling::Replacements &Replaces;
    std::vector<std::string> &KernelFiles;
    clang::ASTContext *Context;
    clang::openacc::RegionStack RStack;
    std::vector<std::string>::iterator CurrentKernelFileIterator;

public:
    void Init(clang::ASTContext *C);
    void Finish();

    std::string getCurrentKernelFile() const { return *CurrentKernelFileIterator; }

    explicit Stage2_ASTVisitor(clang::tooling::Replacements &Replaces,
                                   std::vector<std::string> &KernelFiles) :
        Replaces(Replaces), KernelFiles(KernelFiles), Context(0),
        CurrentKernelFileIterator(KernelFiles.begin()) {}

    void applyReplacement(clang::tooling::Replacement &R);

    std::string getPrettyExpr(clang::Expr *E);

    bool TraverseAccStmt(clang::AccStmt *S);
    bool VisitAccStmt(clang::AccStmt *ACC);

    std::string getIdxOfWorkerInLoop(clang::ForStmt *F, std::string Qual);

};

class Stage2_ASTConsumer : public clang::ASTConsumer {
private:
    Stage2_ASTVisitor Visitor;

public:
    explicit Stage2_ASTConsumer(clang::tooling::Replacements &Replaces,
                                    std::vector<std::string> &KernelFiles) :
        Visitor(Replaces,KernelFiles) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        clang::TranslationUnitDecl *TU = Context.getTranslationUnitDecl();

#if 0
        if (Visitor.getCurrentKernelFile().empty()) {
            clang::SourceManager &SM = Context.getSourceManager();
            std::string FileName =
                SM.getFileEntryForID(SM.getMainFileID())->getName();
            llvm::outs() << "No Kernels, Skip Stage2 for '" << FileName << "'\n";
            return;
        }
#endif

        Visitor.Init(&Context);
        Visitor.TraverseDecl(TU);
        Visitor.Finish();
    }
};

//This doesn't need to be an interface,
//simply having the method newASTConsumer will be enough...
class Stage2_ConsumerFactory {
private:
    clang::tooling::Replacements &Replaces;
    std::vector<std::string> &KernelFiles;

public:
    Stage2_ConsumerFactory(clang::tooling::Replacements &Replaces,
                               std::vector<std::string> &KernelFiles) :
        Replaces(Replaces), KernelFiles(KernelFiles) {}
    clang::ASTConsumer *newASTConsumer() {
        return new Stage2_ASTConsumer(Replaces,KernelFiles);
    }
};

///////////////////////////////////////////////////////////////////////////////
//                        Stage3
///////////////////////////////////////////////////////////////////////////////

class Stage3_ASTVisitor : public clang::RecursiveASTVisitor<Stage3_ASTVisitor> {
private:
    clang::tooling::Replacements &Replaces;
    std::vector<std::string> &KernelFiles;
    clang::ASTContext *Context;
    clang::openacc::RegionStack RStack;
    int UID;  //unique kernel identifier
    std::vector<std::string>::iterator CurrentKernelFileIterator;

    typedef std::pair<clang::openacc::ClauseInfo*, std::string> AsyncEvent;
    typedef llvm::SmallVector<AsyncEvent,8> AsyncEventVector;
    AsyncEventVector AsyncEvents;
    clang::FunctionDecl *CurrentFunction;

    int KernelCalls;
    int hasDirectives;

    std::string UserTypes;
    std::vector<std::string> ReductionPool;

    bool Stage3_TraverseTemplateArgumentLocsHelper(const clang::TemplateArgumentLoc *TAL,unsigned Count);
public:

    void Init(clang::ASTContext *C);
    void Finish();
    std::string getCurrentKernelFile() const { return *CurrentKernelFileIterator; }
    unsigned getCurrentKernelFileStartOffset() const {
        return KernelHeader.size();
    }
    unsigned getCurrentKernelFileEndOffset() const {
        clang::SourceManager &SM = Context->getSourceManager();
        const clang::FileEntry *KernelEntry =
            SM.getFileManager().getFile(getCurrentKernelFile());
        return KernelEntry->getSize();
    }

    bool hasReduction(std::string Mode, std::string Type) {
        std::string NewReduction = Mode + "_" + Type;
        for (std::vector<std::string>::iterator II = ReductionPool.begin(),
                 EE = ReductionPool.end(); II != EE; ++II)
            if (NewReduction.compare(*II) == 0)
                return true;
        return false;
    }

    void addReduction(std::string Mode, std::string Type) {
        if (hasReduction(Mode,Type))
            return;
        std::string NewReduction = Mode + "_" + Type;
        ReductionPool.push_back(NewReduction);
    }

    std::pair<llvm::APSInt,llvm::APSInt>
    getGeometry(clang::openacc::DirectiveInfo *DI) const;

    std::string AddExtraCodeForPrivateClauses(clang::openacc::DirectiveInfo *DI,
                                              std::string OrigBody,
                                              bool AddBrackets);

    explicit Stage3_ASTVisitor(clang::tooling::Replacements &Replaces,
                               std::vector<std::string> &KernelFiles) :
        Replaces(Replaces), KernelFiles(KernelFiles), Context(0), UID(0),
        CurrentKernelFileIterator(KernelFiles.begin()),
        KernelCalls(0), hasDirectives(0) {}

    void applyReplacement(clang::tooling::Replacement &R);

    std::string getUniqueKernelName(const std::string Base);
    std::string CreateNewUniqueEventNameFor(clang::openacc::Arg *A);

    std::string getPrettyExpr(clang::Expr *E);
    std::string CreateNewNameFor(clang::openacc::Arg *A, bool AddPrefix = true);

    bool TraverseAccStmt(clang::AccStmt *S);
    bool TraverseFunctionDecl(clang::FunctionDecl *FD);
    bool VisitAccStmt(clang::AccStmt *ACC);
    bool VisitVarDecl(clang::VarDecl *VD);
    bool VisitCallExpr(clang::CallExpr *CE);
    bool VisitEnumDecl(clang::EnumDecl *E);
    bool VisitTypedefDecl(clang::TypedefDecl *T);
    bool VisitRecordDecl(clang::RecordDecl *R);

    std::string WaitForTasks(clang::openacc::DirectiveInfo *DI,
                             clang::Stmt *SubStmt);

    std::string CreateKernel(clang::openacc::DirectiveInfo *DI,
                             clang::Stmt *SubStmt);
    std::string MakeParamFromArg(clang::openacc::Arg *A, bool MakeDefinition,
                                 std::string Kernel, int &ArgPos,
                                 std::string &CleanupCode);
    std::string MakeParams(clang::openacc::DirectiveInfo *DI,
                           bool MakeDefinition, std::string Kernels,
                           std::string &CleanupCode, int &ArgNum);
    std::string MakeBody(clang::openacc::DirectiveInfo *DI, clang::Stmt *SubStmt);
    std::string getIdxOfWorkerInLoop(clang::ForStmt *F, std::string Qual);

    std::string MakeReductionPrologue(clang::openacc::DirectiveInfo *DI);
    std::string MakeReductionEpilogue(clang::openacc::DirectiveInfo *DI);

};

class Stage3_ASTConsumer : public clang::ASTConsumer {
private:
    Stage3_ASTVisitor Visitor;

public:
    explicit Stage3_ASTConsumer(clang::tooling::Replacements &Replaces,
                                std::vector<std::string> &KernelFiles) :
        Visitor(Replaces,KernelFiles) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        clang::TranslationUnitDecl *TU = Context.getTranslationUnitDecl();

#if 0
        if (Visitor.getCurrentKernelFile().empty()) {
            clang::SourceManager &SM = Context.getSourceManager();
            std::string FileName =
                SM.getFileEntryForID(SM.getMainFileID())->getName();
            llvm::outs() << "No Kernels, Skip Stage3 for '" << FileName << "'\n";
            return;
        }
#endif

        Visitor.Init(&Context);
        Visitor.TraverseDecl(TU);
        Visitor.Finish();
    }
};

//This doesn't need to be an interface,
//simply having the method newASTConsumer will be enough...
class Stage3_ConsumerFactory {
private:
    clang::tooling::Replacements &Replaces;
    std::vector<std::string> &KernelFiles;

public:
    Stage3_ConsumerFactory(clang::tooling::Replacements &Replaces,
                               std::vector<std::string> &KernelFiles) :
        Replaces(Replaces), KernelFiles(KernelFiles) {}
    clang::ASTConsumer *newASTConsumer() {
        return new Stage3_ASTConsumer(Replaces,KernelFiles);
    }
};

///////////////////////////////////////////////////////////////////////////////
//                        Stage4
///////////////////////////////////////////////////////////////////////////////

class Stage4_ASTVisitor : public clang::RecursiveASTVisitor<Stage4_ASTVisitor> {
private:
    clang::tooling::Replacements &Replaces;
    clang::ASTContext *Context;

    bool Stage4_TraverseTemplateArgumentLocsHelper(const clang::TemplateArgumentLoc *TAL,unsigned Count);

public:

    void Init(clang::ASTContext *C);

    explicit Stage4_ASTVisitor(clang::tooling::Replacements &Replaces) :
        Replaces(Replaces), Context(0) {}

    void applyReplacement(clang::tooling::Replacement &R);

    std::string getPrettyExpr(clang::Expr *E);

    bool TraverseFunctionDecl(clang::FunctionDecl *FD);
    bool TraverseCallExpr(clang::CallExpr *CE);
    bool TraverseParenExpr(clang::ParenExpr *PE);
    bool VisitDeclRefExpr(clang::DeclRefExpr *DRE);
    bool VisitCallExpr(clang::CallExpr *CE);
    bool VisitParmVarDecl(clang::ParmVarDecl *P);

};

class Stage4_ASTConsumer : public clang::ASTConsumer {
private:
    Stage4_ASTVisitor Visitor;

public:
    explicit Stage4_ASTConsumer(clang::tooling::Replacements &Replaces) :
        Visitor(Replaces) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        clang::TranslationUnitDecl *TU = Context.getTranslationUnitDecl();

        Visitor.Init(&Context);
        Visitor.TraverseDecl(TU);
    }
};

//This doesn't need to be an interface,
//simply having the method newASTConsumer will be enough...
class Stage4_ConsumerFactory {
private:
    clang::tooling::Replacements &Replaces;

public:
    Stage4_ConsumerFactory(clang::tooling::Replacements &Replaces) :
        Replaces(Replaces) {}
    clang::ASTConsumer *newASTConsumer() {
        return new Stage4_ASTConsumer(Replaces);
    }
};

}  //namespace accll

#endif
