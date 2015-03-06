#ifndef ACCLL_STAGES_HPP_
#define ACCLL_STAGES_HPP_

#include "clang/Basic/OpenACC.h"
#include "clang/Tooling/Refactoring.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include "Types.hpp"

//copy from RecursiveASTVisitor.h
#define TRY_TO(CALL_EXPR)                                       \
    do { if (!getDerived().CALL_EXPR) return false; } while (0)

//http://www.parashift.com/c++-faq-lite/pointers-to-members.html
#define VISITOR_CALL(method) ((*this).*(method))

namespace accll {

extern std::string KernelHeader;
extern std::string OpenCLExtensions;

std::pair<std::string,std::string>
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

class Stage1_ASTVisitor : public clang::RecursiveASTVisitor<Stage1_ASTVisitor> {
private:
    clang::tooling::Replacements &ReplacementPool;
    std::vector<std::string> &KernelFiles;
    clang::ASTContext *Context;
    std::vector<std::string>::iterator CurrentKernelFileIterator;

    NameMap Map;

    typedef std::pair<clang::openacc::ClauseInfo*, std::string> AsyncEvent;
    typedef llvm::SmallVector<AsyncEvent,8> AsyncEventVector;
    AsyncEventVector AsyncEvents;

    clang::openacc::RegionStack RStack;
    clang::FunctionDecl *CurrentFunction;

    VarDeclVector *IgnoreVars;

    std::string UserTypes;

    std::string addMoveHostToDevice(clang::openacc::Arg *A,
                                    clang::openacc::ClauseInfo *AsyncCI = 0,
                                    std::string Event = std::string());
    std::string addMoveDeviceToHost(clang::openacc::Arg *A,
                                    clang::openacc::ClauseInfo *AsyncCI = 0,
                                    std::string Event = std::string());

    std::string CreateNewUniqueEventNameFor(clang::openacc::Arg *A);

    std::string EmitCodeForDirectiveWait(clang::openacc::DirectiveInfo *DI,
                                         clang::Stmt *SubStmt);

    clang::openacc::Arg *EmitImplicitDataMoveCodeFor(clang::Expr *E);
    bool Rename(clang::Expr *E);

    bool Stage1_TraverseTemplateArgumentLocsHelper(const clang::TemplateArgumentLoc *TAL,unsigned Count);

    std::string getCurrentKernelFile() const { return *CurrentKernelFileIterator; }
    unsigned getCurrentKernelFileStartOffset() const {
        return KernelHeader.size() + OpenCLExtensions.size();
    }
    unsigned getCurrentKernelFileEndOffset() const {
        clang::SourceManager &SM = Context->getSourceManager();
        const clang::FileEntry *KernelEntry =
            SM.getFileManager().getFile(getCurrentKernelFile());
        assert(KernelEntry);
        return KernelEntry->getSize();
    }

public:

    void Init(clang::ASTContext *C);
    void Finish();

    explicit Stage1_ASTVisitor(clang::tooling::Replacements &ReplacementPool,
                               std::vector<std::string> &KernelFiles) :
        ReplacementPool(ReplacementPool), KernelFiles(KernelFiles), Context(0),
        CurrentKernelFileIterator(KernelFiles.begin()),
        IgnoreVars(0)
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
    bool VisitReturnStmt(clang::ReturnStmt *S);
    bool VisitBinaryOperator(clang::BinaryOperator *BO);

    bool VisitEnumDecl(clang::EnumDecl *E);
    bool VisitTypedefDecl(clang::TypedefDecl *T);
    bool VisitRecordDecl(clang::RecordDecl *R);
};

class Stage1_ASTConsumer : public clang::ASTConsumer {
private:
    Stage1_ASTVisitor Visitor;

public:
    explicit Stage1_ASTConsumer(clang::tooling::Replacements &ReplacementPool,
                                std::vector<std::string> &KernelFiles) :
        Visitor(ReplacementPool,KernelFiles) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        clang::TranslationUnitDecl *TU = Context.getTranslationUnitDecl();
        Visitor.Init(&Context);
        Visitor.TraverseDecl(TU);
        Visitor.Finish();
    }
};

//This doesn't need to be an interface,
//simply having the method newASTConsumer will be enough...
class Stage1_ConsumerFactory {
private:
    clang::tooling::Replacements &ReplacementPool;
    std::vector<std::string> &KernelFiles;

public:
    Stage1_ConsumerFactory(clang::tooling::Replacements &ReplacementPool,
                           std::vector<std::string> &KernelFiles) :
        ReplacementPool(ReplacementPool), KernelFiles(KernelFiles) {}
    clang::ASTConsumer *newASTConsumer() {
        return new Stage1_ASTConsumer(ReplacementPool,KernelFiles);
    }
};

#if 0
///////////////////////////////////////////////////////////////////////////////
//                        Stage3
///////////////////////////////////////////////////////////////////////////////

class Stage3_ASTVisitor : public clang::RecursiveASTVisitor<Stage3_ASTVisitor> {
private:
    clang::tooling::Replacements &ReplacementPool;
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

    bool Stage3_TraverseTemplateArgumentLocsHelper(const clang::TemplateArgumentLoc *TAL,unsigned Count);
public:
    explicit Stage3_ASTVisitor(clang::tooling::Replacements &ReplacementPool,
                               std::vector<std::string> &KernelFiles) :
        ReplacementPool(ReplacementPool), KernelFiles(KernelFiles), Context(0), UID(0),
        CurrentKernelFileIterator(KernelFiles.begin()),
        KernelCalls(0), hasDirectives(0) {}

    std::string CreateNewUniqueEventNameFor(clang::openacc::Arg *A);

    std::string CreateNewNameFor(clang::openacc::Arg *A, bool AddPrefix = true);

    bool TraverseFunctionDecl(clang::FunctionDecl *FD);
    bool VisitVarDecl(clang::VarDecl *VD);
    bool VisitCallExpr(clang::CallExpr *CE);
    bool VisitEnumDecl(clang::EnumDecl *E);
    bool VisitTypedefDecl(clang::TypedefDecl *T);
    bool VisitRecordDecl(clang::RecordDecl *R);

    std::string getIdxOfWorkerInLoop(clang::ForStmt *F, std::string Qual);
};

class Stage3_ASTConsumer : public clang::ASTConsumer {
private:
    Stage3_ASTVisitor Visitor;

public:
    explicit Stage3_ASTConsumer(clang::tooling::Replacements &ReplacementPool,
                                std::vector<std::string> &KernelFiles) :
        Visitor(ReplacementPool,KernelFiles) {}

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

        Visitor.TraverseDecl(TU);
    }
};

//This doesn't need to be an interface,
//simply having the method newASTConsumer will be enough...
class Stage3_ConsumerFactory {
private:
    clang::tooling::Replacements &ReplacementPool;
    std::vector<std::string> &KernelFiles;

public:
    Stage3_ConsumerFactory(clang::tooling::Replacements &ReplacementPool,
                               std::vector<std::string> &KernelFiles) :
        ReplacementPool(ReplacementPool), KernelFiles(KernelFiles) {}
    clang::ASTConsumer *newASTConsumer() {
        return new Stage3_ASTConsumer(ReplacementPool,KernelFiles);
    }
};
#endif

}  //namespace accll

#endif
