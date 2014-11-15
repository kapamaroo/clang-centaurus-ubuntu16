set(LLVM_LINK_COMPONENTS support)
set(LLVM_USED_LIBS clangTooling clangBasic clangAST)

add_clang_executable(accll
accll.cpp
Common.cpp
Stages.cpp
ClangFormat.cpp
)
target_link_libraries(accll
clangTooling
clangBasic
clangASTMatchers
clangFormat
clangRewriteFrontend
)
