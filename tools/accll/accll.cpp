#include "llvm/Support/CommandLine.h"

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/Refactoring.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"

#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Rewrite/Core/Rewriter.h"

#include "Stages.hpp"
#include "ClangFormat.hpp"

#include <iostream>
#include <fstream>

using namespace llvm;
using namespace clang;
using namespace clang::tooling;
using namespace accll;

///////////////////////////////////////////////////////////////////////////////
//                        Main Tool
///////////////////////////////////////////////////////////////////////////////

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static cl::extrahelp MoreHelp("\nMore help text...\n");

int main(int argc, const char **argv) {
    std::vector<std::string> conf;
    conf.push_back("--");
    conf.push_back("-fopenacc");
    conf.push_back("-I.");
    //conf.push_back("-x");
    //conf.push_back("cl");
    conf.push_back("-w");

    int ARGC = argc + conf.size();
    const char **ARGV = new const char*[ARGC];

    for (int i=0; i<argc; ++i)
        ARGV[i] = argv[i];
    for (std::vector<std::string>::size_type i=0; i<conf.size(); ++i)
        ARGV[argc + i] = conf[i].c_str();

    int _ARGC = ARGC - 1;
    CommonOptionsParser OptionsParser(_ARGC, ARGV);
    CommonOptionsParser OptionsParserSilent(ARGC, ARGV);

    std::vector<std::string> UserInputFiles = OptionsParser.getSourcePathList();

    //After Stage0, proccess only the files that contain OpenACC Directives
    std::vector<std::string> InputFiles;
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
    Stage1_ConsumerFactory Stage1(Tool1.getReplacements(),KernelFiles);
    if (Tool1.runAndSave(newFrontendActionFactory(&Stage1))) {
        llvm::errs() << "Stage1 failed - exit.\n";
        return 1;
    }

#if 0
    llvm::outs() << "\n\n#######     Stage3     #######\n\n";
    llvm::outs() << "Write new kernels to separate '*.cl' files ...\n";
    llvm::outs() << "Generate OpenCL API calls on host program ...\n";
    RefactoringTool Tool3(OptionsParserSilent.getCompilations(), InputFiles);
    Stage3_ConsumerFactory Stage3(Tool3.getReplacements(),KernelFiles);
    if (Tool3.runAndSave(newFrontendActionFactory(&Stage3))) {
        llvm::errs() << "Stage3 failed - exit.\n";
        return 1;
    }
#endif

    std::vector<std::string> RealKernelFiles;
    for (std::vector<std::string>::iterator
             II = KernelFiles.begin(), EE = KernelFiles.end(); II != EE; ++II)
        if (!(*II).empty())
            RealKernelFiles.push_back(*II);

    //format new files
    int status = 0;

    std::string Style = "LLVM";
    for (std::vector<std::string>::iterator
             II = InputFiles.begin(),
             EE = InputFiles.end(); II != EE; ++II)
        status += clang_format_main(*II,Style);

    for (std::vector<std::string>::iterator
             II = RealKernelFiles.begin(),
             EE = RealKernelFiles.end(); II != EE; ++II)
        status += clang_format_main(*II,Style);

    llvm::outs() << "\n\n#######     Stage5     #######\n\n";
    llvm::outs() << "Check new generated host files ...\n";

    ClangTool Tool5(OptionsParser.getCompilations(),InputFiles);
    if (Tool5.run(newFrontendActionFactory<SyntaxOnlyAction>())) {
        llvm::errs() << "FATAL: __internal_error__: illegal host code  -  Exit.\n";
        return 1;
    }

    if (!RealKernelFiles.empty()) {
        std::vector<std::string> kernel_conf;
        kernel_conf.push_back("--");
        kernel_conf.push_back("-I.");
        //kernel_conf.push_back("-w");

        int KernelARGC = 1 + RealKernelFiles.size() + kernel_conf.size();
        const char **KernelARGV = new const char*[1 + RealKernelFiles.size() + kernel_conf.size()];

        KernelARGV[0] = argv[0];
        int i = 1;
        for (std::vector<std::string>::iterator II = RealKernelFiles.begin(),
                 EE = RealKernelFiles.end(); II != EE; ++II,++i)
            KernelARGV[i] = (*II).c_str();
        for (std::vector<std::string>::size_type i=0; i<kernel_conf.size(); ++i)
            KernelARGV[1 + RealKernelFiles.size() + i] = kernel_conf[i].c_str();

        llvm::outs() << "Check new generated device files ...\n";
        CommonOptionsParser KernelOptionsParser(KernelARGC, KernelARGV);
        ClangTool Tool6(KernelOptionsParser.getCompilations(),RealKernelFiles);
        if (Tool6.run(newFrontendActionFactory<SyntaxOnlyAction>())) {
            llvm::errs() << "FATAL: __internal_error__: illegal device code  -  Exit.\n";
            return 1;
        }
    }

    llvm::outs() << "\n\n##############################\n\n";
    llvm::outs() << "New files are valid\n" << "Success!\n\n";

    return 0;
}
