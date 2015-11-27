#ifndef __ACL_TYPES_H__
#define __ACL_TYPES_H__

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SetVector.h"

#include "clang/Basic/Centaurus.h"

#include "Common.hpp"
#include "CentaurusConfig.hpp"

namespace clang {
    class FunctionDecl;
    class ASTContext;
    class CallGraph;

    namespace centaurus {
        class Arg;
        class DirectiveInfo;
        class RegionStack;
    }
}

namespace acl {
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

    DataIOSrc(clang::ASTContext *Context,clang::centaurus::DirectiveInfo *DI,
              clang::centaurus::RegionStack &RStack)
    {
        init(Context,DI,RStack);
    }

private:
    void init(clang::ASTContext *Context,clang::centaurus::DirectiveInfo *DI,
              clang::centaurus::RegionStack &RStack);
};

struct PTXASInfo {
    std::string Raw;

    size_t arch;

    //Number of registers
    size_t registers;

    //sizes are in bytes

    //total global size
    size_t gmem;

    size_t stack_frame;
    size_t spill_stores;
    size_t spill_loads;
    size_t cmem;

    PTXASInfo(std::string Log, std::string PlatformName);
    PTXASInfo() :
        Raw(std::string()), arch(0), registers(0), gmem(0),
        stack_frame(0), spill_stores(0), spill_loads(0), cmem(0) {}

    std::string printDeclInit();
};

struct DeviceBin : public ObjRefDef {
    std::string PlatformName;
    ObjRefDef Bin;
    struct PTXASInfo Log;

    explicit DeviceBin(std::string &PlatformName,
                       std::string &SymbolName,
                       std::string &PrefixDef,
                       std::string &APINameRef,
                       std::string &BinArray,
                       std::string &RawLog,
                       const int id);

    std::string ToHex(const std::string &src);

};

struct PlatformBin : public ObjRefDef, public std::vector<DeviceBin> {
    //if empty ignore this KernelBin
    std::string PlatformName;

    explicit PlatformBin(std::string PlatformName) : PlatformName(PlatformName) {}

    PlatformBin() {}
};

struct KernelRefDef {
    static UIDKernelMap KernelUIDMap;

    const CentaurusConfig &ACLConfig;

    ObjRefDef HostCode;
    ObjRefDef DeviceCode;
    ObjRefDef InlineDeviceCode;

    //common for all platforms
    std::vector<std::string> BuildOptions;

    std::vector<PlatformBin> Binary;

    KernelRefDef(const CentaurusConfig &ACLConfig) : ACLConfig(ACLConfig) {}

    void findCallDeps(clang::FunctionDecl *StartFD, clang::CallGraph *CG,
                      llvm::SmallSetVector<clang::FunctionDecl *,sizeof(clang::FunctionDecl *)> &Deps);

    //return the size of the compiled kernel in bytes, 0 if cannot compile
    int compile(std::string src, std::string SymbolName, std::string PrefixDef,
                const std::vector<std::string> &options = std::vector<std::string>());

    KernelRefDef(const CentaurusConfig &ACLConfig,
                 clang::ASTContext *Context,clang::FunctionDecl *FD, clang::CallGraph *CG,
                 const clang::centaurus::DirectiveInfo *DI,
                 std::string &Extensions, std::string &UserTypes,
                 const enum clang::centaurus::PrintSubtaskType = clang::centaurus::K_PRINT_ALL);

    size_t getKernelUID(std::string Name);

    std::string setDeviceType(const clang::centaurus::DirectiveInfo *DI, const clang::centaurus::ClauseKind CK);

};

}

#endif
