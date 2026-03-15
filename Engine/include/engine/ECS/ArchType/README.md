# ArchType 三件套拆分说明

这份输出把原来的 3 个互相强依赖头文件，拆成了“声明头 + 实现源 + 模板实现头”的结构：

- `archtype_description.h`
- `archtype_description.cpp`
- `archtype_description_impl.h`
- `archtype_instance.h`
- `archtype_instance.cpp`
- `archtype_instance_impl.h`
- `archtype_manager.h`
- `archtype_manager.cpp`

## 目的

原始版本里有两个核心问题：

1. 三个头文件互相 `#include`，形成循环依赖。
2. 很多内联/模板函数直接访问别的类的私有成员布局，单靠前向声明不够。

所以这里采用：

- **非模板函数**：移到 `.cpp`
- **模板函数**：放到 `*_impl.h`
- **主声明头**：只保留类声明、成员声明、必要的 inline 接口

## 你工程里建议的放置方式

建议目录：

- `engine/ECS/ArchType/archtype_description.h`
- `engine/ECS/ArchType/archtype_description.cpp`
- `engine/ECS/ArchType/archtype_description_impl.h`
- `engine/ECS/ArchType/archtype_instance.h`
- `engine/ECS/ArchType/archtype_instance.cpp`
- `engine/ECS/ArchType/archtype_instance_impl.h`
- `engine/ECS/ArchType/archtype_manager.h`
- `engine/ECS/ArchType/archtype_manager.cpp`

## 包含关系

### 外部通常只需要 include

- 用 `ArchTypeDescription`：`#include "engine/ECS/ArchType/archtype_description.h"`
- 用 `ArchType`：`#include "engine/ECS/ArchType/archtype_instance.h"`
- 用 `ArchTypeManager`：`#include "engine/ECS/ArchType/archtype_manager.h"`

### 模板实现

模板实现头已经在对应主头末尾自动包含：

- `archtype_description.h` -> `archtype_description_impl.h`
- `archtype_instance.h` -> `archtype_instance_impl.h`

外部不需要手动 include `*_impl.h`。

## 需要你再核对的一点

你原始工程里有一些类型并不在这 3 个文件里定义，例如：

- `EntityID`
- `ReservationID`
- `ObjectPtr / ObjectWeakPtr`
- `FixedChunkArray`

我沿用了你原有 include 习惯，没有替你改这些来源头文件。如果这些类型在你项目里的真实声明位置与当前 include 不一致，你需要按你工程实际路径微调 include。

## 额外提醒

这次拆分重点是**解循环依赖和未完整类型问题**，没有主动重构你的业务语义。

不过你这套代码里还有一个值得后续继续处理的点：

- `friend` 范围比较大，三个类之间对私有布局耦合仍然偏重。

后面如果你愿意，我可以继续帮你做第二步：

- 把跨类私有访问收缩成少量内部 helper / accessor
- 再进一步降低头文件耦合
- 顺便把异常安全和回滚路径补强
