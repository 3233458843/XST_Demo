add_library(letter_shell STATIC)

target_sources(letter_shell PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/port/shell_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell_cmd_list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell_companion.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell_ext.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell.c
)

target_include_directories(letter_shell PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/port
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(letter_shell PUBLIC
    stm32cubemx 
)

