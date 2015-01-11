#include "Stages.hpp"
#include "Common.hpp"

#include <iostream>
#include <fstream>
#include <sstream>

#include "llvm/ADT/SmallVector.h"

using namespace llvm;
using namespace clang;
using namespace clang::tooling;
using namespace clang::openacc;
using namespace accll;

static
void EmitCodeForDataClause(clang::ASTContext *Context,
                           clang::openacc::DirectiveInfo *DI,
                           clang::openacc::ClauseInfo *CI,
                           clang::Stmt *SubStmt,
                           llvm::SmallVector<ObjRefDef,8> &init_list,
                           NameMap &Map,
                           RegionStack &RStack);

struct GeometrySrc : ObjRefDef {
private:
    void init(DirectiveInfo *DI, clang::ASTContext *Context);

public:
    GeometrySrc(DirectiveInfo *DI, clang::ASTContext *Context) : ObjRefDef() {
        init(DI,Context);
    }
};

struct TaskSrc {
    clang::ASTContext *Context;
    clang::openacc::RegionStack &RStack;

    std::string runtime_call;

    std::string Approx;              //int

    DataIOSrc MemObjInfo;
    ObjRefDef Newkernel;

    std::string kernel;              //const char *
    std::string kernel_accurate;     //const char *
    std::string kernel_approximate;  //const char *

    GeometrySrc Geometry;

    std::string Label;               //const char *
    std::string source_size;         //size_t

    static int KernelCalls;

    TaskSrc(clang::ASTContext *Context, std::string _call, DirectiveInfo *DI,
            NameMap &Map, clang::openacc::RegionStack &RStack,
            clang::tooling::Replacements &ReplacementPool) :
        Context(Context), RStack(RStack),
        runtime_call(_call),
        MemObjInfo(Context,DI,Map,RStack),
        Geometry(DI,Context)
    {
        init(Context,DI);
    }

    std::string HostCall();
    std::string KernelCode();

private:
    void init(clang::ASTContext *Context,DirectiveInfo *DI);
    ObjRefDef CreateKernel(clang::openacc::DirectiveInfo *DI,
                           clang::Stmt *SubStmt);
    std::string MakeParams(clang::openacc::DirectiveInfo *DI,
                           bool MakeDefinition, std::string Kernels,
                           std::string &CleanupCode, int &ArgNum);
    std::string MakeParamFromArg(clang::openacc::Arg *A, bool MakeDefinition,
                                 std::string Kernel, int &ArgPos,
                                 std::string &CleanupCode);
    std::string MakeBody(clang::openacc::DirectiveInfo *DI, clang::Stmt *SubStmt);

    std::string getTaskLabel(DirectiveInfo *DI) {
        ClauseList &CList = DI->getClauseList();
        for (ClauseList::iterator
                 II = CList.begin(), EE = CList.end(); II != EE; ++II)
            if ((*II)->getKind() == CK_LABEL)
                return cast<LabelArg>((*II)->getArg())->getLabel().str();
        return "NULL";
    }

    std::string getTaskApprox(clang::ASTContext *Context, DirectiveInfo *DI) {
        ClauseList &CList = DI->getClauseList();
        for (ClauseList::iterator
                 II = CList.begin(), EE = CList.end(); II != EE; ++II)
            if ((*II)->getKind() == CK_SIGNIFICANT)
                return (*II)->getArg()->getPrettyArg(Context->getPrintingPolicy());
#warning FIXME:  default task significance
        return "0";
    }
};

int TaskSrc::KernelCalls = 0;

void TaskSrc::init(clang::ASTContext *Context, DirectiveInfo *DI) {
    Label = getTaskLabel(DI);
    Approx = getTaskApprox(Context,DI);

    //generate data moves
    Stmt *SubStmt = DI->getAccStmt()->getSubStmt();
    Newkernel = CreateKernel(DI,SubStmt);


}

std::string TaskSrc::HostCall() {
    /*
    int num_in;
    struct _memory_object * inputs;
    int num_out;
    struct _memory_object * outputs;


    struct geom geometry;
    */

    std::string call = runtime_call + "("
        + Approx + ","
        + MemObjInfo.NumOfInMemObj + "," + MemObjInfo.InRef + ","
        + MemObjInfo.NumOfOutMemObj + "," + MemObjInfo.OutRef + ","
        + "NULL" + ",\"" + Newkernel.name_ref + "\"," + "NULL" + ","
        + Geometry.name_ref + ","
        + "\"" + Label + "\","
        + "/*FIXME:  source_size*/ 0"
        + ");";
    return call;
}

std::string TaskSrc::KernelCode() {
    return Newkernel.definition;
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
        global_init_list += (*IA)->getPrettyArg(Context->getPrintingPolicy());
        if ((*IA) != Groups->getArgs().back())
            global_init_list += ",";
    }

    std::string local_init_list;
    for (ArgVector::iterator
             IA = Workers->getArgs().begin(), EA = Workers->getArgs().end(); IA != EA; ++IA) {
        local_init_list += (*IA)->getPrettyArg(Context->getPrintingPolicy());
        if ((*IA) != Workers->getArgs().back())
            local_init_list += ",";
    }

    std::string pre_global_code = "int __accll_geometry_global[" + Dims + "] = {" + global_init_list + "};";
    std::string pre_local_code = "int __accll_geometry_local[" + Dims + "] = {" + local_init_list + "};";

    name_ref = "__task_geometry";
    definition = pre_global_code + pre_local_code
        + "struct _geometry " + name_ref + " = {"
        //definition += name_ref
        + ".dimensions = " + Dims + ","
        //definition += name_ref
        + ".global = __accll_geometry_global,"
        //definition += name_ref
        + ".local = __accll_geometry_local };";
}

static bool isRuntimeCall(const std::string Name) {
    std::string data[] = {"acc_create_task","acc_taskwait"};
    static const SmallVector<std::string,8> RuntimeCalls(data, data + sizeof(data)/sizeof(std::string));

    for (SmallVector<std::string,8>::const_iterator II = RuntimeCalls.begin(), EE = RuntimeCalls.end();
             II != EE; ++II)
        if (Name.compare(*II) == 0)
            return true;
    return false;
}

static std::string getUniqueKernelName(const std::string Base) {
    static int UID = 0;  //unique kernel identifier
    std::string ID;
    raw_string_ostream OS(ID);
    OS << Base << "_" << UID;
    UID++;
    return OS.str();
}

#if 1
std::string accll::KernelHeader =
    "/* Generated by accll */\n\n\n";
#else
std::string accll::KernelHeader =
    "\n/* Generated by accll */\n\n\
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
std::string
Stage0_ASTVisitor::RemoveDotExtension(const std::string &filename) {
    size_t lastdot = filename.find_last_of(".");
    if (lastdot == std::string::npos) return filename;
    return filename.substr(0, lastdot);
}

std::string
Stage0_ASTVisitor::GetDotExtension(const std::string &filename) {
    size_t lastdot = filename.find_last_of(".");
    if (lastdot == std::string::npos) return "";
    return filename.substr(lastdot,std::string::npos-1);
}

bool
Stage0_ASTVisitor::VisitAccStmt(AccStmt *ACC) {
    hasDirectives = true;
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

void
Stage0_ASTVisitor::Finish(ASTContext *Context) {
    SourceManager &SM = Context->getSourceManager();
    std::string FileName = SM.getFileEntryForID(SM.getMainFileID())->getName();
    llvm::outs() << "Found                   : '" << FileName << "'\n";

    if (!hasDirectives && !hasRuntimeCalls) {
        llvm::outs() << "debug: skip file without OpenACC Directives or Runtime Calls: '"
                     << FileName << "'\n";
        return;
    }

    std::string Suffix("_accll");
    std::string Ext = GetDotExtension(FileName);
    std::string NewFile = RemoveDotExtension(FileName);
    NewFile += Suffix;
    NewFile += Ext;

    //Create new C file

    std::string NewFileHeader =
        "/* Generated by accll */\n\n#include <__accll.h>\n\n";

    std::ifstream src(FileName.c_str());
    std::ofstream dst(NewFile.c_str());
    dst << NewFileHeader.c_str();
    dst << src.rdbuf();
    dst.flush();

    InputFiles.push_back(NewFile);
    llvm::outs() << "Rewrite to              : '" << NewFile << "'  -  new file\n";

    if (!hasDeviceCode) {
        llvm::outs() << "No OpenCL Kernels found : skip creation of '*.cl' file\n";
        std::string Empty;
        KernelFiles.push_back(Empty);
        return;
    }

    //Create new OpenCL file ("*.cl")

    size_t lastdot = FileName.find_last_of(".");
    assert(lastdot != std::string::npos && "unexpected filepath");
    std::string NewKernels = FileName.substr(0,lastdot) + Suffix + ".cl";

    //create a new empty file to write the OpenCL kernels
    std::ofstream NewKernelDst(NewKernels.c_str());
    NewKernelDst << KernelHeader.c_str();
    NewKernelDst.flush();

    KernelFiles.push_back(NewKernels);
    llvm::outs() << "Write OpenCL kernels to : '" << NewKernels
                 << "'  -  new file\n";
}

static std::string CreateNewNameFor(clang::ASTContext *Context, Arg *A) {
    std::string NewName("__accll_");  //prefix
    NewName += A->getPrettyArg(PrintingPolicy(Context->getLangOpts()));
    ReplaceStringInPlace(NewName,"->","_");  //delimiter
    ReplaceStringInPlace(NewName,".","_");  //delimiter
    ReplaceStringInPlace(NewName," ","_");  //delimiter
    if (isa<ArrayElementArg>(A)) {
        ReplaceStringInPlace(NewName,"[","_");  //delimiter
        ReplaceStringInPlace(NewName,"]","_");  //delimiter
    }
    if (isa<SubArrayArg>(A)) {
        ReplaceStringInPlace(NewName,"[","_");  //delimiter
        ReplaceStringInPlace(NewName,"]","_");  //delimiter
        ReplaceStringInPlace(NewName,":","_");  //delimiter

        //Length may be subtraction expression
        ReplaceStringInPlace(NewName,"-","_");  //delimiter
    }
    return NewName;
}

static std::string getOrigNameFor(clang::ASTContext *Context,Arg *A) {
    std::string OrigName = A->getPrettyArg(PrintingPolicy(Context->getLangOpts()));
    return OrigName;
}

static std::string getNewNameFor(NameMap &Map, Arg *A) {
    assert(A && "Passed null params");
    NameMap::iterator I = Map.find(A);
    assert(I != Map.end() && "unexpected empty name");
    ArgNames Names = I->second;
    return Names.second;
}

///////////////////////////////////////////////////////////////////////////////
//                        Stage1
///////////////////////////////////////////////////////////////////////////////

std::string
Stage1_ASTVisitor::CreateNewUniqueEventNameFor(Arg *A) {
    static unsigned EID = 0;

    std::string NewName("__accll_update_event_");  //prefix

    //add variable name to event name to correctly separate and identify
    //async events without Arg or with RawExprArg ICE

    if (A) {
        std::string AStr;
        if (A->isICE()) {
            NewName += "const_";
            AStr = A->getICE().toString(10);
        }
        else {
            AStr = A->getPrettyArg(PrintingPolicy(Context->getLangOpts()));
            ReplaceStringInPlace(AStr,"->","_");  //delimiter
            ReplaceStringInPlace(AStr,".","_");   //delimiter
        }
        if (isa<ArrayElementArg>(A)) {
            ReplaceStringInPlace(AStr,"[","_");  //delimiter
            ReplaceStringInPlace(AStr,"]","_");  //delimiter
        }
        NewName += AStr;
    }
    else
        NewName += "implicit_";

    raw_string_ostream OS(NewName);
    OS << "_" << EID;
    EID++;

    return OS.str();
}

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

Arg*
Stage1_ASTVisitor::EmitImplicitDataMoveCodeFor(Expr *E) {
    assert(E && "expected expression");

    ClauseKind CK = CK_IN;
#if 0
    if (E->getType()->isAggregateType() ||
        Context->getAsArrayType(E->getType()))
        CK = CK_IN;
#endif

    DirectiveInfo *DI = RStack.back();
    assert(DI);

    ClauseInfo *CI = new ClauseInfo(CK,DI);
    Arg *A = CreateNewArgFrom(E,CI,Context);

    //next time we find it through FindVisibleCopyInRegionStack()
    //and we just replace the new name, without reemitting this code
    CI->setArg(A);
    DI->getClauseList().push_back(CI);

    SmallVector<ObjRefDef,8> init_list;
#warning FIXME: implement me
    EmitCodeForDataClause(Context,DI,CI,DI->getAccStmt()->getSubStmt(),init_list,Map,RStack);
    return A;
}

bool
Stage1_ASTVisitor::Rename(Expr *E) {
    assert(E && "null Expr");

    DirectiveInfo *DI = RStack.back();
    assert(DI && DI->getKind() == DK_TASK);

    Expr *BaseExpr = E;
    bool NeedNewName = true;

    ClauseInfo *ImplicitCI = new ClauseInfo(CK_IN,DI);
    Arg *Target = CreateNewArgFrom(E,ImplicitCI,Context);
    assert(!isa<RawExprArg>(Target));
    Arg *A = RStack.FindVisibleCopyInRegionStack(Target);
    //search for create Clauses from previous regions
    Arg *AExplicitBuffer = RStack.FindBufferObjectInRegionStack(Target);
    if (!A) {
        llvm::outs() << getPrettyExpr(Context,BaseExpr)
                     << ": searching for explicit device copy ... ";
        A = AExplicitBuffer;
        if (A) {
            llvm::outs() << "FOUND.\n";
            NeedNewName = false;
        }
        else
            llvm::outs() << "NOT FOUND.\n";
    }

    //Args from a host_data Directive and use_device Clause always have
    //a Visible Copy

    if (!A) {
        //not found
        //if this is an ArraySubscriptExpr with non constant index,
        //move the whole BaseExpr to device

        while (ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(BaseExpr)) {
            if (ASE->getIdx()->IgnoreParenCasts()->isIntegerConstantExpr(*Context))
                break;
            BaseExpr = ASE->getBase()->IgnoreParenCasts();
        }

        if (E != BaseExpr){
            //shadow the function global Target
            Arg *Target = CreateNewArgFrom(BaseExpr,ImplicitCI,Context);
            assert(!Target->isICE());
            assert(!isa<ArrayElementArg>(Target));
            A = RStack.FindVisibleCopyInRegionStack(Target);
            //search for create Clauses from previous regions
            AExplicitBuffer = RStack.FindBufferObjectInRegionStack(Target);
            if (!A) {
                llvm::outs() << getPrettyExpr(Context,BaseExpr)
                             << ": searching for explicit device copy ... ";
                A = AExplicitBuffer;
                if (A) {
                    llvm::outs() << "FOUND.\n";
                    NeedNewName = false;
                }
                else
                    llvm::outs() << "NOT FOUND.\n";
            }
            if (!A) {
                A = EmitImplicitDataMoveCodeFor(BaseExpr);
                llvm::outs() << "debug: implicit move for '"
                             << getPrettyExpr(Context,BaseExpr)
                             << "' (move the whole array) due to '"
                             << getPrettyExpr(Context,E) << "'\n";
            }
            else {
                ClauseInfo *CI = A->getParent()->getAsClause();
                assert(CI && CI->isDataClause());
                DirectiveInfo *ParentDI = CI->getParentDirective();
                if (!CI->isImplDefault() && ParentDI != DI) {
                    ImplicitCI->getArgs().push_back(Target);
                    DI->getClauseList().push_back(ImplicitCI);

                    if (NeedNewName) {
                        std::string OrigName = getOrigNameFor(Context,Target);
                        std::string NewName = CreateNewNameFor(Context,Target);
                        Map[Target] = ArgNames(OrigName,NewName);
                    }

                    llvm::outs() << "debug: mark Arg '"
                                 << A->getPrettyArg(Context->getPrintingPolicy())
                                 << "' in directive '" << ParentDI->getAsString()
                                 << "' as present in directive '"
                                 << DI->getAsString() << "'\n";
                }
            }
        }
        else {
            if (ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(BaseExpr)) {
                assert(ASE->getIdx()->IgnoreParenCasts()->isIntegerConstantExpr(*Context));
                Expr *TmpBaseExpr = ASE->getBase()->IgnoreParenCasts();

                //shadow the function global Target
                Arg *Target = CreateNewArgFrom(TmpBaseExpr,ImplicitCI,Context);
                assert(!Target->isICE());
                assert(!isa<ArrayElementArg>(Target));
                A = RStack.FindVisibleCopyInRegionStack(Target);
                //search for create Clauses from previous regions
                AExplicitBuffer = RStack.FindBufferObjectInRegionStack(Target);
                if (!A) {
                    llvm::outs() << getPrettyExpr(Context,BaseExpr)
                                 << ": searching for explicit device copy ... ";
                    A = AExplicitBuffer;
                    if (A) {
                        llvm::outs() << "FOUND.\n";
                        NeedNewName = false;
                    }
                    else
                        llvm::outs() << "NOT FOUND.\n";
                }
                if (!A) {
                    //move the original Expr, because the Idx is constant
                    A = EmitImplicitDataMoveCodeFor(E);
                    llvm::outs() << "debug: implicit move for '"
                                 << getPrettyExpr(Context,E) << "' \n";
                }
                else {
                    ClauseInfo *CI = A->getParent()->getAsClause();
                    assert(CI && CI->isDataClause());
                    DirectiveInfo *ParentDI = CI->getParentDirective();
                    if (!CI->isImplDefault() && ParentDI != DI) {
                        ImplicitCI->getArgs().push_back(Target);
                        DI->getClauseList().push_back(ImplicitCI);

                        if (NeedNewName) {
                            std::string OrigName = getOrigNameFor(Context,Target);
                            std::string NewName = CreateNewNameFor(Context,Target);
                            Map[Target] = ArgNames(OrigName,NewName);
                        }

                        llvm::outs() << "debug: mark Arg '"
                                     << A->getPrettyArg(Context->getPrintingPolicy())
                                     << "' in directive '" << ParentDI->getAsString()
                                     << "' as present in directive '"
                                     << DI->getAsString() << "'\n";
                    }
                    //change only the base expr
                    BaseExpr = TmpBaseExpr;
                }
            }
            else {
                A = EmitImplicitDataMoveCodeFor(E);
                llvm::outs() << "debug: implicit move for '"
                             << getPrettyExpr(Context,E) << "' \n";
            }
        }
    }
#if 0
    else if (RStack.CurrentRegionIs(DK_HOST_DATA)) {
        ClauseList &CList = RStack.back()->getClauseList();
        for (ClauseList::iterator
                 II = CList.begin(), EE = CList.end(); II != EE; ++II) {
            ArgVector &Args = (*II)->getArgs();
            for (ArgVector::iterator
                     IA = Args.begin(), EA = Args.end(); IA != EA; ++IA) {
               if ((*IA)->Matches(A)) {
                    //do the Rename
                    std::string NewCode = getNewNameFor(Map,A);
                    Replacement R(Context->getSourceManager(),E,NewCode);
                    applyReplacement(ReplacementPool,R);
                    llvm::outs() << "debug: use device's '" << getPrettyExpr(Context,E)
                                 << "'\n";
                    return true;
                }
            }
        }
        llvm::outs() << "debug: use host's '" << getPrettyExpr(Context,E)
                     << "'\n";
        return false;
    }
#endif
    else {
        ClauseInfo *CI = A->getParent()->getAsClause();
        assert(CI && (CI->isDataClause()));

        if (!CI->isImplDefault() && CI->getParentDirective() != DI) {
            //FIXME: what about present_* Data Clauses?

            if (NeedNewName) {
                std::string OrigName = getOrigNameFor(Context,Target);
                std::string NewName = CreateNewNameFor(Context,Target);
                Map[Target] = ArgNames(OrigName,NewName);
            }

            ImplicitCI->getArgs().push_back(Target);
            DI->getClauseList().push_back(ImplicitCI);

            //ParentDI can be either data or declare or
            //compute or combined directive (in case of previous implicit move)
            DirectiveInfo *ParentDI = CI->getParentDirective();
            llvm::outs() << "debug: mark Arg '"
                         << A->getPrettyArg(Context->getPrintingPolicy())
                         << "' in directive '" << ParentDI->getAsString()
                         << "' as present in directive '"
                         << DI->getAsString() << "'\n";
        }
    }

    //should be ignored
    assert(A->getParent()->getAsClause() && "OpenACC API changed - update your program!");

    if (AExplicitBuffer) {
        assert(A->Matches(AExplicitBuffer));
        A = AExplicitBuffer;
    }

    //Do the Rename
    std::string NewName = getNewNameFor(Map,A);

    //if (!isa<ArrayArg>(A) && !A->getVarDecl()->getType()->isPointerType())
    if (!isa<ArrayArg>(A) && !isa<SubArrayArg>(A))
        //dereference
        NewName = "(*" + NewName + ")";

    Replacement R(Context->getSourceManager(),BaseExpr,NewName);
    applyReplacement(ReplacementPool,R);

    if (!NeedNewName)
        llvm::outs() << "debug: use explicit buffer's name for '"
                     << getPrettyExpr(Context,E) << "'\n";
    llvm::outs() << "debug: use device's '" << getPrettyExpr(Context,E) << "'\n";
    return true;
}

bool
Stage1_ASTVisitor::TraverseAccStmt(AccStmt *S) {
    VarDeclVector IgnoreVarsPool;
    IgnoreVarsPool.Prev = IgnoreVars;
    IgnoreVars = &IgnoreVarsPool;

    TRY_TO(WalkUpFromAccStmt(S));
    //{ CODE; }
    RStack.EnterRegion(S->getDirective());
    for (Stmt::child_range range = S->children(); range; ++range) {
        TRY_TO(TraverseStmt(*range));
    }
    RStack.ExitRegion(S->getDirective());

    IgnoreVars = IgnoreVars->Prev;

    return true;
}

bool
Stage1_ASTVisitor::TraverseMemberExpr(MemberExpr *S) {
    TRY_TO(WalkUpFromMemberExpr(S));

    //skip children (Base)
    return true;
}

bool
Stage1_ASTVisitor::TraverseArraySubscriptExpr(ArraySubscriptExpr *S) {
    TRY_TO(WalkUpFromArraySubscriptExpr(S));
    //{ CODE; }
    for (Stmt::child_range range = S->children(); range; ++range) {
        //ignore BaseExpr
        if (cast<Expr>(*range) == S->getBase())
            continue;
        TRY_TO(TraverseStmt(*range));
    }
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
        return true;
        //generate data moves
        ClauseList &CList = DI->getClauseList();
        for (ClauseList::iterator
                 II = CList.begin(), EE = CList.end(); II != EE; ++II) {
            if ((*II)->getKind() == CK_ON) {
                std::string NewCode = TaskWaitOn(DI,*II);
                SourceLocation Loc = DI->getLocEnd();
                Replacement R(Context->getSourceManager(),Loc,0,NewCode);
                applyReplacement(ReplacementPool,R);
                return true;
            }
        }

        DataIOSrc IOInfo(Context,DI,Map,RStack);

        //generate runtime calls for taskwait
        EmitCodeForExecutableDirective(DI,SubStmt);
#warning FIXME
        std::string NewCode;
        if (!NewCode.empty()) {
            NewCode = "\n{" + NewCode + "\n}";
            Replacement R(Context->getSourceManager(),DI->getLocEnd(),
                          0,NewCode);
            applyReplacement(ReplacementPool,R);
        }
    }
    else if (DI->getKind() == DK_TASK) {
        llvm::outs() << "  -  Create Kernel";

        TaskSrc NewTask(Context,"acc_create_task",DI,Map,RStack,ReplacementPool);

        Replacement Kernel(getCurrentKernelFile(),getCurrentKernelFileEndOffset(),0,NewTask.KernelCode());
        applyReplacement(ReplacementPool,Kernel);

        std::string DirectiveSrc =
            DI->getPrettyDirective(Context->getPrintingPolicy(),false);
        std::string NewCode = "/*" + DirectiveSrc + "*/\n"
            + "{" + NewTask.MemObjInfo.PrologueCode
            + NewTask.Geometry.definition
            + NewTask.HostCall() + "}";

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

    if (SM.isInSystemHeader(FD->getSourceRange().getBegin()))
        return true;

    if (!SM.isFromMainFile(FD->getSourceRange().getBegin()))
        return true;

#if 0
    if (FD->getResultType()->isVoidType()) {
    }
#endif

    return true;
}

bool
Stage1_ASTVisitor::VisitVarDecl(VarDecl *VD) {
    //do not run if not in any region

    SourceManager &SM = Context->getSourceManager();
    if (SM.isInSystemHeader(VD->getSourceRange().getBegin())) {
        //llvm::outs() << "Writing to system header prevented!\n";
        return true;
    }

    if (RStack.empty())
        return true;

    if (RStack.InRegion(DK_TASK)) {
        llvm::outs() << "debug: device resident declaration of '"
                     << VD->getName() << "'\n";
        IgnoreVars->push_back(VD);
        //maybe later this declaration is used as Argument in a nested
        //region
        //we still create any necessary data moves in this case
    }

    return true;
}

bool
Stage1_ASTVisitor::VisitDeclRefExpr(DeclRefExpr *DRE) {
    //do not run if not in any region
    //do not rename inside a data region scope
    //do not try to copy devise residents

    SourceManager &SM = Context->getSourceManager();
    if (SM.isInSystemHeader(DRE->getSourceRange().getBegin())) {
        //llvm::outs() << "Writing to system header prevented!\n";
        return true;
    }

    //we care only for variable declarations
    VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (!VD)
        return true;

    if (RStack.empty()) {
        return true;
    }

    if (isa<CallExpr>(RStack.back()->getAccStmt()->getSubStmt()))
        return true;

    llvm::outs() << "DEBUG: " << getPrettyExpr(Context,DRE) << "\n";

#if 0
    if (RStack.CurrentRegionIs(DK_TASK)) {
        //FIXME: maybe this should be a warning?
        llvm::outs() << "debug: use host's '"
                     << VD->getName() << "'" << "\n";
        return true;
    }
#endif
    if (IgnoreVars->has(VD)) {
        llvm::outs() << "debug: skip rename '"
                     << VD->getName() << "' (device resident)" << "\n";
    }
    else
        Rename(dyn_cast<Expr>(DRE));
    return true;
}

bool
Stage1_ASTVisitor::VisitMemberExpr(MemberExpr *ME) {
    //do not run if not in any region
    //do not rename inside a data region scope
    //do not try to copy devise residents

    SourceManager &SM = Context->getSourceManager();
    if (SM.isInSystemHeader(ME->getSourceRange().getBegin())) {
        //llvm::outs() << "Writing to system header prevented!\n";
        return true;
    }

    if (RStack.empty()) {
        return true;
    }

#if 0
    if (RStack.CurrentRegionIs(DK_TASK)) {
        llvm::outs() << "debug: use host's '"
                     << getPrettyExpr(Context,ME)
                     << "'" << "\n";
        return true;
    }
#endif

    Expr *BaseExpr = ME->getBase()->IgnoreParenCasts();
    while (MemberExpr *NewME = dyn_cast<MemberExpr>(BaseExpr->IgnoreParenImpCasts())) {
        BaseExpr = NewME->getBase()->IgnoreParenCasts();
        if (ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(BaseExpr))
            BaseExpr = ASE->getBase()->IgnoreParenCasts();
    }
    if (DeclRefExpr *DF = dyn_cast<DeclRefExpr>(BaseExpr))
        if (VarDecl *VD = dyn_cast<VarDecl>(DF->getDecl()))
            if (IgnoreVars->has(VD)) {
                llvm::outs() << "debug: skip rename '"
                             << getPrettyExpr(Context,ME)
                             << "' (device resident)" << "\n";
                return true;
            }

    Rename(dyn_cast<Expr>(ME));

    return true;
}

bool
Stage1_ASTVisitor::VisitArraySubscriptExpr(clang::ArraySubscriptExpr *ASE) {
    //similar to VisitMemberExpr()

    //do not run if not in any region
    //do not rename inside a data region scope
    //do not try to copy devise residents

    SourceManager &SM = Context->getSourceManager();
    if (SM.isInSystemHeader(ASE->getSourceRange().getBegin())) {
        //llvm::outs() << "Writing to system header prevented!\n";
        return true;
    }

    if (RStack.empty()) {
        return true;
    }

#if 0
    if (RStack.CurrentRegionIs(DK_TASK)) {
        llvm::outs() << "debug: use host's '"
                     << getPrettyExpr(Context,ASE)
                     << "'" << "\n";
        return true;
    }
#endif

    Expr *BaseExpr = ASE->getBase()->IgnoreParenCasts();
    while (MemberExpr *NewME = dyn_cast<MemberExpr>(BaseExpr->IgnoreParenImpCasts())) {
        BaseExpr = NewME->getBase()->IgnoreParenCasts();
        if (ArraySubscriptExpr *NewASE = dyn_cast<ArraySubscriptExpr>(BaseExpr))
            BaseExpr = NewASE->getBase()->IgnoreParenCasts();
    }
    if (DeclRefExpr *DF = dyn_cast<DeclRefExpr>(BaseExpr))
        if (VarDecl *VD = dyn_cast<VarDecl>(DF->getDecl()))
            if (IgnoreVars->has(VD)) {
                llvm::outs() << "debug: skip rename '"
                             << getPrettyExpr(Context,ASE)
                             << "' (device resident)" << "\n";
                return true;
            }

    Rename(dyn_cast<Expr>(ASE));

    return true;
}

bool
Stage1_ASTVisitor::VisitCallExpr(CallExpr *CE) {
    SourceManager &SM = Context->getSourceManager();
    if (SM.isInSystemHeader(CE->getSourceRange().getBegin())) {
        //llvm::outs() << "Writing to system header prevented!\n";
        return true;
    }

    if (RStack.empty()) {
        return true;
    }

    FunctionDecl *FD = CE->getDirectCallee();
    if (!FD)
        return true;

    std::string Name = FD->getNameAsString();

    //FIXME: create accurate kernel

    return true;
}

bool
Stage1_ASTVisitor::VisitReturnStmt(ReturnStmt *S) {
    //FIXME: check whether we are inside a task

    return true;
}

ObjRefDef addVarDeclForDevice(clang::ASTContext *Context, Arg *A, NameMap &Map, RegionStack &RStack) {
    //declare a new var here for the accelerator

    if (Arg *AMemObj = RStack.FindBufferObjectInRegionStack(A)) {
        (void)AMemObj;
        //abort creation of device buffer, we created it previously
        llvm::outs() << "abort new buffer (use existing) for '"
                     << A->getPrettyArg(Context->getPrintingPolicy())
                     << "' in Clause '" << A->getParent()->getAsClause()->getAsString() << "'\n";
        return ObjRefDef();
    }
    else
        //FIXME: some of the Args may already exist because of previous regions
        RStack.InterRegionMemBuffers.push_back(A);

#if 0
    //get type as string
    QualType Ty = A->getExpr()->getType();
    if (const ArrayType *ATy = Context->getAsArrayType(Ty))
        Ty = ATy->getElementType();
    std::string type = Ty.getAsString(Context->getPrintingPolicy());
#endif

    std::string OrigName = getOrigNameFor(Context,A);
    std::string NewName = CreateNewNameFor(Context,A);
    Map[A] = ArgNames(OrigName,NewName);
    //llvm::outs() << "orig name: " << OrigName << "  new name: " << NewName << "\n";

    //generate new code
    std::string NewCode = "\n";

    //each stage must generate a valid file to be parsed by the next stage,
    //so put a placehoder here and remove it in the next stage

    NewCode += "struct _memory_object " + NewName;
    std::string BaseName;
    std::string SizeExpr;

    if (SubArrayArg *SA = dyn_cast<SubArrayArg>(A)) {
        BaseName = getPrettyExpr(Context,SA->getExpr());
        SizeExpr = "sizeof(" + SA->getExpr()->getType().getAsString()
            + ")*(" + getPrettyExpr(Context,SA->getLength()) + ")";
    }
    else {
        BaseName = OrigName;
        SizeExpr = "sizeof(" + A->getExpr()->getType().getAsString() + ")";
    }

    std::string address = OrigName;
    if (!isa<ArrayArg>(A) && !isa<SubArrayArg>(A))
        address = "&" + OrigName;

    NewCode += " = { .host_ptr = (void*)" + address
        + ", .size = " + SizeExpr + "};";

    return ObjRefDef(NewName,NewCode);
}

std::string
Stage1_ASTVisitor::addMoveHostToDevice(Arg *A, ClauseInfo *AsyncCI, std::string Event) {
    std::string NewName;
    std::string OrigName;
    std::string BaseName;
    std::string SizeExpr;

    if (Arg *AMemObj = RStack.FindBufferObjectInRegionStack(A)) {
        NewName = getNewNameFor(Map,AMemObj);
        OrigName = getOrigNameFor(Context,AMemObj);
    }
    else {
        NewName = getNewNameFor(Map,A);
        OrigName = getOrigNameFor(Context,A);
    }

    if (SubArrayArg *SA = dyn_cast<SubArrayArg>(A)) {
        BaseName = getPrettyExpr(Context,SA->getExpr());
        SizeExpr = "sizeof(" + SA->getExpr()->getType().getAsString() + ")*("
            + getPrettyExpr(Context,SA->getLength()) + ")";
    }
    else {
        BaseName = OrigName;
        SizeExpr = "sizeof(" + A->getExpr()->getType().getAsString() + ")";
    }

    std::string NewCode = "";

    if (AsyncCI)
        NewCode += "cl_event " + Event + ";";
    NewCode += "error = clEnqueueWriteBuffer(queue, ";
    NewCode += NewName + ", ";
    NewCode += (AsyncCI) ? "CL_FALSE" : "CL_TRUE";
    NewCode += ", 0, " + SizeExpr + ", ";
    if (!isa<ArrayArg>(A))
        NewCode += "&";
    NewCode += BaseName;

    //check for previous async event with matching Argument
    if (AsyncCI && AsyncCI->hasArgs()) {
        Arg *Target = AsyncCI->getArg();
        std::string PrevEvent;
        for (AsyncEventVector::reverse_iterator
                 II = AsyncEvents.rbegin(), EE = AsyncEvents.rend(); II != EE; ++II) {
            Arg *IA = (II->first->hasArgs()) ? II->first->getArg() : 0;
            if (IA && IA->Matches(Target)) {
                PrevEvent = II->second;
                break;
            }
        }
        if (!PrevEvent.empty())
            NewCode += ", 1, &" + PrevEvent + ", ";
        else
            NewCode += ", 0, NULL, ";
    }
    else
        NewCode += ", 0, NULL, ";

    NewCode += (AsyncCI) ? "&" + Event : "NULL";
    NewCode += ");";
    NewCode += "clCheckError(error,\"write buffer '" + OrigName + "'\");";

    return NewCode;
}

std::string
Stage1_ASTVisitor::addMoveDeviceToHost(Arg *A, ClauseInfo *AsyncCI, std::string Event) {
    std::string NewName;
    std::string OrigName;
    std::string BaseName;
    std::string SizeExpr;

    if (Arg *AMemObj = RStack.FindBufferObjectInRegionStack(A)) {
        NewName = getNewNameFor(Map,AMemObj);
        OrigName = getOrigNameFor(Context,AMemObj);
    }
    else {
        NewName = getNewNameFor(Map,A);
        OrigName = getOrigNameFor(Context,A);
    }

    if (SubArrayArg *SA = dyn_cast<SubArrayArg>(A)) {
        BaseName = getPrettyExpr(Context,SA->getExpr());
        SizeExpr = "sizeof(" + SA->getExpr()->getType().getAsString() + ")*("
            + getPrettyExpr(Context,SA->getLength()) + ")";
    }
    else {
        BaseName = OrigName;
        SizeExpr = "sizeof(" + A->getExpr()->getType().getAsString() + ")";
    }

    std::string NewCode = "";

    if (AsyncCI)
        NewCode += "cl_event " + Event + ";";
    NewCode += "error = clEnqueueReadBuffer(queue, ";
    NewCode += NewName + ", ";
    NewCode += (AsyncCI) ? "CL_FALSE" : "CL_TRUE";
    NewCode += ", 0, " + SizeExpr + ", ";
    if (!isa<ArrayArg>(A))
        NewCode += "&";
    NewCode += BaseName;

    //check for previous async event with matching Argument
    if (AsyncCI && AsyncCI->hasArgs()) {
        Arg *Target = AsyncCI->getArg();
        std::string PrevEvent;
        for (AsyncEventVector::reverse_iterator
                 II = AsyncEvents.rbegin(), EE = AsyncEvents.rend(); II != EE; ++II) {
            Arg *IA = (II->first->hasArgs()) ? II->first->getArg() : 0;
            if (IA && IA->Matches(Target)) {
                PrevEvent = II->second;
                break;
            }
        }
        if (!PrevEvent.empty())
            NewCode += ", 1, &" + PrevEvent + ", ";
        else
            NewCode += ", 0, NULL, ";
    }
    else
        NewCode += ", 0, NULL, ";

    NewCode += (AsyncCI) ? "&" + Event : "NULL";
    NewCode += ");";
    NewCode += "clCheckError(error,\"read buffer '" + OrigName + "'\");";

    return NewCode;
}

#if 0
static std::string addDeallocDeviceMemory(clang::ASTContext *Context,
                                          NameMap &Map, Arg *A) {
    std::string NewName = getNewNameFor(Map,A);
    std::string OrigName = getOrigNameFor(Context,A);
    std::string NewCode = "error = clReleaseMemObject(" + NewName + ");";
    NewCode += "clCheckError(error,\"release '" + OrigName + "'\");";

    return NewCode;
}
#endif

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

void EmitCodeForDataClause(clang::ASTContext *Context,
                           DirectiveInfo *DI, ClauseInfo *CI, Stmt *SubStmt,
                           SmallVector<ObjRefDef,8> &init_list, NameMap &Map,
                           RegionStack &RStack) {
    assert(CI->isDataClause());

    for (ArgVector::iterator
             II = CI->getArgs().begin(), EE = CI->getArgs().end(); II != EE; ++II) {
        Arg *A = *II;

        ObjRefDef CreateMem = addVarDeclForDevice(Context,A,Map,RStack);
        init_list.push_back(CreateMem);
    }
}

void DataIOSrc::init(clang::ASTContext *Context, DirectiveInfo *DI,
                     NameMap &Map, RegionStack &RStack) {
    Stmt *SubStmt = DI->getAccStmt()->getSubStmt();

    SmallVector<ObjRefDef,8> in_init_list;
    SmallVector<ObjRefDef,8> out_init_list;

    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator
             II = CList.begin(), EE = CList.end(); II != EE; ++II) {
        if ((*II)->getKind() == CK_IN)
            EmitCodeForDataClause(Context,DI,*II,SubStmt,in_init_list,Map,RStack);
        else if ((*II)->getKind() == CK_OUT)
            EmitCodeForDataClause(Context,DI,*II,SubStmt,out_init_list,Map,RStack);
    }

    llvm::raw_string_ostream tmpIn(NumOfInMemObj);
    tmpIn << in_init_list.size();
    tmpIn.str();
    std::string NewInCode;
    if (in_init_list.size()) {
        std::string pre_in_init;
        InRef = "__accll_task_in_memobj";
        NewInCode += "struct _memory_object " + InRef + "[" + NumOfInMemObj +"] = {";
        for (SmallVector<ObjRefDef,8>::iterator II = in_init_list.begin(), EE = in_init_list.end();
             II != EE; ++II) {
            pre_in_init += (*II).definition;
            NewInCode += (*II).name_ref;
            if (II != &in_init_list.back())
                NewInCode += ",";
        }
        NewInCode += "};";
        NewInCode = pre_in_init + NewInCode;
        PrologueCode += NewInCode;
    }
    else {
        InRef = "NULL";
    }

    llvm::raw_string_ostream tmpOut(NumOfOutMemObj);
    tmpOut << out_init_list.size();
    tmpOut.str();
    std::string NewOutCode;
    if (out_init_list.size()) {
        std::string pre_out_init;
        OutRef = "__accll_task_out_memobj";
        NewOutCode += "struct _memory_object " + OutRef + "[" + NumOfOutMemObj + "] = {";
        for (SmallVector<ObjRefDef,8>::iterator II = out_init_list.begin(), EE = out_init_list.end();
             II != EE; ++II) {
            pre_out_init += (*II).definition;
            NewOutCode += (*II).name_ref;
            if (II != &out_init_list.back())
                NewOutCode += ",";
        }
        NewOutCode += "};";
        NewOutCode = pre_out_init + NewOutCode;
        PrologueCode += NewOutCode;
    }
    else {
        OutRef = "NULL";
    }
}

std::string
Stage1_ASTVisitor::TaskWaitOn(DirectiveInfo *DI, ClauseInfo *CI) {
    assert(CI->getKind() == CK_ON);

    ClauseInfo *AsyncCI = 0;

    SmallVector<std::string,8> EventList;
    unsigned EventID = 0;

    std::string NewCode;

    for (ArgVector::iterator
             II = CI->getArgs().begin(), EE = CI->getArgs().end(); II != EE; ++II) {
        Arg *A = RStack.FindVisibleCopyInRegionStack(*II);
        assert(A);

        std::string Event = "__accll_update_event_tmp_";
        raw_string_ostream OS(Event);
        OS << EventID;
        EventID++;
        EventList.push_back(OS.str());

        NewCode += addMoveDeviceToHost(A,AsyncCI,Event);
    }

    if (!AsyncCI)
        return NewCode;

    Arg *A = (AsyncCI->hasArgs()) ? AsyncCI->getArg() : 0;

    //a safe SourceLocation, declares all the event objects at the top of
    //the current function scope
    SourceLocation EventLoc =
        CurrentFunction->getBody()->getLocStart().getLocWithOffset(1);

    std::string EventName = CreateNewUniqueEventNameFor(A);
    std::string NewEventDeclaration = "cl_event " + EventName + ";";
    AsyncEvents.push_back(AsyncEvent(AsyncCI,EventName));

    Replacement R(Context->getSourceManager(),EventLoc,0,NewEventDeclaration);
    applyReplacement(ReplacementPool,R);

    //Generate Marker to wait for later on

    if (EventList.empty())
        return std::string();

    std::string Size;
    raw_string_ostream OS(Size);
    OS << EventList.size();

    NewCode += "const cl_event __accll_event_wait_list[] = { ";

    std::string Events;
    for (SmallVector<std::string,8>::iterator
             II = EventList.begin(), EE = EventList.end(); II != EE; ++II) {
        if (!Events.empty())
            Events += ", ";
        Events += *II;
    }
    NewCode += Events + " };";

    NewCode += "error = clEnqueueMarkerWithWaitList(queue, " + OS.str() + ", ";
    NewCode += "__accll_event_wait_list, ";
    NewCode += "&" + EventName + ");";
    NewCode += "clCheckError(error, \"marker with wait list\");";

    return NewCode;
}

std::string
Stage1_ASTVisitor::EmitCodeForDirectiveWait(DirectiveInfo *DI, Stmt *SubStmt) {
    //handle only the wait events from the update directives (data moves)
    //any other case will be handled at Stage3

    ClauseList &CList = DI->getClauseList();
    if (CList.empty())
        return std::string();

    for (ClauseList::iterator II = CList.begin(), EE = CList.end();
         II != EE; ++II) {
        //ClauseInfo *CI = *II;
    }

    Arg *A = DI->getArg();

    std::string Events;
    unsigned EventsNum = 0;

    for (AsyncEventVector::iterator
             II = AsyncEvents.begin(), EE = AsyncEvents.end(); II != EE; ++II) {
        Arg *IA = (II->first->hasArgs()) ? II->first->getArg() : 0;
        if (IA && A->Matches(IA)) {
            if (!Events.empty())
                Events += ", ";
            Events += II->second;
            EventsNum++;
        }
    }

    if (EventsNum == 0)
        //we have no async data moves, skip wait directive for Stage2
        //we check again at Stage3 for async device code
        return std::string();

    //wait for a list of events from data moves

    std::string Tmp;
    raw_string_ostream OS(Tmp);
    OS << EventsNum;

    std::string NewCode;
    NewCode += "const cl_event __accll_event_wait_list[] = { ";
    NewCode += Events + "};";
    NewCode += "error = clEnqueueBarrierWithWaitList(queue, " + OS.str();
    NewCode += ", __accll_event_wait_list, NULL);";
    NewCode += "clCheckError(error, \"wait barrier with wait list\");";

    return NewCode;
}

void Stage1_ASTVisitor::EmitCodeForExecutableDirective(DirectiveInfo *DI, Stmt *SubStmt) {
    assert(DI->getKind() == DK_TASKWAIT);

    //FIXME: clang bug, wrong EndLocation for BinaryOperator stmts
    //use findLocationAfterToken() in this case

    std::string NewCode;
    NewCode += EmitCodeForDirectiveWait(DI,SubStmt);

    if (NewCode.empty())
        return;

    NewCode = "\n{" + NewCode + "\n}";

    Replacement R(Context->getSourceManager(),DI->getLocEnd(),0,NewCode);
    applyReplacement(ReplacementPool,R);
    return;
}

void Stage1_ASTVisitor::Init(ASTContext *C) {
    Context = C;
}

void
Stage1_ASTVisitor::Finish() {
    if (!TaskSrc::KernelCalls)
        return;

    std::string NewCode = UserTypes;

    Replacement R(getCurrentKernelFile(),
                  getCurrentKernelFileStartOffset()-1,0,NewCode);
    applyReplacement(ReplacementPool,R);
    CurrentKernelFileIterator++;
}

#if 0
///////////////////////////////////////////////////////////////////////////////
//                        Stage3
///////////////////////////////////////////////////////////////////////////////

std::string
Stage3_ASTVisitor::CreateNewNameFor(Arg *A, bool AddPrefix) {
    assert(!isa<RawExprArg>(A) && !isa<SubArrayArg>(A));

    ClauseInfo *CI = A->getParent()->getAsClause();
    assert(CI);

    if (isa<RawExprArg>(A) && A->isICE())
        return A->getPrettyArg(PrintingPolicy(Context->getLangOpts()));

    std::string Prefix = "__accll_";  //prefix
    std::string NewName;
    Expr *BaseExpr = A->getExpr()->IgnoreParenCasts();
    if (ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(BaseExpr)) {
        Expr *Base = ASE->getBase()->IgnoreParenCasts();
        Arg *BaseA = CreateNewArgFrom(Base,CI,Context);
        std::string NameBase = CreateNewNameFor(BaseA,/*AddPrefix=*/false);

        Expr *Index = ASE->getIdx()->IgnoreParenCasts();
        Arg *IndexA = CreateNewArgFrom(Index,CI,Context);
        std::string NameIndex = CreateNewNameFor(IndexA,/*AddPrefix=*/false);

        DirectiveInfo *DI = CI->getParentDirective();
        assert(DI);
        if (!(isa<RawExprArg>(IndexA) && IndexA->isICE()))
            if (RStack.FindVisibleCopyInDirective(DI,IndexA) ||
                RStack.FindVisibleCopyInRegionStack(IndexA))
                NameIndex = Prefix + NameIndex;

        //NameBase and NameIndex have prefix if needed, d not add it again here

        if (isa<RawExprArg>(IndexA) && IndexA->isICE())
            //let the caller to add any dereference code to the final NewName
            NewName = NameBase + "_" + NameIndex + "_";
        else if (!isa<ArrayArg>(IndexA))
            //dereference the Index only
            NewName = NameBase + "[(*" + NameIndex + ")]";
        else
            //this is a user programming error, let the compiler find it
            NewName = NameBase + "[" + NameIndex + "]";
    }
    else if (MemberExpr *ME = dyn_cast<MemberExpr>(BaseExpr)) {
        FieldDecl *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
        assert(FD);
        BaseExpr = ME->getBase()->IgnoreParenImpCasts();
        Arg *BaseA = CreateNewArgFrom(BaseExpr,CI,Context);
        std::string NameBase = CreateNewNameFor(BaseA,/*AddPrefix=*/false);
        NewName = NameBase + "_" + FD->getNameAsString();
    }
    else {
        assert(isa<DeclRefExpr>(BaseExpr));
        DeclRefExpr *DF = dyn_cast<DeclRefExpr>(BaseExpr);
        (void)DF;
        assert(DF && isa<VarDecl>(DF->getDecl()));
        NewName = getPrettyExpr(Context,BaseExpr);
    }

    if (AddPrefix)
        NewName = Prefix + NewName;

    return NewName;
}
#endif

bool
Stage1_ASTVisitor::VisitRecordDecl(RecordDecl *R) {
    SourceLocation Loc = R->getSourceRange().getBegin();
    assert(!Loc.isInvalid());

    SourceManager &SM = Context->getSourceManager();
    if (SM.isInSystemHeader(Loc))
        return true;

    //maybe the implementiation headers are not in system directories
    if (!SM.isFromMainFile(Loc))
        return true;

    llvm::outs() << "found user defined record\n";

    std::string Tmp;
    raw_string_ostream OS(Tmp);
    R->print(OS);
    UserTypes += OS.str() + ";\n\n";

    return true;
}

bool
Stage1_ASTVisitor::VisitEnumDecl(EnumDecl *E) {
    SourceLocation Loc = E->getSourceRange().getBegin();
    assert(!Loc.isInvalid());

    SourceManager &SM = Context->getSourceManager();
    if (SM.isInSystemHeader(Loc))
        return true;

    //maybe the implementiation headers are not in system directories
    if (!SM.isFromMainFile(Loc))
        return true;

    llvm::outs() << "found user defined enum\n";

    std::string Tmp;
    raw_string_ostream OS(Tmp);
    E->print(OS);
    UserTypes += OS.str() + ";\n\n";

    return true;
}

bool
Stage1_ASTVisitor::VisitTypedefDecl(TypedefDecl *T) {
    SourceLocation Loc = T->getSourceRange().getBegin();

    //compiler defined typedef
    if (Loc.isInvalid())
        return true;

    SourceManager &SM = Context->getSourceManager();
    if (SM.isInSystemHeader(Loc))
        return true;

    //maybe the implementiation headers are not in system directories
    if (!SM.isFromMainFile(Loc))
        return true;

    llvm::outs() << "found user defined typedef\n";

    std::string Tmp;
    raw_string_ostream OS(Tmp);
    T->print(OS);
    UserTypes += OS.str() + ";\n\n";

    return true;
}

#if 0
bool
Stage3_ASTVisitor::VisitVarDecl(VarDecl *VD) {
    //FIXME: move this to separate stage
    //       in order to be able to ignore this stage
    //       when there is no device code

    //clean Stage1 mess

    ValueDecl *Val = cast<ValueDecl>(VD);
    assert(Val);

    QualType QTy = Val->getType();
    std::string TypeName = QTy.getAsString();
    //llvm::outs() << "\ntype is: " << TypeName << "\n";

    //ignore this vardecl
    if (TypeName.compare("__incomplete__ *") != 0)
        return true;

    assert(VD->hasInit());
    Expr *Init = VD->getInit();

    //llvm::outs() << "'" << getPrettyExpr(Context,Init) << "'\n";

    std::string NewName = VD->getNameAsString();
    std::string NewCode = "cl_mem " + NewName + " = " + getPrettyExpr(Context,Init);
    //the semicolon remains
    Replacement R(Context->getSourceManager(),VD,NewCode);
    applyReplacement(ReplacementPool,R);

    return true;
}

bool
Stage3_ASTVisitor::Stage3_TraverseTemplateArgumentLocsHelper(const TemplateArgumentLoc *TAL,unsigned Count) {
    for (unsigned I = 0; I < Count; ++I) {
        TRY_TO(TraverseTemplateArgumentLoc(TAL[I]));
    }
    return true;
}

bool
Stage3_ASTVisitor::TraverseFunctionDecl(FunctionDecl *FD) {
    int KernelCallsSoFar = KernelCalls;
    KernelCalls = 0;
    hasDirectives = 0;

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
                TRY_TO(Stage3_TraverseTemplateArgumentLocsHelper(TALI->getTemplateArgs(),
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

    if (SM.isInSystemHeader(FD->getSourceRange().getBegin()))
        return true;

    //maybe the implementiation headers are not in system directories
    if (!SM.isFromMainFile(FD->getSourceRange().getBegin()))
        return true;

    int NewKernelCalls = KernelCalls;
    KernelCalls = KernelCallsSoFar + NewKernelCalls;

    llvm::outs() << "in function '" << FD->getNameAsString() << "'\n";

#if 0
    if (hasDirectives) {
        std::string NewCode = "\n__accll_init_accll_runtime();";
        if (NewKernelCalls) {
            //Create for each function a program object in the stack
            std::string KernelFile = getCurrentKernelFile();
            NewCode += "cl_program program = __accll_load_and_build(\""
                + KernelFile + "\");";
        }

        CompoundStmt *Body = dyn_cast<CompoundStmt>(FD->getBody());
        assert(Body);
        SourceLocation StartLoc = Body->getLocStart().getLocWithOffset(1);
        Replacement R(Context->getSourceManager(),StartLoc,0,NewCode);
        applyReplacement(ReplacementPool,R);
    }
    #endif

    return true;
}

bool
Stage3_ASTVisitor::VisitCallExpr(CallExpr *CE) {
    SourceManager &SM = Context->getSourceManager();
    if (SM.isInSystemHeader(CE->getSourceRange().getBegin())) {
        //llvm::outs() << "Writing to system header prevented!\n";
        return true;
    }

    FunctionDecl *FD = CE->getDirectCallee();
    if (!FD)
        return true;

    std::string Name = FD->getNameAsString();

#define TEST_RUNTIME_CALL(name,call) (name.compare(#call) == 0)
    if (TEST_RUNTIME_CALL(Name,acc_async_test)) {
        llvm::outs() << "Processing Runtime call to '" << Name << "' ...\n";
        assert(CE->getNumArgs() == 1 && "OpenACC API changed, update your program!");
        //ignore any casts or parens,
        //the Arg Constructors expect a 'clean' expression
        Expr *E = CE->getArg(0)->IgnoreParenCasts();
        //llvm::outs() << "Argument is '" + getPrettyExpr(Context,E) + "'\n";

        Arg *Target = CreateNewArgFrom(E,0,Context);

        std::string Events;
        unsigned EventsNum = 0;

        for (AsyncEventVector::iterator
                 II = AsyncEvents.begin(), EE = AsyncEvents.end(); II != EE; ++II) {
            Arg *IA = (II->first->hasArgs()) ? II->first->getArg() : 0;
            if (IA && Target->Matches(IA)) {
                if (!Events.empty())
                    Events += ", ";
                Events += II->second;
                EventsNum++;
            }
        }

        if (EventsNum == 0) {
            //we have no async device code
            llvm::outs() << "No asynchronous events found\n";
            return true;
        }

        //wait for a list of events

        std::string Tmp;
        raw_string_ostream OS(Tmp);
        OS << EventsNum;

        std::string NewCode = "accll_async_test(" + OS.str() + "," + Events + ")";
        Replacement R(Context->getSourceManager(),CE,NewCode);
        applyReplacement(ReplacementPool,R);
    }
    else if (TEST_RUNTIME_CALL(Name,acc_async_test_all)) {
        llvm::outs() << "Processing Runtime call to '" << Name << "' ...\n";

        std::string Events;
        unsigned EventsNum = 0;

        for (AsyncEventVector::iterator
                 II = AsyncEvents.begin(), EE = AsyncEvents.end(); II != EE; ++II) {
            Arg *IA = (II->first->hasArgs()) ? II->first->getArg() : 0;
            if (IA) {
                if (!Events.empty())
                    Events += ", ";
                Events += II->second;
                EventsNum++;
            }
        }

        if (EventsNum == 0) {
            //we have no async device code
            llvm::outs() << "No asynchronous events found\n";
            return true;
        }

        //wait for a list of events

        std::string Tmp;
        raw_string_ostream OS(Tmp);
        OS << EventsNum;

        std::string NewCode = "accll_async_test(" + OS.str() + "," + Events + ")";
        Replacement R(Context->getSourceManager(),CE,NewCode);
        applyReplacement(ReplacementPool,R);
    }
    else if (TEST_RUNTIME_CALL(Name,__accll_unreachable)) {
        SourceLocation StartLoc = CE->getLocStart();
        //EndLoc + 1, is guaranteed to be semicolol ';', see Stage00
        SourceLocation EndLoc = CE->getLocEnd().getLocWithOffset(2);
        CharSourceRange Range(SourceRange(StartLoc,EndLoc),/*IsTokenRange=*/false);

        //avoid generation of NullStmt
        //Replacement R(Context->getSourceManager(),CE,"");
        Replacement R(Context->getSourceManager(),Range,"");
        applyReplacement(ReplacementPool,R);
    }
#undef TEST_RUNTIME_CALL

    return true;
}
#endif

std::string TaskSrc::MakeParamFromArg(Arg *A,bool MakeDefinition,
                                      std::string Kernel, int &ArgPos,
                                      std::string &CleanupCode) {
    //get original name
    std::string OrigName = A->getPrettyArg(PrintingPolicy(Context->getLangOpts()));

    //create new name
    std::string NewName("__accll_");  //prefix
    NewName += OrigName;
    ReplaceStringInPlace(NewName,"->","_");  //delimiter
    ReplaceStringInPlace(NewName,".","_");  //delimiter
    ReplaceStringInPlace(NewName," ","_");  //delimiter
    if (isa<ArrayElementArg>(A)) {
        ReplaceStringInPlace(NewName,"[","_");  //delimiter
        ReplaceStringInPlace(NewName,"]","_");  //delimiter
    }
    if (isa<SubArrayArg>(A)) {
        ReplaceStringInPlace(NewName,"[","_");  //delimiter
        ReplaceStringInPlace(NewName,"]","_");  //delimiter
        ReplaceStringInPlace(NewName,":","_");  //delimiter

        //Length may be subtraction expression
        ReplaceStringInPlace(NewName,"-","_");  //delimiter
    }

    ClauseInfo *CI = A->getParent()->getAsClause();
    assert(CI);

    //generate new code
    std::string NewCode;

    if (MakeDefinition) {
        //get type as string
        QualType Ty = A->getExpr()->getType();
        if (const ArrayType *ATy = Context->getAsArrayType(Ty))
            Ty = ATy->getElementType();
        std::string type = Ty.getAsString(Context->getPrintingPolicy());

        NewCode += "__global " + type + " *" + NewName;
    }
    else {
        //get type as string
        QualType Ty = A->getExpr()->getType();
        std::string type = Ty.getAsString(Context->getPrintingPolicy());

        std::stringstream ArgNum;
        ArgNum << ArgPos++;

        NewCode += NewName;
    }

    return NewCode;
}

struct UniqueArgVector : ArgVector {
    bool isUnique(Arg *Target) const {
        for (const_iterator II = begin(), EE = end(); II != EE; ++II)
            if (Target->Matches(*II))
                return false;
        return true;
    }
};

std::string TaskSrc::MakeParams(DirectiveInfo *DI,
                                bool MakeDefinition, std::string Kernel,
                                std::string &CleanupCode, int &ArgNum) {
    std::string Params;

    ClauseList DataClauses;

    ClauseList &CList = DI->getClauseList();
    for (ClauseList::iterator II = CList.begin(), EE = CList.end();
         II != EE; ++II) {
        if ((*II)->isDataClause())
            DataClauses.push_back(*II);
    }

    UniqueArgVector UniqueArgPool;

    for (ClauseList::iterator II = DataClauses.begin(), EE = DataClauses.end();
         II != EE; ++II) {
        ArgVector &Args = (*II)->getArgs();
        for (ArgVector::iterator IA = Args.begin(), EA = Args.end();
             IA != EA; ++IA)
            if (UniqueArgPool.isUnique(*IA))
                UniqueArgPool.push_back(*IA);
    }

    for (UniqueArgVector::iterator
             IA = UniqueArgPool.begin(), EA = UniqueArgPool.end();
         IA != EA; ++IA) {
        Params += MakeParamFromArg(*IA,MakeDefinition,Kernel,ArgNum,CleanupCode);
        if (MakeDefinition)
            if (*IA != UniqueArgPool.back())
                Params += ", ";
    }

    /////////////////////////////////////////////////////////////
    //get nested params

    CompoundStmt *Comp = dyn_cast<CompoundStmt>(DI->getAccStmt()->getSubStmt());

    if (ForStmt *F = dyn_cast<ForStmt>(DI->getAccStmt()->getSubStmt()))
        Comp = dyn_cast<CompoundStmt>(F->getBody());
    assert(Comp);

    CompoundStmt::body_iterator II = Comp->body_begin();
    CompoundStmt::body_iterator EE = Comp->body_end();
    for (; II != EE; ++II) {
        if (AccStmt *Acc = dyn_cast<AccStmt>(*II)) {
            //DI is not yet in the RegionStack, see TraverseAccStmt()
            RStack.EnterRegion(DI);
            std::string NewNestedCode = MakeParams(Acc->getDirective(),
                                                   MakeDefinition,Kernel,
                                                   CleanupCode,ArgNum);
            RStack.ExitRegion(DI);
            if (!NewNestedCode.empty()) {
                if (MakeDefinition)
                    Params += ", ";
                Params += NewNestedCode;
            }
        }
    }
    /////////////////////////////////////////////////////////////

    return Params;
}

#if 0
std::string
Stage3_ASTVisitor::getIdxOfWorkerInLoop(ForStmt *F, std::string Qual) {
    assert(Qual.compare("global") == 0 || Qual.compare("local") == 0);

    std::string Buffer;
    raw_string_ostream OS(Buffer);

    std::string NewCode = "";
    if (const DeclStmt *InitDS = dyn_cast_or_null<DeclStmt>(F->getInit())) {
        //asserts if not SingleDecl
        //FIXME: what about multiple declarations?
        //       eg. 'for (int x=1,y=2; ... ; ..) ;

        const NamedDecl *ND = cast<NamedDecl>(InitDS->getSingleDecl());

        InitDS->printPretty(OS,/*Helper=*/0,
                            Context->getPrintingPolicy(),/*Indentation=*/0);
        ND->printName(OS);
    }
    else if (const BinaryOperator *BO = dyn_cast_or_null<BinaryOperator>(F->getInit())) {
        assert(BO->isAssignmentOp());
        BO->getLHS()->printPretty(OS,/*Helper=*/0,
                                  Context->getPrintingPolicy(),/*Indentation=*/0);
        QualType QTy = BO->getLHS()->getType();
        std::string TypeName = QTy.getAsString();
        NewCode += TypeName + " ";
    }
    else
        assert(0 && "unknown for init expr");

    NewCode += OS.str() + " = get_" + Qual + "_id(0);";

    return NewCode;
}
#endif

std::string TaskSrc::MakeBody(DirectiveInfo *DI, Stmt *SubStmt) {
    std::string Body;
    raw_string_ostream OS(Body);

    CompoundStmt *Comp = dyn_cast<CompoundStmt>(SubStmt);
    assert(Comp);

    OS << "{\n";

    /////////////////////////////////////////////////////////////
    //create nested bodies

    CompoundStmt::body_iterator II = Comp->body_begin();
    CompoundStmt::body_iterator EE = Comp->body_end();
    for (; II != EE; ++II) {
        if (AccStmt *Acc = dyn_cast<AccStmt>(*II)) {
            DirectiveInfo *NestedDI = Acc->getDirective();
            Stmt *NestedSubStmt = Acc->getSubStmt();
            assert(NestedSubStmt);

            //DI is not yet in the RegionStack, see TraverseAccStmt()

            RStack.EnterRegion(DI);
            std::string NewNestedCode = MakeBody(NestedDI,NestedSubStmt);
            RStack.ExitRegion(DI);
            NewNestedCode = "{" + NewNestedCode + "}";
            OS << NewNestedCode;
        }
        else {
            (*II)->printPretty(OS,0,PrintingPolicy(Context->getLangOpts()));
            //FIXME: semicolon is not necessary everytime
            OS << ";";
        }
    }
    /////////////////////////////////////////////////////////////

    OS << "\n}\n";
    OS.str();

    return Body;
}

ObjRefDef
TaskSrc::CreateKernel(DirectiveInfo *DI, Stmt *SubStmt) {
    assert(SubStmt && "Null SubStmt");

    std::string CleanupCode;

    DirectiveInfo *EntryDI = DI;
    Stmt *EntrySubStmt = SubStmt;

    std::string prefix("accll_kernel_");
    std::string type("accurate");
    std::string UName;
    std::string kernel;

    //create device code
    if (CallExpr *CE = dyn_cast<CallExpr>(SubStmt)) {
        FunctionDecl *FD = CE->getDirectCallee();
        assert(FD);
        std::string Name = FD->getNameAsString();
        UName = getUniqueKernelName(Name);
        raw_string_ostream OS(kernel);
        if (!FD->getResultType()->isVoidType()) {
            llvm::outs() << "Error: Function '" << Name
                         << "' must have return type of void to define a task\n";
            return ObjRefDef();
        }
        FD->print(OS,Context->getPrintingPolicy());
        OS.str();
        kernel = "__kernel " + kernel;
        llvm::outs() << "\n" << kernel;
    }
    else {
        int ArgNum = 0;

        std::string qual("\n__kernel void ");
        UName = getUniqueKernelName(prefix + type);
        std::string kernel = qual + UName;
        kernel += "(";
        kernel += MakeParams(EntryDI,/*MakeDefinition=*/true,UName,CleanupCode,ArgNum);
        kernel += ") ";
        kernel += "{" + MakeBody(EntryDI,EntrySubStmt) + "}\n";

        assert(CleanupCode.empty() && "only the host code needs cleanup");
    }

    //create host code

#if 0
    int ArgNum = 0;
    std::string StrArgList = MakeParams(DI,/*MakeDefinition=*/false,UName,CleanupCode,ArgNum);
#endif
    KernelCalls++;

    return ObjRefDef(UName,kernel);
}

#if 0
std::string
Stage3_ASTVisitor::CreateNewUniqueEventNameFor(Arg *A) {
    static unsigned EID = 0;

    std::string NewName("__accll_device_code_event_");  //prefix

    //add variable name to event name to correctly separate and identify
    //async events without Arg or with RawExprArgICE

    if (A) {
        std::string AStr;
        if (isa<RawExprArg>(A) && A->isICE()) {
            NewName += "const_";
            AStr = A->getICE().toString(10);
        }
        else {
            AStr = A->getPrettyArg(PrintingPolicy(Context->getLangOpts()));
            ReplaceStringInPlace(AStr,"->","_");  //delimiter
            ReplaceStringInPlace(AStr,".","_");   //delimiter
        }
        if (isa<ArrayElementArg>(A)) {
            ReplaceStringInPlace(AStr,"[","_");  //delimiter
            ReplaceStringInPlace(AStr,"]","_");  //delimiter
        }
        NewName += AStr;
    }
    else
        NewName += "implicit_";

    raw_string_ostream OS(NewName);
    OS << "_" << EID;
    EID++;

    return OS.str();
}
#endif