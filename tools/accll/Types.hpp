#ifndef __ACCLL_TYPES_H__
#define __ACCLL_TYPES_H__

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SetVector.h"

#include "clang/Basic/OpenACC.h"

namespace clang {
    class FunctionDecl;
    class ASTContext;
    class CallGraph;

    namespace openacc {
        class Arg;
        class DirectiveInfo;
        class RegionStack;
    }
}

namespace accll {
typedef llvm::StringMap<size_t> UIDKernelMap;
typedef std::pair<std::string, std::string> ArgNames;

struct ObjRefDef {
#if 1
    std::string NameRef;
    std::string Definition;
    std::string HeaderDecl;
#else
    std::string NameType;
    std::string NameRef;
    std::string NameInit;
#endif

    ObjRefDef(std::string NameRef, std::string Definition, std::string HeaderDecl = std::string()) :
        NameRef(NameRef), Definition(Definition), HeaderDecl(HeaderDecl) {}
    ObjRefDef() {}
};

struct DataIOSrc {
    std::string NameRef;
    std::string Definition;
    std::string NumArgs;

    DataIOSrc(clang::ASTContext *Context,clang::openacc::DirectiveInfo *DI,
              clang::openacc::RegionStack &RStack)
    {
        init(Context,DI,RStack);
    }

private:
    void init(clang::ASTContext *Context,clang::openacc::DirectiveInfo *DI,
              clang::openacc::RegionStack &RStack);
};

struct KernelRefDef {
    static UIDKernelMap KernelUIDMap;

    ObjRefDef HostCode;
    ObjRefDef DeviceCode;
    ObjRefDef InlineDeviceCode;
    ObjRefDef Binary;

    std::vector<std::string> BuildOptions;
    std::string BuildLog;

    KernelRefDef() {}

    void findCallDeps(clang::FunctionDecl *StartFD, clang::CallGraph *CG,
                      llvm::SmallSetVector<clang::FunctionDecl *,sizeof(clang::FunctionDecl *)> &Deps);

    //return the size of the compiled kernel in bytes, 0 if cannot compile
    std::string compile(std::string inFile,
                        const std::string &platform = std::string("NVIDIA"),
                        const std::vector<std::string> &options = std::vector<std::string>());

    KernelRefDef(clang::ASTContext *Context,clang::FunctionDecl *FD, clang::CallGraph *CG,
                 std::string &Extensions, std::string &UserTypes,
                 const enum clang::openacc::PrintSubtaskType = clang::openacc::K_PRINT_ALL);

    size_t getKernelUID(std::string Name);
};

}

#endif
