add_library(tenon.doc STATIC)
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
         src/json.cppm
         src/html.cppm
         src/markdown.cppm
         src/render.cppm
         src/publish.cppm
         src/lib.cppm)
target_link_libraries(tenon.doc PUBLIC tenon.frontend rstd.std PRIVATE rstd.json)
