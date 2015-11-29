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

#include "Common.hpp"
#include "Stages.hpp"
#include "CentaurusConfig.hpp"
#include "ClangFormat.hpp"

#include <iostream>
#include <fstream>
#include <cstdlib>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

using namespace llvm;
using namespace clang;
using namespace clang::tooling;
using namespace acl;

///////////////////////////////////////////////////////////////////////////////
//                        Main Tool
///////////////////////////////////////////////////////////////////////////////

static llvm::cl::OptionCategory aclCategory("acl options");

int runClang(const acl::CentaurusConfig &Config, std::string Path, SmallVector<const char *, 256> &cli) {
#if 1
    llvm::outs() << "\n" << DEBUG << Path << " ";
    for (SmallVector<const char *, 256>::iterator
             II = cli.begin(), EE = cli.end(); II != EE; ++II) {
        llvm::outs() << *II << " ";
    }
    llvm::outs() << "\n";
#endif

    using namespace clang::driver;

    /////////////////////////////////////////////////////////////////////////////////////
    //    strip the driver
    /////////////////////////////////////////////////////////////////////////////////////
    IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
    TextDiagnosticPrinter *DiagClient =
        new TextDiagnosticPrinter(llvm::errs(), &*DiagOpts);
    IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
    DiagnosticsEngine Diags(DiagID, &*DiagOpts, DiagClient);

    //llvm::StringRef A_OUT = "a.out";
    Driver TheDriver(Path, llvm::sys::getDefaultTargetTriple(), Diags);
    TheDriver.setTitle("centaurus");

    llvm::InitializeAllTargets();

#if 0
    if (Config.isCXX) {
        cli.push_back("--driver-mode=g++");
        llvm::outs() << DEBUG << "request for C++\n";
    }
#endif

    std::unique_ptr<Compilation> C(TheDriver.BuildCompilation(cli));
    if (!C.get())
        return 1;

    // llvm::outs() << DEBUG << "CPP_MODE = " << TheDriver.CCCIsCXX() << "\n";

    int Res = 0;
    SmallVector<std::pair<int, const Command *>, 4> FailingCommands;
    Res = TheDriver.ExecuteCompilation(*C,FailingCommands);

    if (FailingCommands.size())
        if (!Res)
            Res = FailingCommands.front().first;

    return Res;
}

int CheckGeneratedSourceFiles(int argc, const char *argv[], const acl::CentaurusConfig &Config) {
    llvm::outs() << "Check new generated source files ... ";

    SmallVector<const char *, 256> ARGV;

    ARGV.push_back(argv[0]);

    for (std::vector<std::string>::const_iterator
             II = Config.InputFiles.begin(),
             EE = Config.InputFiles.end(); II != EE; ++II)
        ARGV.push_back(II->c_str());

    std::string IncludeFlag("-I" + Config.IncludePath);
    //std::string LibFlag("-L" + LibPath);

    ARGV.push_back("--");
    //ARGV.push_back("-fcentaurus");
    ARGV.push_back("-D_GNU_SOURCE");
    ARGV.push_back(IncludeFlag.c_str());
    //ARGV.push_back("-include__acl_api_types.h");
    ARGV.push_back("-include__acl_sys_impl.h");
    //ARGV.push_back("-c");

    for (std::vector<std::string>::const_iterator
             II = Config.ExtraCompilerFlags.begin(),
             EE = Config.ExtraCompilerFlags.end(); II != EE; ++II) {
        ARGV.push_back(II->c_str());
    }

    int ARGC = ARGV.size();

#if 0
    llvm::outs() << DEBUG << "Invoke Stages as: ";
    for (size_t i=0; i<ARGV.size(); ++i)
        llvm::outs() << ARGV[i] + std::string(" ");
    llvm::outs() << "\n";
#endif

    CommonOptionsParser OptionsParser(ARGC, ARGV.data(), aclCategory);

    {
        ClangTool Tool5(OptionsParser.getCompilations(),Config.OutputFiles);
        if (Tool5.run(newFrontendActionFactory<SyntaxOnlyAction>().get())) {
            llvm::errs() << "\nFATAL: __internal_error__: illegal generated source code  -  Exit.\n";
            return 1;
        }
    }

#if 0
    {
        ClangTool Tool5(OptionsParser.getCompilations(),Config.KernelFiles);
        if (Tool5.run(newFrontendActionFactory<SyntaxOnlyAction>().get())) {
            llvm::errs() << "\nFATAL: __internal_error__: illegal generated source code  -  Exit.\n";
            return 1;
        }
    }
#endif

    {
        ClangTool Tool5(OptionsParser.getCompilations(),Config.LibOCLFiles);
        if (Tool5.run(newFrontendActionFactory<SyntaxOnlyAction>().get())) {
            llvm::errs() << "\nFATAL: __internal_error__: illegal generated source code  -  Exit.\n";
            return 1;
        }
    }

    llvm::outs() << "OK\n";
    return 0;
}

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static cl::extrahelp MoreHelp("\nInvocation\n\t./acl input-files [-- compiler-flags]\n\n");

int main(int argc, const char *argv[]) {
    acl::CentaurusConfig Config(argc,argv);
    //Config.print();

    if (Config.NoArgs) {
        llvm::outs() << "No input, try '" << argv[0] << " --help' for more information  -  exit.\n";
        return 0;
    }

    if (!Config.InputFiles.size()) {
        //treat as raw clang invocation
        //llvm::outs() << WARNING << "no input files, enter clang mode.\n";

        SmallVector<const char *, 256> cli;

        cli.push_back("-lcentaurus");
        //cli.push_back("-lcentaurusapi");

        for (std::vector<std::string>::iterator
                 II = Config.ExtraCompilerFlags.begin(),
                 EE = Config.ExtraCompilerFlags.end(); II != EE; ++II)
            cli.push_back(II->c_str());

        if (Config.CompileOnly)
            cli.push_back("-c");

        if (Config.UserDefinedOutputFile.size()) {
            cli.push_back("-o");
            cli.push_back(Config.UserDefinedOutputFile.c_str());
        }

        for (std::vector<std::string>::iterator
                 II = Config.ExtraLinkerFlags.begin(),
                 EE = Config.ExtraLinkerFlags.end(); II != EE; ++II)
            cli.push_back(II->c_str());

        std::string LibPathFlag("-L" + Config.LibPath);
        cli.push_back(LibPathFlag.c_str());
        cli.push_back("-lcentaurus");
        // cli.push_back("-lcentaurusapi");

        // -lpthread -lOpenCL -ldl -lrt -lm -lnvidia-ml -Lpapi-5.4.1/src/ -lpapi

        cli.push_back("-L/usr/lib/nvidia-340");
        cli.push_back("-L/usr/lib/nvidia-346");
        cli.push_back("-L/usr/lib/nvidia-352");

        cli.push_back("-lnvidia-ml");
        cli.push_back("-lpthread");
        cli.push_back("-ldl");
        cli.push_back("-lrt");
        cli.push_back("-lm");

        runClang(Config,Config.ClangPath,cli);

        //llvm::outs() << DEBUG << "Exit clang mode.\n";

        return 0;
    }

    SmallVector<const char *, 256> ARGV;

    ARGV.push_back(argv[0]);

    for (std::vector<std::string>::iterator
             II = Config.InputFiles.begin(),
             EE = Config.InputFiles.end(); II != EE; ++II)
        ARGV.push_back(II->c_str());

    std::string IncludeFlag("-I" + Config.IncludePath);
    std::string CustomSystemHeadersFlag("-I" + Config.CustomSystemHeaders);
    //std::string LibFlag("-L" + LibPath);

    ARGV.push_back("--");
    ARGV.push_back("-fcentaurus");
    ARGV.push_back("-D_GNU_SOURCE");
    ARGV.push_back(IncludeFlag.c_str());
    ARGV.push_back(CustomSystemHeadersFlag.c_str());
    ARGV.push_back("-include__acl_api_types.h");

    // hide warnings related to redeclaration of OpenCL builtins on top of system builtins
    ARGV.push_back("-Wno-incompatible-library-redeclaration");

    ARGV.push_back("-c");

    for (std::vector<std::string>::iterator
             II = Config.ExtraCompilerFlags.begin(),
             EE = Config.ExtraCompilerFlags.end(); II != EE; ++II) {
        ARGV.push_back(II->c_str());
    }

    int ARGC = ARGV.size();

#if 0
    llvm::outs() << DEBUG << "Invoke Stages as: ";
    for (size_t i=0; i<ARGV.size(); ++i)
        llvm::outs() << ARGV[i] + std::string(" ");
    llvm::outs() << "\n";
#endif

    CommonOptionsParser OptionsParser(ARGC, ARGV.data(), aclCategory);

    Config.InputFiles = OptionsParser.getSourcePathList();

    if (Config.InputFiles.size() != 1) {
        llvm::outs() << "Unsupported multiple input files  -  exit.\n";
        return 1;
    }

    //After Stage0, proccess only the files that contain Centaurus Directives and main() function

    llvm::outs() << "Stage0: Check input files ...\n";
    RefactoringTool Tool0(OptionsParser.getCompilations(), Config.InputFiles);
    Stage0_ConsumerFactory Stage0(Config,Config.OutputFiles,Config.RegularFiles);
    if (Tool0.runAndSave(newFrontendActionFactory(&Stage0).get())) {
        llvm::errs() << "Stage0 failed - exit.\n";
        return 1;
    }

    if (!Config.RegularFiles.empty()) {
        //llvm::outs() << WARNING << "Source code has no directives, enter clang mode.\n";

        SmallVector<const char *, 256> cli;

        cli.push_back("-Wall");

        for (std::vector<std::string>::iterator
                 II = Config.RegularFiles.begin(),
                 EE = Config.RegularFiles.end(); II != EE; ++II)
            cli.push_back(II->c_str());

        for (std::vector<std::string>::iterator
                 II = Config.ExtraCompilerFlags.begin(),
                 EE = Config.ExtraCompilerFlags.end(); II != EE; ++II)
            cli.push_back(II->c_str());

        if (Config.CompileOnly)
            cli.push_back("-c");

        for (std::vector<std::string>::iterator
                 II = Config.ExtraLinkerFlags.begin(),
                 EE = Config.ExtraLinkerFlags.end(); II != EE; ++II)
            cli.push_back(II->c_str());

        runClang(Config,Config.ClangPath,cli);

        //llvm::outs() << DEBUG << "Exit clang mode.\n";

        return 0;
    }

    if (Config.OutputFiles.empty()) {
        llvm::outs() << "UNEXPECTED ERROR: Source code has no directives, nothing to do - exit.\n";
        return 1;
    }

    llvm::outs() << "Stage1: Transform source code ...\n";
    RefactoringTool Tool1(OptionsParser.getCompilations(), Config.OutputFiles);
    Stage1_ConsumerFactory Stage1(Config,Tool1.getReplacements(),Config.LibOCLFiles,Config.KernelFiles);
    if (Tool1.runAndSave(newFrontendActionFactory(&Stage1).get())) {
        llvm::errs() << "Stage1 failed - exit.\n";
        return 1;
    }

#if 0
    // Clang does not like the keyword 'static' for function declarations.
    // The underlying OpenCL compiler is happy with it.
    if (!Config.KernelFiles.empty()) {
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

        int KernelARGC = 1 + Config.KernelFiles.size() + kernel_conf.size();
        const char **KernelARGV = new const char*[1 + Config.KernelFiles.size() + kernel_conf.size()];

        KernelARGV[0] = argv[0];
        int i = 1;
        for (std::vector<std::string>::iterator II = Config.KernelFiles.begin(),
                 EE = Config.KernelFiles.end(); II != EE; ++II,++i)
            KernelARGV[i] = (*II).c_str();
        for (std::vector<std::string>::size_type i=0; i<kernel_conf.size(); ++i)
            KernelARGV[1 + Config.KernelFiles.size() + i] = kernel_conf[i].c_str();

        llvm::outs() << "Check new generated device files ...\n";
        CommonOptionsParser KernelOptionsParser(KernelARGC, KernelARGV, aclCategory);
        ClangTool Tool6(KernelOptionsParser.getCompilations(),Config.KernelFiles);
        if (Tool6.run(newFrontendActionFactory<SyntaxOnlyAction>().get())) {
            llvm::errs() << "FATAL: __internal_error__: illegal device code  -  Exit.\n";
            return 1;
        }
    }
#endif

    llvm::outs() << "\n";
    llvm::outs() << "Format new generated source files ... ";

    int status = 0;

    std::string Style = "LLVM";
    for (std::vector<std::string>::iterator
             II = Config.OutputFiles.begin(),
             EE = Config.OutputFiles.end(); II != EE; ++II) {
        status += clang_format_main(*II,Style);
    }
#if 0
    for (std::vector<std::string>::iterator
             II = Config.LibOCLFiles.begin(),
             EE = Config.LibOCLFiles.end(); II != EE; ++II) {
        status += clang_format_main(*II,Style);
    }
#endif
    for (std::vector<std::string>::iterator
             II = Config.KernelFiles.begin(),
             EE = Config.KernelFiles.end(); II != EE; ++II) {
        status += clang_format_main(*II,Style);
    }

    //errs() << "status = " << status << "\n";
    if (status != 0) {
        llvm::outs() << "Fail\n";
        return 1;
    }
    llvm::outs() << "OK\n";

#if 1
    if (CheckGeneratedSourceFiles(argc,argv,Config))
        return 1;
#endif

    llvm::outs() << "Generate temporary object files ... ";

    {
        SmallVector<const char *, 256> cli;

        //cli.push_back("-###");
        cli.push_back("-Wall");
        //cli.push_back("-fcentaurus");
        //cli.push_back("-Wno-gnu-designator");
        cli.push_back(IncludeFlag.c_str());
        cli.push_back("-D_GNU_SOURCE");
        cli.push_back("-include__acl_sys_impl.h");
        cli.push_back("-c");

        for (std::vector<std::string>::iterator
                 II = Config.ExtraCompilerFlags.begin(),
                 EE = Config.ExtraCompilerFlags.end(); II != EE; ++II)
            cli.push_back(II->c_str());

        int Res = 0;

        std::vector<std::string> TmpObjList;

        for (std::vector<std::string>::iterator
                 II = Config.OutputFiles.begin(),
                 EE = Config.OutputFiles.end(); II != EE; ++II) {
#if 0
            if (GetDotExtension(*II).compare(".c") != 0)
                continue;
#endif
            std::string obj = RemoveDotExtension(*II) + ".o";
            obj = GetBasename(obj);
            //llvm::outs() << obj << "\n";
            TmpObjList.push_back(obj);

            cli.push_back(II->c_str());
            cli.push_back("-o");
            cli.push_back(obj.c_str());
            Res += runClang(Config,Config.ClangPath,cli);
            cli.pop_back();
            cli.pop_back();
            cli.pop_back();
        }

        for (std::vector<std::string>::iterator
                 II = Config.LibOCLFiles.begin(),
                 EE = Config.LibOCLFiles.end(); II != EE; ++II) {
            if (GetDotExtension(*II).compare(".c") != 0)
                continue;
            std::string obj = RemoveDotExtension(*II) + ".o";
            obj = GetBasename(obj);
            //llvm::outs() << obj << "\n";
            TmpObjList.push_back(obj);

            cli.push_back(II->c_str());
            cli.push_back("-o");
            cli.push_back(obj.c_str());
            Res += runClang(Config,Config.ClangPath,cli);
            cli.pop_back();
            cli.pop_back();
            cli.pop_back();
        }

        if (Res) {
            llvm::outs() << "Fail\n";
            return 1;
        }
        llvm::outs() << "OK\n";

        std::string ObjFile = RemoveDotExtension(Config.InputFiles.front()) + ".o";
        ObjFile = GetBasename(ObjFile);

        if (Config.CompileOnly && Config.UserDefinedOutputFile.size())
            ObjFile = Config.UserDefinedOutputFile;

        {
            SmallVector<const char *, 256> ldcli;
            ldcli.push_back(Config.LinkerPath.c_str());
            ldcli.push_back("-r");
            for (std::vector<std::string>::iterator
                     II = TmpObjList.begin(), EE = TmpObjList.end(); II != EE; ++II)
                ldcli.push_back(II->c_str());
            ldcli.push_back("-o");
            ldcli.push_back(ObjFile.c_str());
            ldcli.push_back(0);

            llvm::outs() << "Merge temporary object files to '" << ObjFile << "': ... ";
#if 0
            llvm::outs() << DEBUG << " ";
            for (SmallVector<const char *, 256>::iterator
                     II = ldcli.begin(), EE = ldcli.end()-1; II != EE; ++II) {
                llvm::outs() << *II << " ";
            }
            llvm::outs() << "\n";
#endif

            int pid  = fork();
            if (pid < 0) {
                llvm::outs() << "fork() failed  -  exit.\n";
                return 1;
            }
            if (!pid) {
                execvp(Config.LinkerPath.c_str(),(char * const *)const_cast<char **>(ldcli.data()));
                llvm::outs() << "Fail : execlp()  -  exit.\n";
                return 1;
            }

            int status;
            if (waitpid(pid,&status,0) < 0) {
                llvm::outs() << "Fail : waitpid()  -  exit.\n";
                return 1;
            }

            if (!WIFEXITED(status)) {
                llvm::outs() << "Fail : linker termination  -  exit.\n";
                return 1;
            }

            if (WEXITSTATUS(status)) {
                llvm::outs() << "Fail : linker returned " << WEXITSTATUS(status) << "\n";
                return 1;
            }
            llvm::outs() << "OK\n";

            llvm::outs() << "Remove temporary object files ... ";
            for (std::vector<std::string>::iterator
                     II = TmpObjList.begin(), EE = TmpObjList.end(); II != EE; ++II)
                if (unlink(II->c_str())) {
                    llvm::outs() << "Fail : unlink()  -  exit.\n";
                    return 1;
                }
            llvm::outs() << "OK\n";
        }

        if (!Config.CompileOnly) {
            llvm::outs() << "Link with runtime ... ";

#if 1
            int pid  = fork();
            if (pid < 0) {
                llvm::outs() << "fork() failed  -  exit.\n";
                return 1;
            }
            if (!pid) {
                SmallVector<const char *, 256> ldcli;
                ldcli.push_back(Config.ClangPath.c_str());
                ldcli.push_back(ObjFile.c_str());

                std::string LibPathFlag("-L" + Config.LibPath);
                ldcli.push_back(LibPathFlag.c_str());

                ldcli.push_back("-L/usr/lib/nvidia-340");
                ldcli.push_back("-L/usr/lib/nvidia-346");
                ldcli.push_back("-L/usr/lib/nvidia-352");

                ldcli.push_back("-lnvidia-ml");
                ldcli.push_back("-lpthread");
                ldcli.push_back("-ldl");
                ldcli.push_back("-lrt");
                ldcli.push_back("-lm");

                ldcli.push_back("-lcentaurus");
                // ldcli.push_back("-lcentaurusapi");

                //std::string LibStaticRuntime(LibPath + "/libcentaurus.a")
                //ldcli.push_back(LibStaticRuntime.c_str());
                if (Config.UserDefinedOutputFile.size()) {
                    ldcli.push_back("-o");
                    ldcli.push_back(Config.UserDefinedOutputFile.c_str());
                }

                //ldcli.push_back("-v");

                for (std::vector<std::string>::iterator
                         II = Config.ExtraLinkerFlags.begin(),
                         EE = Config.ExtraLinkerFlags.end(); II != EE; ++II)
                    ldcli.push_back(II->c_str());

#if 1
                llvm::outs() << DEBUG;
                for (SmallVector<const char *, 256>::iterator
                         II = ldcli.begin(), EE = ldcli.end(); II != EE; ++II) {
                    llvm::outs() << *II << " ";
                }
                llvm::outs() << "\n";
#endif

                ldcli.push_back(0);

                execvp(Config.ClangPath.c_str(),(char * const *)const_cast<char **>(ldcli.data()));
                llvm::outs() << "Fail : execlp()  -  exit.\n";
                return 1;
            }

            int status;
            if (waitpid(pid,&status,0) < 0) {
                llvm::outs() << "Fail : waitpid()  -  exit.\n";
                return 1;
            }

            if (!WIFEXITED(status)) {
                llvm::outs() << "Fail : linker termination  -  exit.\n";
                return 1;
            }

            if (WEXITSTATUS(status)) {
                llvm::outs() << "Fail : linker returned " << WEXITSTATUS(status) << "\n";
                return 1;
            }
            llvm::outs() << "OK\n";
#else
            SmallVector<const char *, 256> ldcli;
            ldcli.push_back(ObjFile.c_str());
            llvm::outs() << ldcli.back() << "\n";
            //ldcli.push_back("-v");
            if (UserDefinedOutputFile.size()) {
                llvm::outs() << UserDefinedOutputFile << "\n";
                ldcli.push_back("-o");
                ldcli.push_back(UserDefinedOutputFile.c_str());
            }
            int Res = runClang(Config,Config.ClangPath,ldcli);
            if (Res) {
                llvm::outs() << "Fail\n";
                return 1;
            }
#endif
        }

        llvm::outs() << "Success!\n";
    }

    llvm::llvm_shutdown();

    return 0;
}

acl::CentaurusConfig::CentaurusConfig(int argc, const char *argv[]) :
    ProfileMode(false), CompileOnly(false), isCXX(false), NoArgs(false)
{
    if (const char *path = std::getenv("CENTAURUS_INSTALL_PATH"))
        InstallPath = path;
    else
        InstallPath = "/opt/Centaurus";
    IncludePath = InstallPath + "/include/centaurus";
    CustomSystemHeaders = InstallPath + "/include/system";
    LibPath = InstallPath + "/lib";
    LinkerPath = "/usr/bin/ld";
    ClangPath = InstallPath + "/bin/clang";

    // clang -emit-llvm -target spir -include ~/centaurus/acl/opencl_spir.h -S -o clang.ll ~/centaurus/acl/input.cl
    // ioc64 -cmd=compile -input=./input.cl -llvm-spir32=intel.ll
    SPIRToolPath = "ioc64";

    StringRef Path = argv[0];
    if (Path.endswith("++")) {
        ClangPath += "++";
        isCXX = true;
    }

    if (argc == 1) {
        NoArgs = true;
        return;
    }

    int i = 1;

    for (; i<argc; ++i) {
        const std::string Option(argv[i]);
        if (Option.compare("--") == 0) {
            if (argc == 2 && i == 1) {
                NoArgs = true;
                return;
            }
            ++i;
            break;
        }
        if (Option.compare("--profile") == 0)
            ProfileMode = true;
        else
            InputFiles.push_back(Option);
    }

    for (; i<argc; ++i) {
        const std::string Option(argv[i]);
        if (Option.compare("-o") == 0) {
            if (i + 1 < argc) {
                UserDefinedOutputFile = argv[i+1];
                i++;
            }
        }
        else if (Option.compare("-c") == 0) {
            CompileOnly = true;
        }
        else if (Option.compare(0,2,"-l") == 0 || Option.compare(0,2,"-L") == 0)
            ExtraLinkerFlags.push_back(Option);
        else if (Option.compare("-pg") == 0) {
            ExtraCompilerFlags.push_back(Option);
            // workaround for this:
            // [LLVMdev] Program compiled with Clang -pg and -O crashes with SEGFAULT
            // http://lists.llvm.org/pipermail/llvm-dev/2013-July/064107.html
            ExtraCompilerFlags.push_back("-fno-omit-frame-pointer");
        }
        else
            ExtraCompilerFlags.push_back(Option);
    }

}

void
acl::CentaurusConfig::print() const {
#define PRINT(x) #x << " = " << x << "\n"
    llvm::outs() << DEBUG
                 << "\nCentaurus Configuration:\n"
                 << PRINT(ProfileMode)
                 << PRINT(CompileOnly)
                 << PRINT(UserDefinedOutputFile)
                 << "\n"
        ;

    llvm::outs() << "\nExtra Compiler Flags\n";
    for (std::vector<std::string>::const_iterator
             II = ExtraCompilerFlags.begin(),
             EE = ExtraCompilerFlags.end(); II != EE; ++II)
        llvm::outs() << "\t" << *II << "\n";

    llvm::outs() << "\nExtra Linker Flags\n";
    for (std::vector<std::string>::const_iterator
             II = ExtraLinkerFlags.begin(),
             EE = ExtraLinkerFlags.end(); II != EE; ++II)
        llvm::outs() << "\t" << *II << "\n";

    llvm::outs() << "\nInput Files\n";
    for (std::vector<std::string>::const_iterator
             II = InputFiles.begin(),
             EE = InputFiles.end(); II != EE; ++II)
        llvm::outs() << "\t" << *II << "\n";

#undef PRINT
}
