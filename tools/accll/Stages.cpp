#include "Stages.hpp"
#include "Common.hpp"

#include <iostream>
#include <fstream>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

using namespace llvm;
using namespace clang;
using namespace clang::tooling;
using namespace clang::openacc;
using namespace accll;

namespace {
    accll::CentaurusConfig ACLConfig;

    std::vector<std::string> APIHeaderVector;
    llvm::StringMap<bool> DepHeaders;
    bool TrackThisHeader(std::string &Header) {
        for (std::vector<std::string>::iterator
             II = APIHeaderVector.begin(), EE = APIHeaderVector.end(); II != EE; ++II)
            // do not track internal api header files
            if (Header.find(*II) != std::string::npos)
                return false;
        return true;
    }

    llvm::StringMap<std::string> DynSizeMap;
    llvm::DenseMap<FunctionDecl *,KernelRefDef *> KernelAccuratePool;
    llvm::DenseMap<FunctionDecl *,KernelRefDef *> KernelApproximatePool;
    llvm::DenseMap<FunctionDecl *,KernelRefDef *> KernelEvaluatePool;
    llvm::DenseMap<FunctionDecl *,std::string> CommonFunctionsPool;
    llvm::SmallPtrSet<FunctionDecl *,32> EraseFunctionDeclPool;
}

std::string print(const PTXASInfo &info) {
    std::string str;
    llvm::raw_string_ostream OS(str);
#define PRINT(x) #x << " = " << x << "\n"
    OS  << "\n\n"
        << PRINT(info.arch)
        << PRINT(info.registers)
        << PRINT(info.gmem)
        << PRINT(info.stack_frame)
        << PRINT(info.spill_stores)
        << PRINT(info.spill_loads)
        << PRINT(info.cmem)
        ;
#undef PRINT
    return OS.str();
}

#if 0
static std::string getUniqueKernelName(const std::string Base) {
    static int UID = 0;  //unique kernel identifier
    std::string ID;
    raw_string_ostream OS(ID);
    OS << Base << "_" << UID;
    UID++;
    return OS.str();
}
#endif

struct GeometrySrc : ObjRefDef {
private:
    void init(DirectiveInfo *DI, clang::ASTContext *Context);

public:
    GeometrySrc(DirectiveInfo *DI, clang::ASTContext *Context) : ObjRefDef() {
        init(DI,Context);
    }
};

static
ObjRefDef printFunction(clang::FunctionDecl *FD, clang::ASTContext *Context,
                        std::string AlternativeName,
                        const enum PrintSubtaskType SubtaskPrintMode)
{
    std::string Ref;
    std::string Def;
    raw_string_ostream OS(Def);
    switch (SubtaskPrintMode) {
    case K_PRINT_ALL:
#if 1
        Ref = FD->getNameAsString();
#else
        Ref = getUniqueKernelName(FD->getNameAsString());
#endif
       if (Context->getSourceManager().isFromMainFile(FD->getLocStart()))
            FD->print(OS,Context->getPrintingPolicy());
        break;
    case K_PRINT_ACCURATE_SUBTASK: {
        //always print the accurate version
        Ref = AlternativeName;
        FD->printAccurateVersion(OS,Context->getPrintingPolicy(),AlternativeName);
        break;
    }
    case K_PRINT_APPROXIMATE_SUBTASK: {
        //always print the approximate version
        Ref = AlternativeName;
        FD->printApproximateVersion(OS,Context->getPrintingPolicy(),AlternativeName);
        break;
    }
    default:
        assert(0);
    }
    return ObjRefDef(Ref,OS.str());
}

static ClauseInfo *getClauseOfKind(const ClauseList &CList, ClauseKind CK) {
    for (ClauseList::const_iterator
             II = CList.begin(), EE = CList.end(); II != EE; ++II)
        if ((*II)->getKind() == CK)
            return *II;
    return 0;
}

KernelRefDef::KernelRefDef(clang::ASTContext *Context,clang::FunctionDecl *FD, clang::CallGraph *CG,
                           std::string &Extensions, std::string &UserTypes,
                           const enum PrintSubtaskType SubtaskPrintMode)
{
    if (!FD) {
        HostCode.NameRef = "NULL";
        DeviceCode.NameRef = "NULL";
        return;
    }

    if (!FD->getResultType()->isVoidType()) {
        HostCode.NameRef = "NULL";
        DeviceCode.NameRef = "NULL";
        llvm::outs() << ERROR
                     << "Function '" << FD->getNameAsString()
                     << "' must have return type of void to define a task\n";
        return;
    }

    assert(SubtaskPrintMode != K_PRINT_ALL);

    // always set the AlternativeName for the top level kernel function
    std::string AlternativeName = FD->getNameAsString();
    if (Context->isFunctionWithSubtasks(FD)) {
        if (SubtaskPrintMode == K_PRINT_ACCURATE_SUBTASK)
            AlternativeName += "__accurate__";
        else if (SubtaskPrintMode == K_PRINT_APPROXIMATE_SUBTASK)
            AlternativeName += "__approx__";
        // always write a new version if kernel has subtasks
        DeviceCode = printFunction(FD,Context,AlternativeName,SubtaskPrintMode);
    }
    else if (Context->getSourceManager().isFromMainFile(FD->getLocStart())) {
        // if kernel has no subtasks write it on the new file only if it cannot
        // be found through headers
        DeviceCode = printFunction(FD,Context,AlternativeName,SubtaskPrintMode);
    }
    else {
        // exists on header, just set the NameRef
        // we have a header dependency
        SourceManager &SM = Context->getSourceManager();
        std::string DefFile = SM.getFileEntryForID(SM.getFileID(FD->getLocStart()))->getName();
        DepHeaders[DefFile] = true;

#if 0
        llvm::outs() << DEBUG
                     << FD->getNameAsString() << "-------------------kernel function from header, add header dependency -------------" << DefFile << "\n";
#endif
        DeviceCode.NameRef = AlternativeName;
    }

#if 0
        llvm::outs() << DEBUG
                     << "kernel:" << FD->getNameAsString() << "\n"
                     << DeviceCode.Definition << "\n";
#endif

#if 0
        llvm::outs() << DEBUG
                     << "usertypes:" << UserTypes << "\n";
#endif

    std::string __offline = Extensions;
    std::string PreDef;  // = Extensions;
    for (StringMap<bool>::iterator
             II = DepHeaders.begin(), EE = DepHeaders.end(); II != EE; ++II) {
        __offline += "#include \"" + II->getKey().str() + "\"\n";
        //PreDef += "#include \"" + II->getKey().str() + "\"\n";
    }
    __offline += UserTypes;
    //PreDef += UserTypes;

    llvm::SmallSetVector<clang::FunctionDecl *,sizeof(clang::FunctionDecl *)> Deps;
    findCallDeps(FD,CG,Deps);

    //reverse visit to satisfy dependencies
    while (Deps.size()) {
        FunctionDecl *DepFD = Deps.pop_back_val();

        ObjRefDef Src;

        // leave AnternativeName empty if there are no subtasks
        std::string AlternativeName;
        if (Context->isFunctionWithSubtasks(DepFD)) {
            if (SubtaskPrintMode == K_PRINT_ACCURATE_SUBTASK)
                AlternativeName = DepFD->getNameAsString() + "__accurate__";
            else if (SubtaskPrintMode == K_PRINT_APPROXIMATE_SUBTASK)
                AlternativeName = DepFD->getNameAsString() + "__approx__";
            Src = printFunction(DepFD,Context,AlternativeName,SubtaskPrintMode);
            //has two versions which we add independently in the final *.cl file
            __offline += Src.Definition;
            PreDef += Src.Definition;
        }
        else if (Context->getSourceManager().isFromMainFile(DepFD->getLocStart())) {
            Src = printFunction(DepFD,Context,AlternativeName,SubtaskPrintMode);
            // it appears as it is, in both accurate and approximate
            // version (if exists), add it only once in the final *.cl file
            CommonFunctionsPool[DepFD] = Src.Definition;
            __offline += Src.Definition;
        }
        else {
            // exists on header
            //llvm::outs() << DEBUG
            //             << DepFD->getNameAsString() << "-------------------function dep from header\n";
            continue;
        }

#if 0
        llvm::outs() << DEBUG
                     << "dep:" << DepFD->getNameAsString() << "\n"
                     << Src.Definition << "\n";
#endif
    }
    __offline += DeviceCode.Definition;
    DeviceCode.Definition = PreDef + DeviceCode.Definition;

    std::string __inline_definition = __offline;
    ReplaceStringInPlace(__inline_definition,"\\","\\\\");
    ReplaceStringInPlace(__inline_definition,"\r","");
    ReplaceStringInPlace(__inline_definition,"\t","");
    ReplaceStringInPlace(__inline_definition,"\"","\\\"");
    ReplaceStringInPlace(__inline_definition,"\n","\\n");

    InlineDeviceCode.NameRef = "__src_inline__" + DeviceCode.NameRef;
    InlineDeviceCode.HeaderDecl = "extern const char " + InlineDeviceCode.NameRef
        + "[" + toString(__inline_definition.size()) + "];";
    InlineDeviceCode.Definition = "const char " + InlineDeviceCode.NameRef
        + "[" + toString(__inline_definition.size()) + "]"
        + " = "
        + "\"" + __inline_definition + "\";";

    std::string PrefixDef;
    if (SubtaskPrintMode == K_PRINT_ACCURATE_SUBTASK)
        PrefixDef += "__ACCR__";
    else if (SubtaskPrintMode == K_PRINT_APPROXIMATE_SUBTASK)
        PrefixDef += "__APRX__";
    else
        PrefixDef += "__EVAL__";
    compile(__offline,DeviceCode.NameRef,PrefixDef,BuildOptions);

    // On Linux the driver caches compiled kernels in ~/.nv/ComputeCache.
    // Deleting this folder forces a recompile.
    std::string OpenCLCacheDir = "~/.nv/ComputeCache";

    std::string PreAPIDef;
    std::string PlatformTableName = PrefixDef + "PLATFORM_TABLE";
    std::string PlatformTable = "struct _platform_bin *" + PlatformTableName + "[ACL_SUPPORTED_PLATFORMS_NUM] = { NULL };";

    for (std::vector<PlatformBin>::iterator
             II = Binary.begin(), EE = Binary.end(); II != EE; ++II) {
        PlatformBin &Platform = *II;
        llvm::outs() << NOTE
                     << "########    [" << Platform.PlatformName << "]'\n";

        std::string PlatformDef;

        bool CacheWarning = false;
        for (std::vector<DeviceBin>::iterator
                 DI = Platform.begin(), DE = Platform.end(); DI != DE; ++DI) {
            DeviceBin &Device = *DI;
            llvm::outs() << NOTE
                << "########" << Device.Bin.NameRef << "' build log   ########\n";
            if (Device.Log.Raw.size()) {
                llvm::outs() << Device.Log.Raw;
                if (Device.PlatformName.compare("NVIDIA") == 0) {
                    llvm::outs() << print(Device.Log);
                }
            }
            else {
                CacheWarning = true;
            }

            PlatformDef += Device.Definition;
        }

        if (CacheWarning)
            llvm::outs() << WARNING
                //<< "[OpenCL cache]: found '" << Device.Bin.NameRef << "'  -  "
                         << "delete cache directory '" << OpenCLCacheDir
                         << "' to regenerate the build log.\n";

        PlatformDef += Platform.Definition;
        PreAPIDef += PlatformDef;
        PlatformTable += PlatformTableName + "[" "PL_" + Platform.PlatformName + "] = &" + Platform.NameRef + ";";

        llvm::outs() << "\n#################################\n";
    }

    HostCode.NameRef = "__accll_kernel_" + DeviceCode.NameRef;
    HostCode.Definition = PreAPIDef + PlatformTable
        + "struct _kernel_struct " + HostCode.NameRef
        + " = {"
        + ".UID = " + toString(getKernelUID(DeviceCode.NameRef))
        + ",.name = \"" + DeviceCode.NameRef + "\""
        + ",.name_size = " + toString(DeviceCode.NameRef.size())
        + ",.src = " + InlineDeviceCode.NameRef
        + ",.src_size = " + toString(__inline_definition.size())
        + ",.platform_table = &" + PlatformTableName
        + "};";
}

size_t KernelRefDef::getKernelUID(std::string Name) {
    static size_t KUID = 0;
    if (!Name.size())
        return 0;
    UIDKernelMap::const_iterator II = KernelUIDMap.find(Name);
    if (II == KernelUIDMap.end()) {
        KernelUIDMap[Name] = ++KUID;
        return KUID;
    }
    return II->getValue();
}

static FunctionDecl *getApproxFunctionDecl(DirectiveInfo *DI) {
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator
             II = CList.begin(), EE = CList.end(); II != EE; ++II)
        if ((*II)->getKind() == CK_APPROXFUN) {
            FunctionArg *FA = dyn_cast<FunctionArg>((*II)->getArg());
            return FA->getFunctionDecl();
        }
    return 0;
}

struct KernelSrc {
    std::string NameRef;
    std::string Definition;

    clang::ASTContext *Context;

    KernelRefDef *AccurateKernel;
    KernelRefDef *ApproximateKernel;
    KernelRefDef *EvaluationKernel;

    KernelSrc(clang::ASTContext *Context, clang::CallGraph *CG,
              DirectiveInfo *DI, int TaskUID,
              std::string &Extensions, std::string &UserTypes) :
        Context(Context),
        AccurateKernel(0), ApproximateKernel(0), EvaluationKernel(0)
    {
        CreateKernel(Context,CG,DI,Extensions,UserTypes);

        std::string DeviceType = setDeviceType(DI);
        NameRef = "__accll_task_exe";
        Definition = AccurateKernel->HostCode.Definition;

        std::string ApproxName = ApproximateKernel ? std::string("&" + ApproximateKernel->HostCode.NameRef) : "NULL";
        if (ApproximateKernel)
            Definition += ApproximateKernel->HostCode.Definition;

        std::string EstimationName = "NULL";
        std::string EvalfunName = EvaluationKernel ? std::string("&" + EvaluationKernel->HostCode.NameRef) : "NULL";

        if (ACLConfig.ProfileMode) {
            if (EvaluationKernel) {
                Definition += EvaluationKernel->HostCode.Definition;

                ClauseInfo *ClauseEstimation = getClauseOfKind(DI->getClauseList(),CK_ESTIMATION);
                ClauseInfo *ClauseEvalfun = getClauseOfKind(DI->getClauseList(),CK_EVALFUN);
                // see SemaOpenACC.cpp
                unsigned Index = ClauseEvalfun->getArgAs<FunctionArg>()->getFunctionDecl()->getNumParams() - 1;

                EstimationName = "__accll_arg_estimation";
                std::string EstimationCode = "struct _memory_object " + EstimationName + " = {"
                    + ".cl_obj = 0"
                    + ",.index = " + toString(Index)
                    + ",.dependency = D_ESTIMATION"
                    + ",.host_ptr = (void*)" + ClauseEstimation->getArg()->getPrettyArg()
                    + ",.start_offset = 0"
                    + ",.size = sizeof(double)"
                    + "};";
                Definition += EstimationCode;
                EstimationName = "&" + EstimationName;
            }
        }

        Definition += "struct _task_executable " + NameRef + " = {"
            //+ ".UID = " + toString(TaskUID)
            + ".device_type = " + DeviceType
            + ",.kernel_accurate = &" + AccurateKernel->HostCode.NameRef
            + ",.kernel_approximate = " + ApproxName
            + ",.kernel_evalfun = " + EvalfunName
            + ",.estimation = " + EstimationName
            + "};";
    }

private:
    void CreateKernel(clang::ASTContext *Context, clang::CallGraph *CG,
                      clang::openacc::DirectiveInfo *DI,
                      std::string &Extensions, std::string &UserTypes);

    std::string setDeviceType(const DirectiveInfo *DI) {
        std::string DeviceType;
        if (const ClauseInfo *CI = getClauseOfKind(DI->getClauseList(),CK_SUGGEST)) {
            DeviceType += "((unsigned int)" + CI->getArg()->getPrettyArg() + " << 1) | 0x0";
        }
        else if (const ClauseInfo *CI = getClauseOfKind(DI->getClauseList(),CK_BIND)) {
            DeviceType += "((unsigned int)" + CI->getArg()->getPrettyArg() + " << 1) | 0x1";
        }
        else {
            DeviceType += "((unsigned int)ACL_DEV_ALL << 1) | 0x0";
        }
        return DeviceType;
    }
};

struct TaskSrc {
    static int TaskUID;

    std::string Label;
    std::string Approx;

    DataIOSrc MemObjInfo;
    GeometrySrc Geometry;
    KernelSrc OpenCLCode;

    PresumedLoc PLoc;

    TaskSrc(clang::ASTContext *Context, clang::CallGraph *CG, DirectiveInfo *DI,
            clang::openacc::RegionStack &RStack,
            clang::tooling::Replacements &ReplacementPool,
            std::string &Extensions, std::string &UserTypes) :
        Label(getTaskLabel(DI)),
        Approx(getTaskApprox(Context,DI)),
        MemObjInfo(Context,DI,RStack),
        Geometry(DI,Context),
        OpenCLCode(Context,CG,DI,++TaskUID,Extensions,UserTypes)
    {
        PLoc = Context->getSourceManager().getPresumedLoc(DI->getLocStart());
    }

    std::string HostCall();
    std::string KernelCode();

private:
    std::string getTaskLabel(DirectiveInfo *DI) {
        ClauseList &CList = DI->getClauseList();
        for (ClauseList::iterator
                 II = CList.begin(), EE = CList.end(); II != EE; ++II)
            if ((*II)->getKind() == CK_LABEL)
                return (*II)->getArgAs<LabelArg>()->getLabel();
        return "NULL";
    }

    std::string getTaskApprox(clang::ASTContext *Context, DirectiveInfo *DI) {
        ClauseList &CList = DI->getClauseList();
        for (ClauseList::iterator
                 II = CList.begin(), EE = CList.end(); II != EE; ++II)
            if ((*II)->getKind() == CK_SIGNIFICANT)
                return (*II)->getArg()->getPrettyArg();
#warning FIXME:  default task significance
        return "100";
    }
};

int TaskSrc::TaskUID = 0;
UIDKernelMap KernelRefDef::KernelUIDMap;

void
KernelSrc::CreateKernel(clang::ASTContext *Context, clang::CallGraph *CG, DirectiveInfo *DI,
                        std::string &Extensions, std::string &UserTypes) {
    Stmt *SubStmt = DI->getAccStmt()->getSubStmt();
    assert(SubStmt && "Null SubStmt");

    //create device code
    CallExpr *CE = dyn_cast<CallExpr>(SubStmt);
    if (!CE)
        return;

    FunctionDecl *AccurateFun = CE->getDirectCallee();
    FunctionDecl *ApproxFun = getApproxFunctionDecl(DI);

    assert(AccurateFun);

    llvm::DenseMap<FunctionDecl *, KernelRefDef *>::iterator Kref;

    //top level kernel call
    if (Context->isFunctionWithSubtasks(AccurateFun)) {
        assert(!ApproxFun);

        llvm::outs() << "Kernel '" << AccurateFun->getNameAsString() << "' has subtasks\n";

        Kref = KernelAccuratePool.find(AccurateFun);
        if (Kref != KernelAccuratePool.end())
            AccurateKernel = Kref->second;
        else {
            AccurateKernel = new KernelRefDef(Context,AccurateFun,CG,
                                              Extensions,UserTypes,
                                              K_PRINT_ACCURATE_SUBTASK);
            KernelAccuratePool[AccurateFun] = AccurateKernel;
        }

        Kref = KernelApproximatePool.find(AccurateFun);
        if (Kref != KernelApproximatePool.end())
            ApproximateKernel = Kref->second;
        else {
            ApproximateKernel = new KernelRefDef(Context,AccurateFun,CG,
                                                 Extensions,UserTypes,
                                                 K_PRINT_APPROXIMATE_SUBTASK);
            KernelApproximatePool[AccurateFun] = ApproximateKernel;
        }
    }
    else {
        Kref = KernelAccuratePool.find(AccurateFun);
        if (Kref != KernelAccuratePool.end())
            AccurateKernel = Kref->second;
        else {
            AccurateKernel = new KernelRefDef(Context,AccurateFun,CG,
                                              Extensions,UserTypes,
                                              K_PRINT_ACCURATE_SUBTASK);
            KernelAccuratePool[AccurateFun] = AccurateKernel;
        }
        if (ApproxFun) {
            Kref = KernelApproximatePool.find(ApproxFun);
            if (Kref != KernelApproximatePool.end())
                ApproximateKernel = Kref->second;
            else {
                ApproximateKernel = new KernelRefDef(Context,ApproxFun,CG,
                                                     Extensions,UserTypes,
                                                     K_PRINT_APPROXIMATE_SUBTASK);
                KernelApproximatePool[ApproxFun] = ApproximateKernel;
            }
        }
    }

    ClauseInfo *ClauseEvalfun = getClauseOfKind(DI->getClauseList(),CK_EVALFUN);
    ClauseInfo *ClauseEstimation = getClauseOfKind(DI->getClauseList(),CK_ESTIMATION);
    (void)ClauseEstimation;

    assert((bool)(~((bool)ClauseEvalfun ^ (bool)ClauseEstimation))
           && "UNEXPECTED ERROR: evalfun() and estimation()");

    if (ACLConfig.ProfileMode) {
        if (ClauseEvalfun) {
            FunctionDecl *Evalfun = ClauseEvalfun->getArgAs<FunctionArg>()->getFunctionDecl();
            Kref = KernelEvaluatePool.find(Evalfun);
            if (Kref != KernelEvaluatePool.end())
                EvaluationKernel = Kref->second;
            else {
                EvaluationKernel = new KernelRefDef(Context,Evalfun,CG,Extensions,UserTypes);
                KernelEvaluatePool[Evalfun] = EvaluationKernel;
            }
        }
    }
}

std::string TaskSrc::HostCall() {
    std::string FileName = PLoc.getFilename();
#warning FIXME: dynamically generate SrcLocID to take care of possible iteration spaces
    std::string SrcLocID = GetBasename(FileName) + ":" + toString(PLoc.getLine());

    std::string LabelDef = "const char __acl_group_label[] = \"" + Label + "\";";
    std::string SrcLocDef = "const char __acl_srcloc[] = \"" + SrcLocID + "\";";
    std::string call = LabelDef + SrcLocDef
        + "acl_create_task("
        + Approx + ","
        + MemObjInfo.NameRef + "," + MemObjInfo.NumArgs + ","
        + OpenCLCode.NameRef + ","
        + Geometry.NameRef + ","
        + "__acl_group_label" + ","
        + "__acl_srcloc"
        + ");";
    return call;
}

std::string TaskSrc::KernelCode() {
    return OpenCLCode.AccurateKernel->DeviceCode.Definition
        + OpenCLCode.ApproximateKernel->DeviceCode.Definition;
}

void GeometrySrc::init(DirectiveInfo *DI, clang::ASTContext *Context) {
    ClauseInfo *Workers = NULL;
    ClauseInfo *Groups = NULL;

    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator
             II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseInfo *CI = *II;
        if (!Workers && CI->getKind() == CK_WORKERS)
            Workers = CI;
        else if (!Groups && CI->getKind() == CK_GROUPS)
            Groups = CI;
    }

    assert(Workers && Groups);

    std::string Dims;
    raw_string_ostream localOS(Dims);
    localOS << Groups->getArgs().size();
    localOS.str();

    std::string global_init_list;
    for (ArgVector::iterator
             IA = Groups->getArgs().begin(), EA = Groups->getArgs().end(); IA != EA; ++IA) {
        global_init_list += (*IA)->getPrettyArg();
        if ((*IA) != Groups->getArgs().back())
            global_init_list += ",";
    }

    std::string local_init_list;
    for (ArgVector::iterator
             IA = Workers->getArgs().begin(), EA = Workers->getArgs().end(); IA != EA; ++IA) {
        local_init_list += (*IA)->getPrettyArg();
        if ((*IA) != Workers->getArgs().back())
            local_init_list += ",";
    }

    std::string pre_global_code = "size_t __accll_geometry_global[" + Dims + "] = {" + global_init_list + "};";
    std::string pre_local_code = "size_t __accll_geometry_local[" + Dims + "] = {" + local_init_list + "};";

    NameRef = "__task_geometry";
    Definition = pre_global_code + pre_local_code
        + "struct _geometry " + NameRef + " = {"
        + ".dimensions = " + Dims
        + ",.acl_global = __accll_geometry_global"
        + ",.acl_local = __accll_geometry_local };";
}

static bool isRuntimeCall(const std::string Name) {
    std::string data[] = {"create_task","taskwait"};
    static const SmallVector<std::string,8> RuntimeCalls(data, data + sizeof(data)/sizeof(std::string));

    for (SmallVector<std::string,8>::const_iterator II = RuntimeCalls.begin(), EE = RuntimeCalls.end();
         II != EE; ++II)
        if (Name.compare(*II) == 0)
            return true;
    return false;
}

#warning FIXME: autodetect double extensions
std::string accll::OpenCLExtensions = "#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n\n";

#if 0
std::string accll::KernelHeader =
    "/* Generated by accll */\n\n\n";
#else
std::string accll::KernelHeader =
    "/* Generated by accll */\n\n\
#ifndef CLK_LOCAL_MEM_FENCE\n#define CLK_LOCAL_MEM_FENCE 1\n#endif\n\
#ifndef CLK_GLOBAL_MEM_FENCE\n#define CLK_GLOBAL_MEM_FENCE 2\n#endif\n\n\n";
#endif

std::string
accll::getNewNameFromOrigName(std::string OrigName) {
    std::string NewName("__accll_");  //prefix
    NewName += OrigName;
    ReplaceStringInPlace(NewName,"->","_");  //delimiter
    ReplaceStringInPlace(NewName,".","_");   //delimiter

    ReplaceStringInPlace(NewName,"[","_");  //delimiter
    ReplaceStringInPlace(NewName,"]","_");  //delimiter
    return NewName;
}

Arg*
accll::CreateNewArgFrom(Expr *E, ClauseInfo *ImplicitCI, ASTContext *Context) {
    E = E->IgnoreParenImpCasts();
    Arg *Target = 0;
    if (isa<ArraySubscriptExpr>(E))
        Target = new ArrayElementArg(ImplicitCI,E,Context);
    else if (Context->getAsArrayType(E->getType()))
        Target = new ArrayArg(ImplicitCI,E,Context);
    else if (isa<MemberExpr>(E))
        Target = new VarArg(ImplicitCI,E,Context);
    else if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(E)) {
        if (isa<VarDecl>(DeclRef->getDecl()))
            Target = new VarArg(ImplicitCI,E,Context);
    }
    else
        Target = new RawExprArg(ImplicitCI,E,Context);

#if 1
    assert(Target && "null Target");
#else
    //fancy assert
    if (!Target) {
        if (isa<ArraySubscriptExpr>(E))
            assert(Target && "null subscript");
        else if (Context->getAsArrayType(E->getType()))
            assert(Target && "null array type");
        else if (isa<MemberExpr>(E))
            assert(Target && "null member");
        else if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(E)) {
            if (isa<VarDecl>(DeclRef->getDecl()))
                assert(Target && "null VarArg");
            assert(Target && "null declref");
        }
        else
            assert(Target && "null neither");
    }
#endif

    return Target;
}

///////////////////////////////////////////////////////////////////////////////
//                        Stage0
///////////////////////////////////////////////////////////////////////////////
bool
Stage0_ASTVisitor::VisitAccStmt(AccStmt *ACC) {
    hasDirectives = true;
    if (ACC->getDirective()->getKind() == DK_TASK)
        hasDeviceCode = true;
    return true;
}

bool
Stage0_ASTVisitor::VisitCallExpr(CallExpr *CE) {
    //do nothing
    return true;

    FunctionDecl *FD = CE->getDirectCallee();
    if (!FD)
        return true;

    std::string Name = FD->getNameAsString();

    if (isRuntimeCall(Name)) {
        hasRuntimeCalls = true;
        llvm::outs() << "Runtime call to '" << Name << "'\n";
    }

    return true;
}

bool
Stage0_ASTVisitor::VisitFunctionDecl(FunctionDecl *FD) {
    if (FD->isMain()) {
        hasMain = true;
        return true;
    }

    return true;
}

void
Stage0_ASTVisitor::Finish(ASTContext *Context) {
    SourceManager &SM = Context->getSourceManager();
    std::string FileName = SM.getFileEntryForID(SM.getMainFileID())->getName();
    llvm::outs() << "Found                   : '" << FileName << "'\n";

    if (!hasDirectives && !hasRuntimeCalls && !hasMain) {
        //llvm::outs() << DEBUG << "treat as regular file: '" << FileName << "'\n";
        RegularFiles.push_back(FileName);
        return;
    }

    std::string Suffix("_accll");
    std::string Ext = GetDotExtension(FileName);
    std::string NewFile = RemoveDotExtension(FileName) + Suffix + Ext;

    //Create new C file

    std::ifstream src(FileName.c_str());
    std::ofstream dst(NewFile.c_str());
    dst << src.rdbuf();
    dst.flush();

    InputFiles.push_back(NewFile);
    llvm::outs() << "Rewrite to              : '" << NewFile << "'  -  new file\n";
}

///////////////////////////////////////////////////////////////////////////////
//                        Stage1
///////////////////////////////////////////////////////////////////////////////

static void applyReplacement(Replacements &Pool, Replacement &R) {
    static const bool WRITE_REPLACEMENTS (true);
    static const bool DEBUG_REPLACEMENTS (false);

    if (DEBUG_REPLACEMENTS)
        llvm::outs() << R.toString() << "\n";

    if (!R.isApplicable()) {
        llvm::outs() << "****    bad replacement !!!    ****\n";
        return;
    }

    if (WRITE_REPLACEMENTS)
        Pool.insert(R);
}

static std::string getPrettyExpr(clang::ASTContext *Context, Expr *E) {
    std::string StrExpr;
    raw_string_ostream OS(StrExpr);
    E->printPretty(OS,/*Helper=*/0,
                   Context->getPrintingPolicy(),/*Indentation=*/0);
    return OS.str();  //flush
}

bool
Stage1_ASTVisitor::TraverseAccStmt(AccStmt *S) {
    TRY_TO(WalkUpFromAccStmt(S));
    //{ CODE; }
    RStack.EnterRegion(S->getDirective());
    for (Stmt::child_range range = S->children(); range; ++range) {
        TRY_TO(TraverseStmt(*range));
    }
    RStack.ExitRegion(S->getDirective());

    return true;
}

bool
Stage1_ASTVisitor::UpdateDynamicSize(std::string key, Expr *E) {
    std::string SizeStr;

#if 1
    // we need to put every size into a separate global variable
    // in case we need it out of the scope the allocation took place
    return false;
#else

    if (CallExpr *CE = dyn_cast<CallExpr>(E)) {
        FunctionDecl *FD = CE->getDirectCallee();
        std::string Name = FD->getNameAsString();

        if (Name.compare("malloc") == 0) {
            //void *malloc(size_t size);
            SizeStr = getPrettyExpr(Context,CE->getArg(0));
        }
        else if (Name.compare("calloc") == 0) {
            //void *calloc(size_t nmemb, size_t size);
            SizeStr = getPrettyExpr(Context,CE->getArg(0)) + "*" + getPrettyExpr(Context,CE->getArg(1));
        }
        else if (Name.compare("realloc") == 0) {
            //void *realloc(void *ptr, size_t size);
            SizeStr = getPrettyExpr(Context,CE->getArg(1));
        }
        else if (Name.compare("free") == 0) {
            //void free(void *ptr);
            SizeStr = "0";
        }
    }
    else if (UnaryOperator *UOP = dyn_cast<UnaryOperator>(E)) {
        if (UOP->getOpcode() == UO_AddrOf) {
            Expr *Size = UOP->getSubExpr()->IgnoreParenCasts();
            SizeStr = "sizeof(" + getPrettyExpr(Context,Size) + ")";
        }
    }
    else {
        //must have pointer type
#warning FIXME: size from pointer to pointer assignment
        //SizeStr = "sizeof(" + getPrettyExpr(Context,RHS) + ")";
    }

    DynSizeMap[key] = SizeStr;
    return true;
#endif
}

bool
Stage1_ASTVisitor::VisitBinaryOperator(BinaryOperator *BO) {
    SourceManager &SM = Context->getSourceManager();
    if (SM.isInSystemHeader(BO->getSourceRange().getBegin())) {
        //llvm::outs() << "Writing to system header prevented!\n";
        return true;
    }

    //BO_Assign
    if (BO->getOpcode() != BO_Assign)
        return true;

    Expr *LHS = BO->getLHS()->IgnoreParenCasts();
    Expr *RHS = BO->getRHS()->IgnoreParenCasts();

    QualType Ty = LHS->getType();
    if (!Ty->isPointerType())
        return true;

    UpdateDynamicSize(getPrettyExpr(Context,LHS),RHS);
    return true;
}

bool
Stage1_ASTVisitor::VisitDeclStmt(DeclStmt *DS) {
    for (DeclGroupRef::iterator
             II = DS->decl_begin(), EE = DS->decl_end(); II != EE; ++II)
        if (VarDecl *VD = dyn_cast<VarDecl>(*II))
            if (static_cast<ValueDecl*>(VD)->getType()->isPointerType())
                if (Expr *Init = VD->getInit())
                    UpdateDynamicSize(static_cast<NamedDecl*>(VD)->getNameAsString(),
                                      Init->IgnoreParenCasts());
    return true;
}

bool
Stage1_ASTVisitor::VisitAccStmt(AccStmt *ACC) {
    DirectiveInfo *DI = ACC->getDirective();
    Stmt *SubStmt = ACC->getSubStmt();

    NamedDecl *ND = dyn_cast<NamedDecl>(CurrentFunction);
    llvm::outs()
        << " in " << ND->getName() << "(): "
        << "Found OpenACC Directive: " << DI->getAsString() << "\n";

    if (DI->getKind() == DK_TASKWAIT) {
        //generate runtime calls for taskwait
        ClauseList &CList = DI->getClauseList();

        ClauseInfo *ClauseOn = getClauseOfKind(CList,CK_ON);
        ClauseInfo *ClauseLabel = getClauseOfKind(CList,CK_LABEL);
        ClauseInfo *ClauseRatio = getClauseOfKind(CList,CK_RATIO);
        ClauseInfo *ClauseEnergy_joule = getClauseOfKind(CList,CK_ENERGY_JOULE);
        ClauseInfo *ClauseEvalfun = getClauseOfKind(CList,CK_EVALFUN);
        ClauseInfo *ClauseEstimation = getClauseOfKind(CList,CK_ESTIMATION);

        std::string QLabel = "NULL";
        std::string Ratio;
        std::string Energy;

        if (ClauseLabel)
            QLabel = ClauseLabel->getArgAs<LabelArg>()->getQuotedLabel();
        if (ClauseRatio)
            Ratio = ClauseRatio->getArg()->getPrettyArg();
        if (ClauseEnergy_joule)
            Energy = ClauseEnergy_joule->getArg()->getPrettyArg();

        std::string NewCode;
        if (ClauseOn) {
            llvm::outs() << DEBUG
                         << "Unsupported feature: IMPLEMENT ME:  wait_on()";
            return true;
        }
        else if (ClauseLabel && ClauseRatio) {
            NewCode = "acl_taskwait_label_ratio(" + QLabel + "," + Ratio + ");";
        }
        else if (ClauseLabel && ClauseEnergy_joule) {
            NewCode = "acl_taskwait_label_energy(" + QLabel + "," + Energy + ");";
        }
        else if (ClauseLabel) {
            NewCode = "acl_taskwait_label(" + QLabel + ");";
        }
        else {
            NewCode = "acl_taskwait_all();";
        }

        assert((bool)(~((bool)ClauseEvalfun ^ (bool)ClauseEstimation))
               && "UNEXPECTED ERROR: evalfun() and estimation()");

        if (ACLConfig.ProfileMode) {
            if (ClauseEvalfun) {
                std::string EstValue = ClauseEstimation->getArg()->getPrettyArg();
                if (ClauseEstimation->getArg()->getExpr()->getType()->isPointerType())
                    EstValue = "*" + EstValue;
                NewCode += getPrettyExpr(Context,ClauseEvalfun->getArg()->getExpr()->IgnoreParens()) + ";";
                NewCode += "acl_set_group_quality(" + QLabel + "," + EstValue + ");";
            }
        }

        if (!NewCode.empty()) {
            std::string DirectiveSrc =
                DI->getPrettyDirective(Context->getPrintingPolicy(),false);
            //NewCode = "\n{" + NewCode + "\n}";
            NewCode = "/*" + DirectiveSrc + "*/\n" + NewCode;
            SourceLocation StartLoc = DI->getLocStart().getLocWithOffset(-8);
            SourceLocation EndLoc = DI->getLocEnd();
            CharSourceRange Range(SourceRange(StartLoc,EndLoc),/*IsTokenRange=*/false);
            Replacement R(Context->getSourceManager(),Range,NewCode);
            applyReplacement(ReplacementPool,R);
        }
    }
    else if (DI->getKind() == DK_TASK) {
        llvm::outs() << "  -  Create Kernel\n";

        //extra semantic checking
        CallExpr *CE = dyn_cast<CallExpr>(SubStmt);
        FunctionDecl *AccurateFun = CE->getDirectCallee();
        FunctionDecl *ApproxFun = getApproxFunctionDecl(DI);
        if (Context->isFunctionWithSubtasks(AccurateFun) && ApproxFun) {
            llvm::outs() << ERROR
                         << "Kernel with subtasks '"
                         << AccurateFun->getNameAsString()
                         << "' cannot have direct approximate version\n";
            return true;
        }
        /////////////////////////

        TaskSrc NewTask(Context,CG,DI,RStack,ReplacementPool,
                        accll::OpenCLExtensions,UserTypes);

        EraseFunctionDeclPool.insert(CE->getDirectCallee());
        if (FunctionDecl *ApproxFun = getApproxFunctionDecl(DI))
            EraseFunctionDeclPool.insert(ApproxFun);

        std::string DirectiveSrc =
            DI->getPrettyDirective(Context->getPrintingPolicy(),false);
        std::string NewCode = "/*" + DirectiveSrc + "*/\n"
            + "{"
            + NewTask.MemObjInfo.Definition
            + NewTask.Geometry.Definition
            + NewTask.OpenCLCode.Definition
            + NewTask.HostCall()
            + "}";

        SourceLocation PrologueLoc = DI->getLocStart().getLocWithOffset(-8);
        SourceLocation EpilogueLoc;
        if (isa<CompoundStmt>(SubStmt))
            EpilogueLoc = SubStmt->getLocEnd().getLocWithOffset(1);
        else if (isa<CallExpr>(SubStmt))
            EpilogueLoc = SubStmt->getLocEnd().getLocWithOffset(2);
        else
            EpilogueLoc = SubStmt->getLocEnd().getLocWithOffset(2);

        CharSourceRange Range(SourceRange(PrologueLoc,EpilogueLoc),/*IsTokenRange=*/false);
        Replacement HostCall(Context->getSourceManager(),Range,NewCode);
        applyReplacement(ReplacementPool,HostCall);
    }

    llvm::outs() << "\n";

    return true;
}

//used inside TraverseFunctionDecl
bool
Stage1_ASTVisitor::Stage1_TraverseTemplateArgumentLocsHelper(const TemplateArgumentLoc *TAL,unsigned Count) {
    for (unsigned I = 0; I < Count; ++I) {
        TRY_TO(TraverseTemplateArgumentLoc(TAL[I]));
    }
    return true;
}

bool
Stage1_ASTVisitor::TraverseFunctionDecl(FunctionDecl *FD) {
    TRY_TO(WalkUpFromFunctionDecl(FD));
    SourceManager &SM = Context->getSourceManager();
    CurrentFunction = 0;
    if (!SM.isInSystemHeader(FD->getSourceRange().getBegin()))
        CurrentFunction = FD;

    //{ CODE; }
    //inline the helper function
    //
    //TraverseFunctionHelper(D);

    TRY_TO(TraverseNestedNameSpecifierLoc(FD->getQualifierLoc()));
    TRY_TO(TraverseDeclarationNameInfo(FD->getNameInfo()));

    // If we're an explicit template specialization, iterate over the
    // template args that were explicitly specified.  If we were doing
    // this in typing order, we'd do it between the return type and
    // the function args, but both are handled by the FunctionTypeLoc
    // above, so we have to choose one side.  I've decided to do before.
    if (const FunctionTemplateSpecializationInfo *FTSI =
        FD->getTemplateSpecializationInfo()) {
        if (FTSI->getTemplateSpecializationKind() != TSK_Undeclared &&
            FTSI->getTemplateSpecializationKind() != TSK_ImplicitInstantiation) {
            // A specialization might not have explicit template arguments if it has
            // a templated return type and concrete arguments.
            if (const ASTTemplateArgumentListInfo *TALI =
                FTSI->TemplateArgumentsAsWritten) {
                TRY_TO(Stage1_TraverseTemplateArgumentLocsHelper(TALI->getTemplateArgs(),
                                                                 TALI->NumTemplateArgs));
            }
        }
    }

    // Visit the function type itself, which can be either
    // FunctionNoProtoType or FunctionProtoType, or a typedef.  This
    // also covers the return type and the function parameters,
    // including exception specifications.
    if (TypeSourceInfo *TSI = FD->getTypeSourceInfo()) {
        TRY_TO(TraverseTypeLoc(TSI->getTypeLoc()));
    }

    if (CXXConstructorDecl *Ctor = dyn_cast<CXXConstructorDecl>(FD)) {
        // Constructor initializers.
        for (CXXConstructorDecl::init_iterator I = Ctor->init_begin(),
                 E = Ctor->init_end();
             I != E; ++I) {
            TRY_TO(TraverseConstructorInitializer(*I));
        }
    }

    if (FD->isThisDeclarationADefinition()) {
        TRY_TO(TraverseStmt(FD->getBody()));  // Function body.
    }

    /////////////////////////////////////////////////////////////////////
    CurrentFunction = 0;

#if 0
    if (SM.isInSystemHeader(FD->getSourceRange().getBegin()))
        return true;

    if (!SM.isFromMainFile(FD->getSourceRange().getBegin()))
        return true;
#endif

    if (FD->isMain()) {
        std::string RuntimeInit = "acl_centaurus_init();";
        Replacement R(Context->getSourceManager(),
                      FD->getBody()->getLocStart().getLocWithOffset(1),0,
                      RuntimeInit);
        applyReplacement(ReplacementPool,R);
    }

#if 0
    if (FD->getResultType()->isVoidType()) {
    }
#endif

    return true;
}

bool
Stage1_ASTVisitor::VisitReturnStmt(ReturnStmt *S) {
    if (!CurrentFunction || !CurrentFunction->isMain())
        return true;

    std::string RuntimeFinish = "acl_centaurus_finish();";

    //avoid subtle cases like:
    //    if ( ... )
    //        ReturnStmt;
    //
    //to become:
    //
    //    if ( ... )
    //        RuntimeFinish
    //    ReturnStmt;

    std::string Return;
    llvm::raw_string_ostream OS(Return);
    S->printPretty(OS,/*Helper=*/0,Context->getPrintingPolicy(),/*Indentation=*/0);
    OS.str();
    Replacement R(Context->getSourceManager(),
                  S->getLocStart(),0,
                  "{" + RuntimeFinish + Return + "}\n//");
    applyReplacement(ReplacementPool,R);

    return true;
}

static Arg *getMatchedArg(Arg *A, SmallVector<Arg*,8> &Pool, ASTContext *Context) {
    assert(!isa<SubArrayArg>(A));
    for (SmallVector<Arg*,8>::iterator
             AI = Pool.begin(), AE = Pool.end(); AI != AE; ++AI) {
        if (A->Matches(*AI))
            return *AI;
        if (SubArrayArg *SA = dyn_cast<SubArrayArg>(*AI)) {
            ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(SA->getExpr());
            ClauseInfo TmpCI(CK_IN,SA->getParent()->getAsDirective());  //the clause type here is not important
            Arg *TmpA = CreateNewArgFrom(ASE->getBase()->IgnoreParenCasts(),&TmpCI,Context);
            TmpCI.setArg(TmpA);
            if (A->Matches(TmpA)) {
                delete TmpA;
                return *AI;
            }
            else
                delete TmpA;
        }
    }
    return 0;
}

ObjRefDef addVarDeclForDevice(clang::ASTContext *Context, Expr *E,
                              clang::openacc::DirectiveInfo *DI,
                              SmallVector<Arg*,8> &PragmaArgs,
                              RegionStack &RStack, const int Index) {
    //declare a new var here for the accelerator

    E = E->IgnoreParenImpCasts();
    if (E->getType()->isConstantArrayType() ||
        E->getType()->isArrayType()) {
        llvm::outs() << WARNING
                     << "argument " << toString(Index + 1) << " '"
                     << getPrettyExpr(Context,E)
                     << "' is not heap-allocated\n";
        return ObjRefDef();
    }

    ClauseInfo TmpCI(CK_IN,DI);  //default data dependency
    Arg *TmpA = CreateNewArgFrom(E,&TmpCI,Context);
    TmpCI.setArg(TmpA);

    Arg *A = getMatchedArg(TmpA,PragmaArgs,Context);
    if (!A) {
        //not in data clause
        //therefore not a dependency (pass by value)
        A = TmpA;
        if (A->getExpr()->getType()->isPointerType()) {
            llvm::outs() << WARNING
                         << "argument " << toString(Index + 1) << " '"
                         << A->getPrettyArg()
                         << "' not found in data clauses - treat as 'in' dependency\n";
        }
    }
    else {
        delete TmpA;

#if 0
        if (RStack.FindBufferObjectInRegionStack(A)) {
            assert(0 && "Unexpected nesting");
            //abort creation of device buffer, we created it previously
            llvm::outs() << "abort new buffer (use existing) for '"
                         << A->getPrettyArg()
                         << "' in Clause '" << A->getParent()->getAsClause()->getAsString() << "'\n";
            return ObjRefDef();
        }
#endif
    }

    std::string OrigName = A->getPrettyArg();

    std::string NewName = "__accll_arg_" + toString(Index);
    //llvm::outs() << "orig name: " << OrigName << "  new name: " << NewName << "\n";

    //generate new code

    //each stage must generate a valid file to be parsed by the next stage,
    //so put a placehoder here and remove it in the next stage

    QualType Ty = A->getExpr()->getType();
    std::string SizeExpr;
    std::string Prologue;
    std::string Address = OrigName;
    std::string StartOffset = "0";

    std::string DataDepType;
    ClauseKind CK = A->getParent()->getAsClause()->getKind();
    switch (CK) {
    case CK_BUFFER:        DataDepType = "D_BUFFER";        break;
    case CK_LOCAL_BUFFER:  DataDepType = "D_LOCAL_BUFFER";  break;
    case CK_IN:            DataDepType = "D_IN";            break;
    case CK_OUT:           DataDepType = "D_OUT";           break;
    case CK_INOUT:         DataDepType = "D_INOUT";         break;
    case CK_DEVICE_IN:     DataDepType = "D_DEVICE_IN";     break;
    case CK_DEVICE_OUT:    DataDepType = "D_DEVICE_OUT";    break;
    case CK_DEVICE_INOUT:  DataDepType = "D_DEVICE_INOUT";  break;
    default:
        DataDepType = "D_PASS_BY_VALUE";
    }

#if 0
    if (A->getExpr()->getType().getUnqualifiedType().getAsString().compare("cl_mem") == 0) {
        llvm::outs() << "Found user defined low level cl_mem object '"
                     << A->getPrettyArg() << "' as '"
                     << A->getParent()->getAsClause()->getAsString() << "' data dependency.\n";

        std::string NewCode = Prologue + "struct _memory_object " + NewName + " = {"
            + ".cl_obj = " + getPrettyExpr(Context,A->getExpr())
            + ",.index = " + toString(Index)
            + ",.dependency = " + DataDepType
            + ",.host_ptr = 0"
            + ",.start_offset = 0"
            + ",.size = 0"
            + "};";

        return ObjRefDef(NewName,NewCode);
    }
#endif

    if (SubArrayArg *SA = dyn_cast<SubArrayArg>(A)) {
        ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(SA->getExpr());
        Address = getPrettyExpr(Context,ASE->getBase());
        StartOffset = getPrettyExpr(Context,ASE->getIdx());
        SizeExpr = "sizeof(" + SA->getExpr()->getType().getAsString()
            + ")*(" + getPrettyExpr(Context,SA->getLength()) + ")";
    }
    else if (Ty->isPointerType()) {
#if 0
        SizeExpr = DynSizeMap.lookup(OrigName);
        if (!SizeExpr.size()) {
            llvm::outs() << WARNING
                         << "'" << OrigName << "' pointer's data size not found automatically\n";
            llvm::outs() << NOTE
                         << "use malloc_usable_size(void *)\n";
            SizeExpr = "malloc_usable_size(" + A->getPrettyArg() + ")";
        }
#else
        SizeExpr = "malloc_usable_size(" + A->getPrettyArg() + ")";
#endif
    }
    else if (isa<ArrayArg>(A)) {
        SizeExpr = "sizeof(" + A->getExpr()->getType().getAsString() + ")";
    }
    else {
        SizeExpr = "sizeof(" + A->getExpr()->getType().getAsString() + ")";
        DataDepType = "D_PASS_BY_VALUE";
        std::string HiddenName = NewName + "__hidden__";
        Prologue = A->getExpr()->getType().getAsString() + " "
            + HiddenName + " = " + getPrettyExpr(Context,A->getExpr()) + ";";
        Address = "&" + HiddenName;
    }

    std::string NewCode = Prologue + "struct _memory_object " + NewName + " = {"
        + ".cl_obj = 0"
        + ",.index = " + toString(Index)
        + ",.dependency = " + DataDepType
        + ",.host_ptr = (void*)" + Address
        + ",.start_offset = " + StartOffset
        + ",.size = " + SizeExpr
        + "};";

    return ObjRefDef(NewName,NewCode);
}

static inline
SourceLocation getLocAfterDecl(FunctionDecl *CurrentFunction, VarDecl *VD, std::string &CreateMem) {
    //BIG FAT HACK
    //ne must fix the SourceLocation Issues. Seriously

    SourceLocation DeclLoc;
    CompoundStmt *Body = cast<CompoundStmt>(CurrentFunction->getBody());
    for (CompoundStmt::body_iterator
             IS = Body->body_begin(),
             ES = Body->body_end(); IS != ES; ++IS) {
        DeclStmt *DS = dyn_cast<DeclStmt>(*IS);
        if (!DS)
            continue;
        for (DeclStmt::decl_iterator
                 ID = DS->decl_begin(),
                 ED = DS->decl_end(); ID != ED; ++ID) {
            Decl *D = *ID;
            if (VarDecl *DVD = dyn_cast<VarDecl>(D))
                if (DVD == VD) {
                    CompoundStmt::body_iterator NextStmt = IS + 1;
                    assert(NextStmt != ES);
                    DeclLoc = (*NextStmt)->getLocStart();
                    if (isa<AccStmt>(*NextStmt)) {
                        DeclLoc = DeclLoc.getLocWithOffset(-9);
                        CreateMem += "\n";
                    }
                    return DeclLoc;
                }
        }
    }
    assert(0 && "Unexpected SourceLocation of Decl");
    return SourceLocation();
}

void DataIOSrc::init(clang::ASTContext *Context, DirectiveInfo *DI,
                     RegionStack &RStack) {
    Stmt *SubStmt = DI->getAccStmt()->getSubStmt();

    CallExpr *CE = dyn_cast<CallExpr>(SubStmt);
    assert(CE);
    if (!CE)
        return;

    if (CE->getNumArgs() == 0) {
        NameRef = "NULL";
        //Definition = "";
        NumArgs = "0";
        return;
    }

    //gather data clause arguments
    SmallVector<Arg*,8> PragmaArgs;
    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator
             II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        ClauseInfo *CI = *II;
        if (!CI->isDataClause())
            continue;
        for (ArgVector::iterator
                 AI = CI->getArgs().begin(), AE = CI->getArgs().end(); AI != AE; ++AI) {
            Arg *A = *AI;
            if (!isa<SubArrayArg>(*AI) && !isa<VarArg>(A)) {
                llvm::outs() << WARNING
                             << "[INTERNAL ERORR] invalid '" << (A)->getParent()->getAsClause()->getAsString()
                             << "' data dependency for pass-by-value argument '"
                             << (A)->getPrettyArg() << "' (" << (A)->getKindAsString() << ")\n";
                assert(0);
                continue;
            }
            PragmaArgs.push_back(A);
        }
    }

#if 0
    SmallVector<Expr*,8> PtrArgs;
    for (CallExpr::arg_iterator II = CE->arg_begin(), EE = CE->arg_end(); II != EE; ++II)
        if ((*II)->getType()->isPointerType())
            PtrArgs.push_back(*II);
    if (PtrArgs.size() != PragmaArgs.size()) {
        NameRef = "NULL";
        //Definition = "";
        NumArgs = "0";
        llvm::outs() << ERROR
                     << "number of pass-by-reference function arguments (" << PtrArgs.size()
                     << ") and dependency data clause arguments (" << PragmaArgs.size()
                     << ") mismatch\n";
        return;
    }
#endif

    //generate code
    NumArgs = toString(CE->getNumArgs());
    std::string Prologue;
    std::string InitList;

    int Index = 0;
    for (CallExpr::arg_iterator II(CE->arg_begin()),EE(CE->arg_end()); II != EE; ++II) {
        ObjRefDef MemObj = addVarDeclForDevice(Context,*II,DI,PragmaArgs,RStack,Index++);
        Prologue += MemObj.Definition;
        if (II != CE->arg_begin())
            InitList += ",";
        InitList += MemObj.NameRef;
    }

    NameRef = "__accll_kernel_args";
    Definition = Prologue
        + "struct _memory_object " + NameRef + "[" + NumArgs + "] = {" + InitList + "};";
}

static void updateNestedSubtasks(ASTContext *C, CallGraph *CG) {
    bool repeat = true;
    while (repeat) {
        repeat = false;
        CallGraphNode *Root = CG->getRoot();
        for (CallGraphNode::const_iterator CI = Root->begin(),
                 CE = Root->end(); CI != CE; ++CI) {
            FunctionDecl *CurrentFD = dyn_cast<FunctionDecl>((*CI)->getDecl());
            if (C->isFunctionWithSubtasks(CurrentFD))
                continue;

            CallGraphNode *CurrentNode = CG->getNode(CurrentFD);
            if (!CurrentNode)
                continue;
            for (CallGraphNode::iterator
                     NI = CurrentNode->begin(), NE = CurrentNode->end(); NI != NE; ++NI) {
                FunctionDecl *NewFD = dyn_cast<FunctionDecl>((*NI)->getDecl());
                if (NewFD == CurrentFD)
                    continue;
                if (C->isOpenCLKernel(NewFD))
                    continue;
                if (!C->isFunctionWithSubtasks(NewFD))
                    continue;
                C->markAsFunctionWithSubtasks(CurrentFD);
                repeat = true;
            }
        }
    }
}

void Stage1_ASTVisitor::Init(ASTContext *C, CallGraph *_CG) {
    ACLConfig = Config;

    Context = C;
    CG = _CG;
    updateNestedSubtasks(C,CG);
    //CG->dump();

    APIHeaderVector.push_back("CL/centaurus_cl_platform.h");
    APIHeaderVector.push_back("centaurus_common.h");
    APIHeaderVector.push_back("__acl_api_types.h");
    APIHeaderVector.push_back("malloc.h");

    SourceManager &SM = Context->getSourceManager();
    std::string FileName = SM.getFileEntryForID(SM.getMainFileID())->getName();

    Suffix = "_ocl";
    CommonFileHeader = "/* Generated by accll */\n";
    NewHeader = RemoveDotExtension(FileName) + Suffix + ".h";
    HostHeader = CommonFileHeader;  // + "#include \"" + NewHeader + "\"\n";

    HostHeader += "#define __kernel \n";
    HostHeader += "#define __global \n";
    HostHeader += "#define __local \n";
    HostHeader += "#define __constant \n";
    HostHeader += "#define __private \n";

    for (std::vector<std::string>::iterator
             II = APIHeaderVector.begin(), EE = APIHeaderVector.end(); II != EE; ++II)
        HostHeader += "#include <" + *II + ">\n";

#if 0
    for (llvm::SmallPtrSet<clang::FunctionDecl *, 32>::iterator
             II = Context->getKernelFunctions().begin(),
             EE = Context->getKernelFunctions().end(); II != EE; ++II) {
        llvm::outs() << DEBUG << (*II)->getNameAsString() << " <-- kernel\n";
    }
    for (llvm::SmallPtrSet<clang::FunctionDecl *, 32>::iterator
             II = Context->getFunctionsWithSubtasks().begin(),
             EE = Context->getFunctionsWithSubtasks().end(); II != EE; ++II) {
        llvm::outs() << DEBUG << (*II)->getNameAsString() << " <-- has subtasks\n";
    }
#endif
}

void KernelRefDef::findCallDeps(FunctionDecl *StartFD, CallGraph *CG,
                                llvm::SmallSetVector<clang::FunctionDecl *,sizeof(clang::FunctionDecl *)> &Deps) {
    llvm::SmallPtrSet<clang::FunctionDecl *,8> WorkList;
    llvm::SmallPtrSet<clang::FunctionDecl *,8> VisitedList;

    WorkList.insert(StartFD);

    while (WorkList.size()) {
        FunctionDecl *CurrentFD = *WorkList.begin();
        WorkList.erase(CurrentFD);
        VisitedList.insert(CurrentFD);
        CallGraphNode *CurrentNode = CG->getNode(CurrentFD);
        if (!CurrentNode)
            continue;
        for (CallGraphNode::iterator
                 NI = CurrentNode->begin(), NE = CurrentNode->end(); NI != NE; ++NI)
            if (FunctionDecl *NewFD = dyn_cast<FunctionDecl>((*NI)->getDecl()))
                if (!VisitedList.count(NewFD) && NewFD != CurrentFD) {
                    Deps.insert(NewFD);
                    WorkList.insert(NewFD);
                }
    }
}

void
Stage1_ASTVisitor::Finish() {
    SourceManager &SM = Context->getSourceManager();

    for (SmallVector<SourceLocation,8>::iterator
             II = SM.OpenCLIncludeDirectives.begin(),
             EE = SM.OpenCLIncludeDirectives.end(); II != EE; ++II) {
        SourceLocation Loc = *II;
        Replacement R(SM,Loc,0,"//");
        applyReplacement(ReplacementPool,R);
    }

    if (!TaskSrc::TaskUID) {
        SourceLocation StartLocOfMainFile = SM.getLocForStartOfFile(SM.getMainFileID());
        Replacement R(SM,StartLocOfMainFile,0,HostHeader);
        applyReplacement(ReplacementPool,R);
        return;
    }

    std::string FileName = SM.getFileEntryForID(SM.getMainFileID())->getName();

    {
        std::string Headers = HostHeader + "#include \"" + NewHeader + "\"\n";
        SourceLocation StartLocOfMainFile = SM.getLocForStartOfFile(SM.getMainFileID());
        Replacement R(SM,StartLocOfMainFile,0,Headers);
        applyReplacement(ReplacementPool,R);
    }

    {
        std::ofstream dst(NewHeader.c_str());
        dst << CommonFileHeader;
        for (llvm::DenseMap<FunctionDecl *,KernelRefDef *>::iterator
                 II = KernelAccuratePool.begin(), EE = KernelAccuratePool.end(); II != EE; ++II) {
            dst << II->second->InlineDeviceCode.HeaderDecl;
            std::vector<PlatformBin> &Platforms = II->second->Binary;
            for (std::vector<PlatformBin>::iterator
                     BI = Platforms.begin(), BE = Platforms.end(); BI != BE; ++BI) {
                PlatformBin &Platform = *BI;
                for (std::vector<DeviceBin>::iterator
                         DI = Platform.begin(), DE = Platform.end(); DI != DE; ++DI) {
                    DeviceBin &Device = *DI;
                    dst << Device.Bin.HeaderDecl;
                }
            }
        }
        for (llvm::DenseMap<FunctionDecl *,KernelRefDef *>::iterator
                 II = KernelApproximatePool.begin(), EE = KernelApproximatePool.end(); II != EE; ++II) {
            dst << II->second->InlineDeviceCode.HeaderDecl;
            std::vector<PlatformBin> &Platforms = II->second->Binary;
            for (std::vector<PlatformBin>::iterator
                     BI = Platforms.begin(), BE = Platforms.end(); BI != BE; ++BI) {
                PlatformBin &Platform = *BI;
                for (std::vector<DeviceBin>::iterator
                         DI = Platform.begin(), DE = Platform.end(); DI != DE; ++DI) {
                    DeviceBin &Device = *DI;
                    dst << Device.Bin.HeaderDecl;
                }
            }
        }
        for (llvm::DenseMap<FunctionDecl *,KernelRefDef *>::iterator
                 II = KernelEvaluatePool.begin(), EE = KernelEvaluatePool.end(); II != EE; ++II) {
            dst << II->second->InlineDeviceCode.HeaderDecl;
            std::vector<PlatformBin> &Platforms = II->second->Binary;
            for (std::vector<PlatformBin>::iterator
                     BI = Platforms.begin(), BE = Platforms.end(); BI != BE; ++BI) {
                PlatformBin &Platform = *BI;
                for (std::vector<DeviceBin>::iterator
                         DI = Platform.begin(), DE = Platform.end(); DI != DE; ++DI) {
                    DeviceBin &Device = *DI;
                    dst << Device.Bin.HeaderDecl;
                }
            }
        }
        dst << "\n";
        dst.flush();
    }
    InputFiles.push_back(NewHeader);
    llvm::outs() << "Create header           : '" << NewHeader << "'  -  new file\n";

    std::string NewImpl = RemoveDotExtension(FileName) + Suffix + ".c";
    {
        std::ofstream dst(NewImpl.c_str());
        dst << CommonFileHeader;
        //dst << "#include \"" << NewHeader << "\"\n";
        for (llvm::DenseMap<FunctionDecl *,KernelRefDef *>::iterator
                 II = KernelAccuratePool.begin(), EE = KernelAccuratePool.end(); II != EE; ++II) {
            dst << II->second->InlineDeviceCode.Definition;
            std::vector<PlatformBin> &Platforms = II->second->Binary;
            for (std::vector<PlatformBin>::iterator
                     BI = Platforms.begin(), BE = Platforms.end(); BI != BE; ++BI) {
                PlatformBin &Platform = *BI;
                for (std::vector<DeviceBin>::iterator
                         DI = Platform.begin(), DE = Platform.end(); DI != DE; ++DI) {
                    DeviceBin &Device = *DI;
                    dst << Device.Bin.Definition;
                }
            }
        }
        for (llvm::DenseMap<FunctionDecl *,KernelRefDef *>::iterator
                 II = KernelApproximatePool.begin(), EE = KernelApproximatePool.end(); II != EE; ++II) {
            dst << II->second->InlineDeviceCode.Definition;
            std::vector<PlatformBin> &Platforms = II->second->Binary;
            for (std::vector<PlatformBin>::iterator
                     BI = Platforms.begin(), BE = Platforms.end(); BI != BE; ++BI) {
                PlatformBin &Platform = *BI;
                for (std::vector<DeviceBin>::iterator
                         DI = Platform.begin(), DE = Platform.end(); DI != DE; ++DI) {
                    DeviceBin &Device = *DI;
                    dst << Device.Bin.Definition;
                }
            }
        }
        for (llvm::DenseMap<FunctionDecl *,KernelRefDef *>::iterator
                 II = KernelEvaluatePool.begin(), EE = KernelEvaluatePool.end(); II != EE; ++II) {
            dst << II->second->InlineDeviceCode.Definition;
            std::vector<PlatformBin> &Platforms = II->second->Binary;
            for (std::vector<PlatformBin>::iterator
                     BI = Platforms.begin(), BE = Platforms.end(); BI != BE; ++BI) {
                PlatformBin &Platform = *BI;
                for (std::vector<DeviceBin>::iterator
                         DI = Platform.begin(), DE = Platform.end(); DI != DE; ++DI) {
                    DeviceBin &Device = *DI;
                    dst << Device.Bin.Definition;
                }
            }
        }
        dst << "\n";
        dst.flush();
    }
    InputFiles.push_back(NewImpl);
    llvm::outs() << "Create kernel src/bin   : '" << NewImpl << "'  -  new file\n";

    std::string NewDeviceImpl = RemoveDotExtension(FileName) + Suffix + ".cl";
    {
        std::ofstream dst(NewDeviceImpl.c_str());
        dst << KernelHeader << OpenCLExtensions;
        for (StringMap<bool>::iterator
                 II = DepHeaders.begin(), EE = DepHeaders.end(); II != EE; ++II) {
            dst << "#include \"" + II->getKey().str() + "\"\n";
        }
        dst << UserTypes;
        for (llvm::DenseMap<FunctionDecl *, std::string>::iterator
                 II = CommonFunctionsPool.begin(), EE = CommonFunctionsPool.end(); II != EE; ++II) {
            if (SM.isFromMainFile((*II).first->getLocStart()))
                dst << (*II).second;
        }
        for (llvm::DenseMap<FunctionDecl *,KernelRefDef *>::iterator
                 II = KernelAccuratePool.begin(), EE = KernelAccuratePool.end(); II != EE; ++II) {
            if (Context->isFunctionWithSubtasks((*II).first) ||
                 SM.isFromMainFile((*II).first->getLocStart()))
                dst << (*II).second->DeviceCode.Definition;
        }
        for (llvm::DenseMap<FunctionDecl *,KernelRefDef *>::iterator
                 II = KernelApproximatePool.begin(), EE = KernelApproximatePool.end(); II != EE; ++II) {
            if (Context->isFunctionWithSubtasks((*II).first) ||
                 SM.isFromMainFile((*II).first->getLocStart()))
                dst << (*II).second->DeviceCode.Definition;
        }
        for (llvm::DenseMap<FunctionDecl *,KernelRefDef *>::iterator
                 II = KernelEvaluatePool.begin(), EE = KernelEvaluatePool.end(); II != EE; ++II) {
            if (Context->isFunctionWithSubtasks((*II).first) ||
                 SM.isFromMainFile((*II).first->getLocStart()))
                dst << (*II).second->DeviceCode.Definition;
        }
        dst.flush();
    }
    KernelFiles.push_back(NewDeviceImpl);
    llvm::outs() << "Write OpenCL kernels to : '" << NewDeviceImpl << "'  -  new file\n";

    {
        for (llvm::DenseMap<FunctionDecl *,KernelRefDef *>::iterator
                 II = KernelAccuratePool.begin(), EE = KernelAccuratePool.end(); II != EE; ++II) {
            delete (*II).second;
        }
        KernelAccuratePool.clear();
        for (llvm::DenseMap<FunctionDecl *,KernelRefDef *>::iterator
                 II = KernelApproximatePool.begin(), EE = KernelApproximatePool.end(); II != EE; ++II) {
            delete (*II).second;
        }
        KernelApproximatePool.clear();
        for (llvm::DenseMap<FunctionDecl *,KernelRefDef *>::iterator
                 II = KernelEvaluatePool.begin(), EE = KernelEvaluatePool.end(); II != EE; ++II) {
            delete (*II).second;
        }
        KernelEvaluatePool.clear();
    }

    for (SmallPtrSet<FunctionDecl*,32>::iterator
             II = EraseFunctionDeclPool.begin(),
             EE = EraseFunctionDeclPool.end(); II != EE; ++II) {
        if (!SM.isFromMainFile((*II)->getLocStart()))
            continue;

        Replacement R1(SM,(*II)->getLocStart(),0,"\n#if 0\n");
        applyReplacement(ReplacementPool,R1);

        Replacement R2(SM,(*II)->getLocEnd().getLocWithOffset(1),0,"\n#endif\n");
        applyReplacement(ReplacementPool,R2);
    }
    EraseFunctionDeclPool.clear();

    APIHeaderVector.clear();
    CommonFunctionsPool.clear();
    DepHeaders.clear();

    ACLConfig = accll::CentaurusConfig();
}

bool
Stage1_ASTVisitor::VisitVarDecl(VarDecl *VD) {
    if (!CurrentFunction || !Context->isOpenCLKernel(CurrentFunction))
        return true;

    const Type *Ty = (cast<ValueDecl>(VD))->getType().getTypePtr();

    std::string DeclStr;
    raw_string_ostream OS(DeclStr);
    SourceLocation Loc;
    if (const TagType *TT = Ty->getAs<TagType>()) {
        const TagDecl *Tag = cast<TagDecl>(TT->getDecl());
        Tag->print(OS);
        Loc = Tag->getSourceRange().getBegin();
    }
    else if (const TypedefType *TT = dyn_cast<TypedefType>(Ty)) {
        TT->getDecl()->print(OS);
        Loc = TT->getDecl()->getSourceRange().getBegin();
    }
    else
        return true;

    OS.str();

    if (Loc.isInvalid())
        return true;

    assert(!Loc.isInvalid());

    SourceManager &SM = Context->getSourceManager();
    if (SM.isInSystemHeader(Loc))
        return true;

#if 0
    PresumedLoc PLoc = SM.getPresumedLoc(Loc);
    if (PLoc.isInvalid())
        return true;
    llvm::outs() << DEBUG
                 << "inside kernel '" << CurrentFunction->getNameAsString()
                 << "' from symbol definition '" << VD->getNameAsString() << "'\n       "
                 << "found type '" << (cast<ValueDecl>(VD))->getType().getAsString() << "' defined at "
                 << GetBasename(PLoc.getFilename()) << ":" << PLoc.getLine() << "\n";
#endif

    std::string DefFile = SM.getFileEntryForID(SM.getFileID(Loc))->getName();
    //llvm::outs() << DEBUG
    //             << "user defined record '" << TT->getNameAsString() << "' from file :" << DefFile << "\n";

    //maybe the implementation headers are not in system directories
    if (!SM.isFromMainFile(Loc)) {
        if (TrackThisHeader(DefFile)) {
            llvm::outs() << DEBUG
                         << VD->getNameAsString() << " ------------------- " << DefFile << "\n";
            DepHeaders[DefFile] = true;
        }
    }
    else
        UserTypes += DeclStr + ";\n\n";

    return true;
}

bool
Stage1_ASTVisitor::VisitCallExpr(CallExpr *CE) {
    if (!CurrentFunction)
        return true;

    FunctionDecl *FD = CE->getDirectCallee();
    std::string Name = FD->getNameAsString();

    SourceManager &SM = Context->getSourceManager();

    if (Name.compare("free") == 0) {
        SourceLocation Loc = CE->getLocStart();
        std::string NewCode = "acl_garbage_collect(" + getPrettyExpr(Context,CE->getArg(0)) + ");";
        Replacement R(SM,Loc,0,NewCode);
        applyReplacement(ReplacementPool,R);
        return true;
    }

    SourceLocation Loc = FD->getSourceRange().getBegin();
    assert(!Loc.isInvalid());
    if (Loc.isInvalid())
        return true;

    if (SM.isInSystemHeader(Loc))
        return true;

    if (!Context->isOpenCLKernel(CurrentFunction)) {
        return true;
    }

    // inside kernel function

#if 0
    PresumedLoc PLoc = SM.getPresumedLoc(Loc);
    if (PLoc.isInvalid())
        return true;

    llvm::outs() << DEBUG
                 << "inside kernel '" << CurrentFunction->getNameAsString()
                 << "' function call to '" << FD->getNameAsString() << "'\n       "
                 << GetBasename(PLoc.getFilename()) << ":" << PLoc.getLine() << "\n";
#endif

    const FileEntry *FE = SM.getFileEntryForID(SM.getFileID(Loc));
    if (!FE) {
        // this happens when we include files through the cmd (-include option)
        // if that happens, we already have the header's declarations

        //llvm::outs() << DEBUG
        //             << "implicit declaration of function '" << FD->getNameAsString() << "'\n";
        return true;
    }
    std::string DefFile = FE->getName();
    //llvm::outs() << DEBUG
    //             << "user defined record '" << R->getNameAsString() << "' from file :" << DefFile << "\n";

    //maybe the implementation headers are not in system directories
    if (!SM.isFromMainFile(Loc)) {
        if (TrackThisHeader(DefFile))
            DepHeaders[DefFile] = true;
    }
#if 0
    // we get them through call graph

    else {
        std::string DeclStr;
        raw_string_ostream OS(DeclStr);
        FD->print(OS,Context->getPrintingPolicy());
        OS.str();
        UserTypes += DeclStr + ";\n\n";
    }
#endif

    return true;
}

#if 0
struct UniqueArgVector : ArgVector {
    bool isUnique(Arg *Target) const {
        for (const_iterator II = begin(), EE = end(); II != EE; ++II)
            if (Target->Matches(*II))
                return false;
        return true;
    }
};
#endif
