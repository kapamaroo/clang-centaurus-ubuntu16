#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/TargetSelect.h"

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/Refactoring.h"

#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"

#include "clang/Rewrite/Core/Rewriter.h"

#include "Stages.hpp"
#include "ClangFormat.hpp"

#include <iostream>
#include <fstream>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

using namespace llvm;
using namespace clang;
using namespace clang::tooling;
using namespace accll;

///////////////////////////////////////////////////////////////////////////////
//                        Main Tool
///////////////////////////////////////////////////////////////////////////////

int runClang(std::string Path, SmallVector<const char *, 256> &cli) {
    using namespace clang::driver;

    /////////////////////////////////////////////////////////////////////////////////////
    //    strip the driver
    /////////////////////////////////////////////////////////////////////////////////////
    IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
    TextDiagnosticPrinter *DiagClient =
        new TextDiagnosticPrinter(llvm::errs(), &*DiagOpts);
    IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
    DiagnosticsEngine Diags(DiagID, &*DiagOpts, DiagClient);

    Driver TheDriver(Path, llvm::sys::getDefaultTargetTriple(), "a.out", Diags);
    TheDriver.setTitle("clang");

    llvm::InitializeAllTargets();

    OwningPtr<Compilation> C(TheDriver.BuildCompilation(cli));
    if (!C.get())
        return 1;

    int Res = 0;
    SmallVector<std::pair<int, const Command *>, 4> FailingCommands;
    Res = TheDriver.ExecuteCompilation(*C,FailingCommands);

    if (FailingCommands.size())
        if (!Res)
            Res = FailingCommands.front().first;

    return Res;
}

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static cl::extrahelp MoreHelp("\nInvocation\n\t./acl input-files [-- compiler-flags]\n\n");

int main(int argc, const char **argv) {
    int ExtraArgsStartPos = 0;
    for (int i=1; i<argc; ++i)
        if (strlen(argv[i]) == 2 && argv[i][0] == '-' && argv[i][1] == '-' && i + 1 < argc) {
            ExtraArgsStartPos = i;
            break;
        }
    int tool_argc = ExtraArgsStartPos ? ExtraArgsStartPos : argc;

    std::vector<std::string> conf;
    conf.push_back("--");
    conf.push_back("-fopenacc");
    conf.push_back("-I/opt/LLVM/include");
    conf.push_back("-include__acl_api_types.h");
    conf.push_back("-c");
    //conf.push_back("-DCLK_LOCAL_MEM_FENCE=1");
    //conf.push_back("-DCLK_GLOBAL_MEM_FENCE=2");

    int ARGC = tool_argc + conf.size();
    const char **ARGV = new const char*[ARGC];

    for (int i=0; i<tool_argc; ++i)
        ARGV[i] = argv[i];
    for (std::vector<std::string>::size_type i=0; i<conf.size(); ++i)
        ARGV[tool_argc + i] = conf[i].c_str();

    CommonOptionsParser OptionsParser(ARGC, ARGV);

    std::vector<std::string> UserInputFiles = OptionsParser.getSourcePathList();

    if (UserInputFiles.size() != 1) {
        llvm::outs() << "Unsupported multiple input fils  -  exit.\n";
        return 1;
    }

    //After Stage0, proccess only the files that contain OpenACC Directives
    std::vector<std::string> InputFiles;
    std::vector<std::string> LibOCLFiles;
    std::vector<std::string> KernelFiles;

    llvm::outs() << "\n\n#######     Stage0     #######\n\n";
    llvm::outs() << "Check input files ...\n\n";
    RefactoringTool Tool0(OptionsParser.getCompilations(), UserInputFiles);
    Stage0_ConsumerFactory Stage0(InputFiles,KernelFiles);
    if (Tool0.runAndSave(newFrontendActionFactory(&Stage0))) {
        llvm::errs() << "Stage0 failed - exit.\n";
        return 1;
    }

    if (InputFiles.empty()) {
        llvm::outs() << "Source code has no directives, nothing to do - exit\n";
        return 1;
    }

    llvm::outs() << "\n\n#######     Stage1     #######\n\n";
    llvm::outs() << "Generate new scopes, data moves, renaming ...\n";
    RefactoringTool Tool1(OptionsParser.getCompilations(), InputFiles);
    Stage1_ConsumerFactory Stage1(Tool1.getReplacements(),LibOCLFiles,KernelFiles);
    if (Tool1.runAndSave(newFrontendActionFactory(&Stage1))) {
        llvm::errs() << "Stage1 failed - exit.\n";
        return 1;
    }

#if 0
    llvm::outs() << "\n\n#######     Stage3     #######\n\n";
    llvm::outs() << "Write new kernels to separate '*.cl' files ...\n";
    llvm::outs() << "Generate OpenCL API calls on host program ...\n";
    RefactoringTool Tool3(OptionsParser.getCompilations(), InputFiles);
    Stage3_ConsumerFactory Stage3(Tool3.getReplacements(),KernelFiles);
    if (Tool3.runAndSave(newFrontendActionFactory(&Stage3))) {
        llvm::errs() << "Stage3 failed - exit.\n";
        return 1;
    }
#endif

#if 0
    // Clang does not like the keyword 'static' for function declarations.
    // The underlying OpenCL compiler is happy with it.
    if (!KernelFiles.empty()) {
        std::vector<std::string> kernel_conf;
        /* clang
           -S
           -emit-llvm
           -o test.ll
           -x cl
           -I./libclc/generic/include
           -include clc/clc.h
           -Dcl_clang_storage_class_specifiers
           -w
        */

        kernel_conf.push_back("--");
        kernel_conf.push_back("-S");
        kernel_conf.push_back("-o tmp.ll");
        kernel_conf.push_back("-I.");
        //kernel_conf.push_back("-I./libclc/generic/include");
        //kernel_conf.push_back("-include clc/clc.h");
        //kernel_conf.push_back("-Dcl_clang_storage_class_specifiers");
        kernel_conf.push_back("-Wno-implicit-function-declaration");
        kernel_conf.push_back("-x");
        kernel_conf.push_back("cl");
        //kernel_conf.push_back("-w");

        int KernelARGC = 1 + KernelFiles.size() + kernel_conf.size();
        const char **KernelARGV = new const char*[1 + KernelFiles.size() + kernel_conf.size()];

        KernelARGV[0] = argv[0];
        int i = 1;
        for (std::vector<std::string>::iterator II = KernelFiles.begin(),
                 EE = KernelFiles.end(); II != EE; ++II,++i)
            KernelARGV[i] = (*II).c_str();
        for (std::vector<std::string>::size_type i=0; i<kernel_conf.size(); ++i)
            KernelARGV[1 + KernelFiles.size() + i] = kernel_conf[i].c_str();

        llvm::outs() << "Check new generated device files ...\n";
        CommonOptionsParser KernelOptionsParser(KernelARGC, KernelARGV);
        ClangTool Tool6(KernelOptionsParser.getCompilations(),KernelFiles);
        if (Tool6.run(newFrontendActionFactory<SyntaxOnlyAction>())) {
            llvm::errs() << "FATAL: __internal_error__: illegal device code  -  Exit.\n";
            return 1;
        }
    }
#endif

    llvm::outs() << "\n\n##############################\n\n";
    llvm::outs() << "Format new generated source files ...\n";

    int status = 0;

    std::string Style = "LLVM";
    for (std::vector<std::string>::iterator
             II = InputFiles.begin(),
             EE = InputFiles.end(); II != EE; ++II) {
        status += clang_format_main(*II,Style);
    }
#if 0
    for (std::vector<std::string>::iterator
             II = LibOCLFiles.begin(),
             EE = LibOCLFiles.end(); II != EE; ++II) {
        status += clang_format_main(*II,Style);
    }
#endif
    for (std::vector<std::string>::iterator
             II = KernelFiles.begin(),
             EE = KernelFiles.end(); II != EE; ++II) {
        status += clang_format_main(*II,Style);
    }

    llvm::outs() << "\n\n##############################\n\n";
    llvm::outs() << "Check new generated source files ...\n";

    {
        ClangTool Tool5(OptionsParser.getCompilations(),InputFiles);
        if (Tool5.run(newFrontendActionFactory<SyntaxOnlyAction>())) {
            llvm::errs() << "FATAL: __internal_error__: illegal generated source code  -  Exit.\n";
            return 1;
        }
    }

#if 0
    {
        ClangTool Tool5(OptionsParser.getCompilations(),KernelFiles);
        if (Tool5.run(newFrontendActionFactory<SyntaxOnlyAction>())) {
            llvm::errs() << "FATAL: __internal_error__: illegal generated source code  -  Exit.\n";
            return 1;
        }
    }
#endif

    {
        ClangTool Tool5(OptionsParser.getCompilations(),LibOCLFiles);
        if (Tool5.run(newFrontendActionFactory<SyntaxOnlyAction>())) {
            llvm::errs() << "FATAL: __internal_error__: illegal generated source code  -  Exit.\n";
            return 1;
        }
    }

    llvm::outs() << "\n\n##############################\n\n";
    llvm::outs() << "New source files are valid\n" << "Generate object files ...\n\n";

    std::string ObjFile = RemoveDotExtension(UserInputFiles.front()) + ".o";
    ObjFile = GetBasename(ObjFile);
    {
        int Res = 0;

        SmallVector<const char *, 256> cli;

        std::string ClangPath = "/opt/LLVM/build-dev/bin/clang-3.3";

        //cli.push_back("-###");
        cli.push_back("-Wall");
        cli.push_back("-fopenacc");
        cli.push_back("-I/opt/LLVM/include");
        cli.push_back("-c");

        bool CompileOnly = false;
        std::string UserDefinedOutputFile;
        if (ExtraArgsStartPos)
            for (int i=ExtraArgsStartPos+1; i<argc; ++i) {
                if (strcmp(argv[i],"-c") == 0) {
                    CompileOnly = true;
                    continue;
                }
                else if (strcmp(argv[i],"-o") == 0 && i + 1 < argc) {
                    UserDefinedOutputFile = argv[i+1];
                    ++i;
                    continue;
                }
                cli.push_back(argv[i]);
            }

        std::vector<std::string> TmpObjList;

        cli.push_back("-include__acl_api_types.h");
        for (std::vector<std::string>::iterator
                 II = InputFiles.begin(),
                 EE = InputFiles.end(); II != EE; ++II) {
            if (GetDotExtension(*II).compare(".c") != 0)
                continue;
            std::string obj = RemoveDotExtension(*II) + ".o";
            obj = GetBasename(obj);
            llvm::outs() << obj << "\n";
            TmpObjList.push_back(obj);

            cli.push_back(II->c_str());
            cli.push_back("-o");
            cli.push_back(obj.c_str());
            Res = runClang(ClangPath,cli);
            cli.pop_back();
            cli.pop_back();
            cli.pop_back();
        }
        cli.pop_back();

        for (std::vector<std::string>::iterator
                 II = LibOCLFiles.begin(),
                 EE = LibOCLFiles.end(); II != EE; ++II) {
            if (GetDotExtension(*II).compare(".c") != 0)
                continue;
            std::string obj = RemoveDotExtension(*II) + ".o";
            obj = GetBasename(obj);
            llvm::outs() << obj << "\n";
            TmpObjList.push_back(obj);

            cli.push_back(II->c_str());
            cli.push_back("-o");
            cli.push_back(obj.c_str());
            Res = runClang(ClangPath,cli);
            cli.pop_back();
            cli.pop_back();
            cli.pop_back();
        }

        if (CompileOnly && UserDefinedOutputFile.size())
            ObjFile = UserDefinedOutputFile;

        {
            std::string LinkerPath = "/usr/bin/ld";

            SmallVector<const char *, 256> ldcli;
            ldcli.push_back(LinkerPath.c_str());
            ldcli.push_back("-r");
            for (std::vector<std::string>::iterator
                     II = TmpObjList.begin(), EE = TmpObjList.end(); II != EE; ++II)
                ldcli.push_back(II->c_str());
            ldcli.push_back("-o");
            ldcli.push_back(ObjFile.c_str());
            ldcli.push_back(0);

            llvm::outs() << "\n\n##############################\n\n";
            llvm::outs() << "Link partial object files to: " << ObjFile << " ...\n";

            int pid  = fork();
            if (pid < 0) {
                llvm::outs() << "fork() failed  -  exit.\n";
                return 1;
            }
            if (!pid) {
                //execvp(LinkerPath.c_str(),(char * const *)ldcli.data());
                execlp(LinkerPath.c_str(),LinkerPath.c_str(),"-r",TmpObjList[0].c_str(),TmpObjList[1].c_str(),"-o",ObjFile.c_str(),(char *)NULL);
                llvm::outs() << "Linker failed  -  exit.\n";
                return 1;
            }
            waitpid(pid,NULL,0);
        }

        if (!CompileOnly) {
            llvm::outs() << "\n\n##############################\n\n";
            llvm::outs() << "Link executable ...\n\n";

            SmallVector<const char *, 256> ldcli;
            ldcli.push_back(ObjFile.c_str());
            llvm::outs() << ldcli.back() << "\n";
            //ldcli.push_back("-v");
            if (UserDefinedOutputFile.size()) {
                llvm::outs() << UserDefinedOutputFile << "\n";
                ldcli.push_back("-o");
                ldcli.push_back(UserDefinedOutputFile.c_str());
            }
            runClang(ClangPath,ldcli);
        }
    }

    llvm::llvm_shutdown();

    return 0;
}
