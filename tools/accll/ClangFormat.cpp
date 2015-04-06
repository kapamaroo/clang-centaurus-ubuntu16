//===-- clang-format/ClangFormat.cpp - Clang format tool ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief This file implements a clang-format tool that automatically formats
/// (fragments of) C++ code.
///
//===----------------------------------------------------------------------===//

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Signals.h"

using namespace llvm;

namespace clang {
namespace format {

static FileID createInMemoryFile(StringRef FileName, const MemoryBuffer *Source,
                                 SourceManager &Sources, FileManager &Files) {
  const FileEntry *Entry = Files.getVirtualFile(FileName == "-" ? "<stdin>" :
                                                    FileName,
                                                Source->getBufferSize(), 0);
  Sources.overrideFileContents(Entry, Source, true);
  return Sources.createFileID(Entry, SourceLocation(), SrcMgr::C_User);
}

static FormatStyle getStyle(std::string Style) {
  FormatStyle TheStyle = getGoogleStyle();
  if (Style == "LLVM")
    TheStyle = getLLVMStyle();
  else if (Style == "Chromium")
    TheStyle = getChromiumStyle();
  else if (Style == "Mozilla")
    TheStyle = getMozillaStyle();
  else if (Style != "Google")
    llvm::errs() << "Unknown style " << Style << ", using Google style.\n";

  return TheStyle;
}

// Returns true on error.
static bool format(std::string FileName, std::string Style) {
  FileManager Files((FileSystemOptions()));
  DiagnosticsEngine Diagnostics(
      IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs),
      new DiagnosticOptions);
  SourceManager Sources(Diagnostics, Files);
  OwningPtr<MemoryBuffer> Code;
  if (error_code ec = MemoryBuffer::getFileOrSTDIN(FileName, Code)) {
    llvm::errs() << ec.message() << "\n";
    return true;
  }
  FileID ID = createInMemoryFile(FileName, Code.get(), Sources, Files);
  Lexer Lex(ID, Sources.getBuffer(ID), Sources, getFormattingLangOpts());
  std::vector<CharSourceRange> Ranges;
  SourceLocation Start =
      Sources.getLocForStartOfFile(ID).getLocWithOffset(0);
  SourceLocation End;
  End = Sources.getLocForEndOfFile(ID);
  Ranges.push_back(CharSourceRange::getCharRange(Start, End));
  tooling::Replacements Replaces = reformat(getStyle(Style), Lex, Sources, Ranges);
  Rewriter Rewrite(Sources, LangOptions());
  tooling::applyAllReplacements(Replaces, Rewrite);
  if (Replaces.size() == 0)
      return false; // Nothing changed, don't touch the file.

  std::string ErrorInfo;
  llvm::raw_fd_ostream FileStream(FileName.c_str(), ErrorInfo,
                                  llvm::raw_fd_ostream::F_Binary);
  if (!ErrorInfo.empty()) {
      llvm::errs() << "Error while writing file: " << ErrorInfo << "\n";
      return true;
  }
  Rewrite.getEditBuffer(ID).write(FileStream);
  FileStream.flush();
  return false;
}

}  // namespace format
}  // namespace clang

int clang_format_main(std::string FileName, std::string Style) {
    return clang::format::format(FileName,Style) ? 0 : 1;
}
