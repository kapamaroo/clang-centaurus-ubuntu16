#ifndef ACL_STAGES_HPP_
#define ACL_STAGES_HPP_

#include "clang/Basic/Centaurus.h"
#include "clang/Tooling/Refactoring.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include "clang/Analysis/CallGraph.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "Types.hpp"
#include "Common.hpp"
#include "CentaurusConfig.hpp"

//copy from RecursiveASTVisitor.h
#define TRY_TO(CALL_EXPR)                                       \
    do { if (!getDerived().CALL_EXPR) return false; } while (0)

//http://www.parashift.com/c++-faq-lite/pointers-to-members.html
#define VISITOR_CALL(method) ((*this).*(method))

namespace acl {

extern std::string KernelHeader;
extern std::string OpenCLExtensions;
extern std::string NewFileHeader;

std::pair<std::string,std::string>
getGeometry(clang::centaurus::DirectiveInfo *DI);

std::string getNewNameFromOrigName(std::string OrigName);

clang::centaurus::Arg*
CreateNewArgFrom(clang::Expr *E, clang::centaurus::ClauseInfo *ImplicitCI,
                 clang::ASTContext *Context);

///////////////////////////////////////////////////////////////////////////////
//                        Stage0
///////////////////////////////////////////////////////////////////////////////

class Stage0_ASTVisitor : public clang::RecursiveASTVisitor<Stage0_ASTVisitor> {
private:
    CentaurusConfig Config;

    std::vector<std::string> &InputFiles;
    std::vector<std::string> &RegularFiles;
    clang::ASTContext *Context;
    bool hasDirectives;
    bool hasDeviceCode;
    bool hasRuntimeCalls;
    bool hasMain;

public:
    void Init(clang::ASTContext *C);
    void Finish(clang::ASTContext *Context);

    explicit Stage0_ASTVisitor(CentaurusConfig Config,
                               std::vector<std::string> &InputFiles,
                               std::vector<std::string> &RegularFiles) :
        Config(Config), InputFiles(InputFiles), RegularFiles(RegularFiles),
        Context(0),
        hasDirectives(false), hasDeviceCode(false), hasRuntimeCalls(false), hasMain(false) {}

    bool VisitAclStmt(clang::AclStmt *ACC);
    bool VisitCallExpr(clang::CallExpr *CE);
    bool VisitFunctionDecl(clang::FunctionDecl *FD);

};

class Stage0_ASTConsumer : public clang::ASTConsumer {
private:
    Stage0_ASTVisitor Visitor;

public:
    explicit Stage0_ASTConsumer(CentaurusConfig Config,
                                std::vector<std::string> &InputFiles,
                                std::vector<std::string> &RegularFiles) :
        Visitor(Config,InputFiles,RegularFiles) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        clang::SourceManager &SM = Context.getSourceManager();
        std::string FileName = SM.getFileEntryForID(SM.getMainFileID())->getName();
        std::string Ext = GetDotExtension(FileName);
#if 0
        if (Ext.compare(".c")) {
            llvm::outs() << "skip invalid input file: '" << FileName << "'\n";
            return;
        }
#endif

        clang::TranslationUnitDecl *TU = Context.getTranslationUnitDecl();
        Visitor.Init(&Context);
        Visitor.TraverseDecl(TU);
        Visitor.Finish(&Context);
    }
};

//This doesn't need to be an interface,
//simply having the method newASTConsumer will be enough...
class Stage0_ConsumerFactory {
private:
    CentaurusConfig Config;

    std::vector<std::string> &InputFiles;
    std::vector<std::string> &RegularFiles;

public:
    Stage0_ConsumerFactory(CentaurusConfig Config,
                           std::vector<std::string> &InputFiles,
                           std::vector<std::string> &RegularFiles) :
        Config(Config),
        InputFiles(InputFiles), RegularFiles(RegularFiles) {}

    std::unique_ptr<clang::ASTConsumer> newASTConsumer() {
        return std::unique_ptr<clang::ASTConsumer>(new Stage0_ASTConsumer(Config,InputFiles,RegularFiles));
    }
};

///////////////////////////////////////////////////////////////////////////////
//                        Stage1
///////////////////////////////////////////////////////////////////////////////

#if 0
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
#endif

class Stage1_ASTVisitor : public clang::RecursiveASTVisitor<Stage1_ASTVisitor> {
private:
    CentaurusConfig Config;

    clang::tooling::Replacements &ReplacementPool;
    std::vector<std::string> &InputFiles;
    std::vector<std::string> &KernelFiles;
    clang::ASTContext *Context;
    clang::CallGraph *CG;

    typedef std::pair<clang::centaurus::ClauseInfo*, std::string> AsyncEvent;
    typedef llvm::SmallVector<AsyncEvent,8> AsyncEventVector;
    AsyncEventVector AsyncEvents;

    clang::centaurus::RegionStack RStack;
    clang::FunctionDecl *CurrentFunction;

    //VarDeclVector *IgnoreVars;

    std::string UserTypes;

    std::string Suffix;
    std::string CommonFileHeader;
    std::string NewHeader;
    std::string HostHeader;

    llvm::SmallVector<clang::VarDecl *, 4> IterationSpace;

    bool Stage1_TraverseTemplateArgumentLocsHelper(const clang::TemplateArgumentLoc *TAL,unsigned Count);

public:

    void Init(clang::ASTContext *C, clang::CallGraph *_CG);
    void Finish();

    explicit Stage1_ASTVisitor(CentaurusConfig Config,
                               clang::tooling::Replacements &ReplacementPool,
                               std::vector<std::string> &InputFiles,
                               std::vector<std::string> &KernelFiles) :
        Config(Config),
        ReplacementPool(ReplacementPool),
        InputFiles(InputFiles), KernelFiles(KernelFiles),
        Context(0), CG(0)
        //,IgnoreVars(0)
        /*, DeviceOnlyVisibleVars(0)*/ {}

    //////

    bool TraverseAclStmt(clang::AclStmt *S);
    bool TraverseForStmt(clang::ForStmt *F);
    bool TraverseFunctionDecl(clang::FunctionDecl *FD);

    bool VisitAclStmt(clang::AclStmt *ACC);
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
    explicit Stage1_ASTConsumer(CentaurusConfig Config,
                                clang::tooling::Replacements &ReplacementPool,
                                std::vector<std::string> &InputFiles,
                                std::vector<std::string> &KernelFiles) :
        Visitor(Config,ReplacementPool,InputFiles,KernelFiles) {}

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
    CentaurusConfig Config;

    clang::tooling::Replacements &ReplacementPool;
    std::vector<std::string> &InputFiles;
    std::vector<std::string> &KernelFiles;

public:
    Stage1_ConsumerFactory(CentaurusConfig Config,
                           clang::tooling::Replacements &ReplacementPool,
                           std::vector<std::string> &InputFiles,
                           std::vector<std::string> &KernelFiles) :
        Config(Config),
        ReplacementPool(ReplacementPool),
        InputFiles(InputFiles), KernelFiles(KernelFiles) {}
    std::unique_ptr<clang::ASTConsumer> newASTConsumer() {
        return std::unique_ptr<clang::ASTConsumer>(new Stage1_ASTConsumer(Config,ReplacementPool,InputFiles,KernelFiles));
    }
};

}  //namespace acl

#endif
