find_program(TENON_DOC_NPM_EXECUTABLE npm REQUIRED)

set(TENON_DOC_FRONTEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/frontend)
set(TENON_DOC_FRONTEND_INCLUDE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/generated/default-bundle.inc)
set(TENON_DOC_FRONTEND_INPUTS
    ${TENON_DOC_FRONTEND_DIR}/package.json
    ${TENON_DOC_FRONTEND_DIR}/package-lock.json
    ${TENON_DOC_FRONTEND_DIR}/build.mjs
    ${TENON_DOC_FRONTEND_DIR}/src/app.ts
    ${TENON_DOC_FRONTEND_DIR}/src/theme-bootstrap.ts
    ${TENON_DOC_FRONTEND_DIR}/src/styles/style.css
    ${TENON_DOC_FRONTEND_DIR}/src/templates/root.html
    ${TENON_DOC_FRONTEND_DIR}/src/templates/package.html
    ${TENON_DOC_FRONTEND_DIR}/src/templates/module.html
    ${TENON_DOC_FRONTEND_DIR}/src/templates/symbol.html
    ${TENON_DOC_FRONTEND_DIR}/src/templates/source.html
    ${TENON_DOC_FRONTEND_DIR}/src/templates/partials/head.html
    ${TENON_DOC_FRONTEND_DIR}/src/templates/partials/foot.html)

add_custom_command(
  OUTPUT ${TENON_DOC_FRONTEND_INCLUDE}
  COMMAND ${TENON_DOC_NPM_EXECUTABLE} ci
  COMMAND ${TENON_DOC_NPM_EXECUTABLE} run bundle
  WORKING_DIRECTORY ${TENON_DOC_FRONTEND_DIR}
  DEPENDS ${TENON_DOC_FRONTEND_INPUTS}
  COMMENT "Building embedded Tenon doc frontend"
  VERBATIM)
add_custom_target(tenon.doc.frontend DEPENDS ${TENON_DOC_FRONTEND_INCLUDE})

add_library(tenon.doc STATIC)
add_dependencies(tenon.doc tenon.doc.frontend)
target_compile_features(tenon.doc PUBLIC cxx_std_20)
target_sources(
  tenon.doc
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
target_link_libraries(tenon.doc PUBLIC tenon.frontend rstd.std PRIVATE rstd.json)
