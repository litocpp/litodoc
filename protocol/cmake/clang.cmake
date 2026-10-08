find_package(Clang REQUIRED CONFIG)

if(NOT TARGET clang-cpp)
  message(FATAL_ERROR "Litodoc requires the clang-cpp target")
endif()

add_library(Litodoc::Clang INTERFACE IMPORTED)
target_include_directories(Litodoc::Clang SYSTEM INTERFACE
  ${CLANG_INCLUDE_DIRS}
  ${LLVM_INCLUDE_DIRS}
)
target_link_libraries(Litodoc::Clang INTERFACE clang-cpp)
if(TARGET LLVM)
  target_link_libraries(Litodoc::Clang INTERFACE LLVM)
endif()
