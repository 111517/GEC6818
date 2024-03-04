set(CMAKE_SYSTEM_NAME Linux)
# 指定目标平台
set(CMAKE_SYSTEM_PROCESSOR arm)
 
  # 指定交叉编译工具链的根路径
   set(CROSS_CHAIN_PATH /usr/local/arm/5.4.0/usr)
    # 指定C编译器
     set(CMAKE_C_COMPILER "${CROSS_CHAIN_PATH}/bin/arm-none-linux-gnueabi-gcc")
      # 指定C++编译器
       set(CMAKE_CXX_COMPILER "${CROSS_CHAIN_PATH}/bin/arm-none-linux-gnueabi-g++")

