set(LLVM_LINK_COMPONENTS support)
set(LLVM_USED_LIBS clangTooling clangBasic clangAST)

set(CMAKE_MODULE_PATH
  ${CMAKE_MODULE_PATH}
  "${CMAKE_CURRENT_SOURCE_DIR}/config"
  )

find_package(OpenCL REQUIRED)

add_clang_executable(accll
accll.cpp
Common.cpp
Stages.cpp
ClangFormat.cpp
ocl_utils.cpp
ocl_compiler.cpp
)
target_link_libraries(accll
clangTooling
clangBasic
clangASTMatchers
clangFormat
clangRewriteFrontend
${OPENCL_LIBRARIES}
)
