add_contract(arisen.msig arisen.msig ${CMAKE_CURRENT_SOURCE_DIR}/src/arisen.msig.cpp)

target_include_directories(arisen.msig
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(arisen.msig
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/arisen.msig.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/arisen.msig.contracts.md @ONLY )

target_compile_options( arisen.msig PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )
