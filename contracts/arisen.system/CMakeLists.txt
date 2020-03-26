add_contract(arisen.system arisen.system
   ${CMAKE_CURRENT_SOURCE_DIR}/src/arisen.system.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/delegate_bandwidth.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/exchange_state.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/native.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/producer_pay.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/com.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/voting.cpp
)

target_include_directories(arisen.system
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../arisen.token/include)

set_target_properties(arisen.system
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_contract(com.results com.results ${CMAKE_CURRENT_SOURCE_DIR}/src/com.results.cpp)

target_include_directories(com.results
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(com.results
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/.com")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/arisen.system.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/arisen.system.contracts.md @ONLY )

target_compile_options( arisen.system PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )
