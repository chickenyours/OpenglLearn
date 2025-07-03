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

OLD = code/Model/mesh.cpp \
	code/Model/model_animation.cpp \
	code/Model/bone.cpp \
	code/Model/animation.cpp \
	code/Model/animator.cpp \
	code/RenderPipe/RenderPipe.cpp \
	code/RenderPipe/simpleRenderPipe.cpp \

# 源文件和目标文件
SRC = include/stb_image.cpp \
	code/Resource/Texture/texture.cpp \
	code/Resource/Shader/shader.cpp \
	code/Resource/Shader/shader_factory.cpp \
	code/Resource/Shader/shader_manager.cpp \
	code/Resource/Shader/shader_program.cpp \
	code/Resource/Shader/shader_program_factory.cpp \
	code/Resource/Model/mesh.cpp \
	code/Resource/Model/model.cpp \
	code/ToolAndAlgorithm/Hash/md5.cpp \
	code/Resource/Material/material.cpp \
	code/Application/application.cpp \
	code/Scene/scene.cpp \
	code/ECS/System/SceneTree/scene_tree.cpp \

	

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
