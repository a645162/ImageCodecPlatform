add_subdirectory(libs)

# 自测 CLI 默认不构建（它不是库的一部分，只用于本机验证硬件单元与容器）。
if(OIC_BUILD_CLI)
  add_subdirectory(cli)
endif()
