set(LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  Support
  )
set(LLVM_USED_LIBS clangTooling clangBasic clangAST)

set(CMAKE_MODULE_PATH
  ${CMAKE_MODULE_PATH}
  "${CMAKE_CURRENT_SOURCE_DIR}/config"
  )

find_package(OpenCL REQUIRED)

add_clang_executable(acl
acl.cpp
Common.cpp
Stages.cpp
ClangFormat.cpp
ocl_utils.cpp
ocl_compiler.cpp
)
target_link_libraries(acl
clangAnalysis
clangAST
clangASTMatchers
clangBasic
clangDriver
clangFormat
clangFrontend
clangRewrite
clangRewriteFrontend
clangSema
clangTooling
clangToolingCore
${OPENCL_LIBRARIES}
)

# out of source pass
find_package(LLVM)

# Define add_llvm_* macro's.
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

add_subdirectory(Transforms/OCLInfo)

install(TARGETS acl
  RUNTIME DESTINATION bin)
