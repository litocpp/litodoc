find_program(LITO_DOC_NPM_EXECUTABLE npm REQUIRED)

set(LITO_DOC_FRONTEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/frontend)
set(LITO_DOC_FRONTEND_INCLUDE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/generated/default-bundle.inc)
file(
  GLOB_RECURSE LITO_DOC_FRONTEND_SOURCES
  CONFIGURE_DEPENDS ${LITO_DOC_FRONTEND_DIR}/src/*.css
  ${LITO_DOC_FRONTEND_DIR}/src/*.html ${LITO_DOC_FRONTEND_DIR}/src/*.ts)
set(LITO_DOC_FRONTEND_INPUTS ${LITO_DOC_FRONTEND_DIR}/package.json
                              ${LITO_DOC_FRONTEND_DIR}/build.mjs
                              ${LITO_DOC_FRONTEND_SOURCES})

add_custom_command(
  OUTPUT ${LITO_DOC_FRONTEND_INCLUDE}
  COMMAND ${LITO_DOC_NPM_EXECUTABLE} install --no-package-lock
  COMMAND ${LITO_DOC_NPM_EXECUTABLE} run bundle
  WORKING_DIRECTORY ${LITO_DOC_FRONTEND_DIR}
  DEPENDS ${LITO_DOC_FRONTEND_INPUTS}
  COMMENT "Building embedded Lito doc frontend"
  VERBATIM)
add_custom_target(lito.doc.frontend DEPENDS ${LITO_DOC_FRONTEND_INCLUDE})

add_library(lito.doc STATIC)
add_dependencies(lito.doc lito.doc.frontend)
target_compile_features(lito.doc PUBLIC cxx_std_20)
target_sources(
  lito.doc
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/model.cppm
         src/database.cppm
         src/data/mod.cppm
         src/html.cppm
         src/markdown.cppm
         src/templates/mod.cppm
         src/frontend/mod.cppm
         src/publication.cppm
         src/render.cppm
         src/publish.cppm
         src/lib.cppm)
target_link_libraries(lito.doc PUBLIC lito.frontend rstd.std PRIVATE rstd.json)
