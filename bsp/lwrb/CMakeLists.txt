add_library( lwrb STATIC)

target_sources( lwrb PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lwrb.c 
)   

target_include_directories(lwrb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

