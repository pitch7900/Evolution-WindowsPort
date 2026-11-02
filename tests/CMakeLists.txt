# CMakeLists.txt pour les tests unitaires et de non-regression d'Evolution-WindowsPort

cmake_minimum_required(VERSION 3.20)
project(EvolutionTests LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
enable_testing()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Test 1 : Helpers WinUI
add_executable(test_helpers unit/test_helpers.cpp)
add_test(NAME TestHelpers COMMAND test_helpers)

# Test 2 : Contrats evo-core
add_executable(test_evo_core_contracts unit/test_evo_core_contracts.c)
add_test(NAME TestEvoCoreContracts COMMAND test_evo_core_contracts)

# Test 3 : Preferences Settings
add_executable(test_settings unit/test_settings.cpp)
add_test(NAME TestSettings COMMAND test_settings)

# Options de compilation communes pour MSVC
if(MSVC)
    target_compile_options(test_helpers PRIVATE /utf-8 /W4)
    target_compile_options(test_evo_core_contracts PRIVATE /utf-8 /W4)
    target_compile_options(test_settings PRIVATE /utf-8 /W4)
endif()

