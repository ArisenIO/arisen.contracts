add_contract(arisen.wrap arisen.wrap ${CMAKE_CURRENT_SOURCE_DIR}/src/arisen.wrap.cpp)

target_include_directories(arisen.wrap
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(arisen.wrap
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/arisen.wrap.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/arisen.wrap.contracts.md @ONLY )

target_compile_options( arisen.wrap PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )
