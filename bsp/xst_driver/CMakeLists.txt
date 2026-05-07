add_library(xst_driver STATIC)

# 仅包含核心驱动和基础移植文件
target_sources(xst_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/xst_drv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/porting/xst_porting.c
)

# BSP include directories (PUBLIC so main executable can access)
target_include_directories(xst_driver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/porting
)

# Dependencies for BSP
target_link_libraries(xst_driver PUBLIC
    stm32cubemx  # Depends on STM32CubeMX library 
    lwrb
)
