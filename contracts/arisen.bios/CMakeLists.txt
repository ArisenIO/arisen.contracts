add_contract(arisen.bios arisen.bios ${CMAKE_CURRENT_SOURCE_DIR}/src/arisen.bios.cpp)

target_include_directories(arisen.bios
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(arisen.bios
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/arisen.bios.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/arisen.bios.contracts.md @ONLY )

target_compile_options( arisen.bios PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )
