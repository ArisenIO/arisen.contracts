add_contract(arisen.token arisen.token ${CMAKE_CURRENT_SOURCE_DIR}/src/arisen.token.cpp)

target_include_directories(arisen.token
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(arisen.token
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/arisen.token.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/arisen.token.contracts.md @ONLY )

target_compile_options( arisen.token PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )
