#ifndef ACCLL_STAGES_HPP_
#define ACCLL_STAGES_HPP_

#include "clang/Basic/OpenACC.h"
#include "clang/Tooling/Refactoring.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include "clang/Analysis/CallGraph.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "Types.hpp"
#include "Common.hpp"

//copy from RecursiveASTVisitor.h
#define TRY_TO(CALL_EXPR)                                       \
    do { if (!getDerived().CALL_EXPR) return false; } while (0)

//http://www.parashift.com/c++-faq-lite/pointers-to-members.html
#define VISITOR_CALL(method) ((*this).*(method))

namespace accll {

extern std::string KernelHeader;
extern std::string OpenCLExtensions;
extern std::string NewFileHeader;

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
    std::vector<std::string> &RegularFiles;
    bool hasDirectives;
    bool hasDeviceCode;
    bool hasRuntimeCalls;

public:
    void Finish(clang::ASTContext *Context);

    explicit Stage0_ASTVisitor(std::vector<std::string> &InputFiles,
                               std::vector<std::string> &RegularFiles) :
        InputFiles(InputFiles), RegularFiles(RegularFiles),
        hasDirectives(false), hasDeviceCode(false), hasRuntimeCalls(false) {}

    bool VisitAccStmt(clang::AccStmt *ACC);
    bool VisitCallExpr(clang::CallExpr *CE);

};

class Stage0_ASTConsumer : public clang::ASTConsumer {
private:
    Stage0_ASTVisitor Visitor;

public:
    explicit Stage0_ASTConsumer(std::vector<std::string> &InputFiles,
                                std::vector<std::string> &RegularFiles) :
        Visitor(InputFiles,RegularFiles) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        clang::SourceManager &SM = Context.getSourceManager();
        std::string FileName = SM.getFileEntryForID(SM.getMainFileID())->getName();
        std::string Ext = GetDotExtension(FileName);
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
    std::vector<std::string> &RegularFiles;

public:
    Stage0_ConsumerFactory(std::vector<std::string> &InputFiles,
                           std::vector<std::string> &RegularFiles) :
        InputFiles(InputFiles), RegularFiles(RegularFiles) {}

    clang::ASTConsumer *newASTConsumer() {
        return new Stage0_ASTConsumer(InputFiles,RegularFiles);
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
    std::vector<std::string> &InputFiles;
    std::vector<std::string> &KernelFiles;
    clang::ASTContext *Context;
    clang::CallGraph *CG;

    typedef std::pair<clang::openacc::ClauseInfo*, std::string> AsyncEvent;
    typedef llvm::SmallVector<AsyncEvent,8> AsyncEventVector;
    AsyncEventVector AsyncEvents;

    clang::openacc::RegionStack RStack;
    clang::FunctionDecl *CurrentFunction;

    VarDeclVector *IgnoreVars;

    std::string UserTypes;

    std::string Suffix;
    std::string CommonFileHeader;
    std::string NewHeader;
    std::string HostHeader;

    bool Stage1_TraverseTemplateArgumentLocsHelper(const clang::TemplateArgumentLoc *TAL,unsigned Count);

    bool UpdateDynamicSize(std::string key, clang::Expr *E);
public:

    void Init(clang::ASTContext *C, clang::CallGraph *_CG);
    void Finish();

    explicit Stage1_ASTVisitor(clang::tooling::Replacements &ReplacementPool,
                               std::vector<std::string> &InputFiles,
                               std::vector<std::string> &KernelFiles) :
        ReplacementPool(ReplacementPool),
        InputFiles(InputFiles), KernelFiles(KernelFiles),
        Context(0), CG(0),
        IgnoreVars(0)
        /*, DeviceOnlyVisibleVars(0)*/ {}

    //////

    bool TraverseAccStmt(clang::AccStmt *S);
    bool TraverseFunctionDecl(clang::FunctionDecl *FD);

    bool VisitAccStmt(clang::AccStmt *ACC);
    bool VisitDeclStmt(clang::DeclStmt *DS);
    bool VisitReturnStmt(clang::ReturnStmt *S);
    bool VisitBinaryOperator(clang::BinaryOperator *BO);

#if 0
    bool TraverseMemberExpr(clang::MemberExpr *S);
    bool TraverseArraySubscriptExpr(clang::ArraySubscriptExpr *S);

    bool VisitEnumDecl(clang::EnumDecl *E);
    bool VisitTypedefDecl(clang::TypedefDecl *T);
    bool VisitRecordDecl(clang::RecordDecl *R);
#endif
    bool VisitVarDecl(clang::VarDecl *VD);
    bool VisitCallExpr(clang::CallExpr *CE);
};

class Stage1_ASTConsumer : public clang::ASTConsumer {
private:
    Stage1_ASTVisitor Visitor;

public:
    explicit Stage1_ASTConsumer(clang::tooling::Replacements &ReplacementPool,
                                std::vector<std::string> &InputFiles,
                                std::vector<std::string> &KernelFiles) :
        Visitor(ReplacementPool,InputFiles,KernelFiles) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        clang::TranslationUnitDecl *TU = Context.getTranslationUnitDecl();

        clang::CallGraph CG;
        CG.addToCallGraph(TU);

        Visitor.Init(&Context,&CG);
        Visitor.TraverseDecl(TU);
        Visitor.Finish();
    }
};

//This doesn't need to be an interface,
//simply having the method newASTConsumer will be enough...
class Stage1_ConsumerFactory {
private:
    clang::tooling::Replacements &ReplacementPool;
    std::vector<std::string> &InputFiles;
    std::vector<std::string> &KernelFiles;

public:
    Stage1_ConsumerFactory(clang::tooling::Replacements &ReplacementPool,
                           std::vector<std::string> &InputFiles,
                           std::vector<std::string> &KernelFiles) :
        ReplacementPool(ReplacementPool),
        InputFiles(InputFiles), KernelFiles(KernelFiles) {}
    clang::ASTConsumer *newASTConsumer() {
        return new Stage1_ASTConsumer(ReplacementPool,InputFiles,KernelFiles);
    }
};

}  //namespace accll

#endif
