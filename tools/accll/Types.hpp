#ifndef __ACCLL_TYPES_H__
#define __ACCLL_TYPES_H__

#include "llvm/ADT/DenseMap.h"

namespace clang {
    class FunctionDecl;
    class ASTContext;

    namespace openacc {
        class Arg;
        class DirectiveInfo;
        class RegionStack;
    }
}

namespace accll {
typedef llvm::DenseMap<clang::FunctionDecl*,size_t> UIDKernelMap;
typedef std::pair<std::string, std::string> ArgNames;
typedef llvm::DenseMap<clang::openacc::Arg*,ArgNames> NameMap;

struct ObjRefDef {
    std::string NameRef;
    std::string Definition;

    ObjRefDef(std::string NameRef, std::string Definition) :
        NameRef(NameRef), Definition(Definition) {}
    ObjRefDef() {}
};

struct DataIOSrc : public ObjRefDef {
    std::string NumArgs;

    DataIOSrc(clang::ASTContext *Context,clang::openacc::DirectiveInfo *DI,
              NameMap &Map,clang::openacc::RegionStack &RStack)
    {
        init(Context,DI,Map,RStack);
    }

private:
    void init(clang::ASTContext *Context,clang::openacc::DirectiveInfo *DI,
              NameMap &Map,clang::openacc::RegionStack &RStack);
};

struct KernelRefDef {
    static UIDKernelMap KernelUIDMap;

    ObjRefDef HostCode;
    ObjRefDef DeviceCode;
    std::string DeviceCodeInlineDeclaration;
    std::string Binary;
    std::string BuildOptions;
    std::string BuildLog;

    KernelRefDef() {}

    bool compile(std::string inFile,const std::vector<std::string> &options = std::vector<std::string>());

    KernelRefDef(clang::ASTContext *Context,clang::FunctionDecl *FD);

    void CreateInlineDeclaration();

    size_t getKernelUID(clang::FunctionDecl *FD);
};

}

#endif
