# 编译器
CXX = g++

# 编译选项
CXXFLAGS = -fdiagnostics-color=always -std=c++2a -g 

# 包含路径 (头文件路径)
INCLUDES = -I./include -I. -I./code

# 宏定义
DEFINES = -DDEBUG

# 库路径
LDFLAGS = -L./lib

# 链接库
LDLIBS = -lglfw3dll -lglad -lassimp.dll -ljsonloader

# 源文件和目标文件放置目录
BUILD_DIR = devel
BIN_DIR = bin

# 源文件和目标文件
SRC = code/shader.cpp \
	code/Material/material.cpp \
	code/Texture/texture.cpp \
	code/Model/mesh.cpp \
	code/Model/model_animation.cpp \
	code/Model/bone.cpp \
	code/Model/animation.cpp \
	code/Model/animator.cpp \
	code/RenderPipe/RenderPipe.cpp \
	code/RenderPipe/simpleRenderPipe.cpp \
	code/VisualTool/visual.cpp \
	code/Camera/camera.cpp \
	code/RenderPipe/Pass/pass.cpp \
	code/RenderPipe/Pass/CSMpass.cpp \
	code/RenderPipe/Pass/ScenePass.cpp \
	code/RenderPipe/Pass/ImageToBuffer.cpp \
	code/RenderPipe/Pass/CameraPass.cpp \
	code/ECS/System/SceneTree/scene_tree.cpp \
	code/DebugTool/DynamicVarChange/dynamic_change_vars.cpp \
	include/stb_image.cpp \
	code/Resource/Texture/texture.cpp \
	

# 定义 .o 和 .d 文件的位置
CPP_FILES = $(SRC) $(TARGET)
OBJ = $(addprefix $(BUILD_DIR)/, $(SRC:.cpp=.o))
DEP = $(addprefix $(BUILD_DIR)/, $(CPP_FILES:.cpp=.d))

# 主程序源文件
TARGET ?= main.cpp

# 检查 TARGET 是否存在（避免传入不存在的文件）
ifeq ($(wildcard $(TARGET)),)
$(error 主程序源文件 $(TARGET) 不存在！请指定有效的 TARGET)
endif

# 根据 TARGET 生成 .exe 文件名
EXE_NAME = $(BIN_DIR)/$(basename $(notdir $(TARGET))).exe

# 自动生成 .d 依赖文件的标志
DEPFLAGS = -MMD -MP

# 最终编译命令
all: $(EXE_NAME)

# 链接目标程序
$(EXE_NAME): $(OBJ) $(BUILD_DIR)/$(basename $(notdir $(TARGET))).o
	@echo Linking $(EXE_NAME)...
	$(CXX) $(OBJ) $(BUILD_DIR)/$(basename $(notdir $(TARGET))).o $(LDFLAGS) $(LDLIBS) -o $(EXE_NAME)

# 编译 .cpp 生成 .o 和 .d 文件，放在 devel 目录中
$(BUILD_DIR)/%.o: %.cpp
	$(shell if not exist $(subst /,\,$(dir $@)) cmd /c "mkdir $(subst /,\,$(dir $@))")
	$(shell if not exist $(subst /,\,$(dir $(patsubst %.o,%.d,$@))) cmd /c "mkdir $(subst /,\,$(dir $(patsubst %.o,%.d,$@)))")
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) $(DEPFLAGS) -c $< -o $@

# 自动包含 .d 依赖文件
-include $(DEP)

# 清理命令
clean:
	rm -f $(OBJ) $(DEP) $(TARGET)
