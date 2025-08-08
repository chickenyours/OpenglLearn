// 这是测试场景树 SceneTreeSystem 的代码

#include <iostream>

#include "code/DebugTool/RunTimeInspector/fatal_error_assert_log.h"
#include "code/ECS/Entity/entity.h"
#include "code/ECS/Component/component_register.h"
#include "code/ECS/System/SceneTree/scene_tree.h"
#include "code/ECS/Component/Hierarchy/hierarchy.h"

ECS::Core::ComponentRegister reg;
ECS::System::SceneTreeSystem st;

void test1(){
    ECS::Entity a(1);
    ECS::Entity b(2);
    ECS::Entity c(3);
    ECS::Entity d(4);
    ECS::Entity e(5);
    // --- 建立初始父子关系 ---
    st.SetParent(a.GetID(), b.GetID()); // b -> a
    st.SetParent(c.GetID(), a.GetID()); // a -> c
    st.SetParent(d.GetID(), a.GetID()); // a -> d
    st.SetParent(e.GetID(), b.GetID()); // b -> e

    std::cout << "\n===== 初始Scene Tree =====\n";
    st.Print();
    /*
    Root
    └───2
        ├───1
        │   ├───3
        │   └───4
        └───5
    */

    // --- 测试: 移动节点 ---
    // 把 d (4) 移动到 c (3) 下
    st.SetParent(d.GetID(), c.GetID());

    std::cout << "\n===== 移动4到3下方后的Scene Tree =====\n";
    st.Print();
    /*
    Root
    └───2
        ├───1
        │   └───3
        │       └───4
        └───5
    */

    // --- 测试: 移除节点 ---
    // 移除节点 a (1)，应该使c(3)保留，但a(1)不见
    st.RemoveEntity(a.GetID());

    std::cout << "\n===== 移除1后的Scene Tree =====\n";
    st.Print();
    /*
    Root
    ├───2
    │   └───5
    └───3
        └───4
    */
}

void TestShaderProgram1(){
    ECS::Entity a(1), b(2), c(3), d(4), e(5), f(6), g(7);

    st.SetParent(a.GetID(), b.GetID());
    st.SetParent(c.GetID(), a.GetID());
    st.SetParent(d.GetID(), a.GetID());
    st.SetParent(e.GetID(), b.GetID());
    st.SetParent(f.GetID(), c.GetID());
    st.SetParent(g.GetID(), d.GetID());

    // Perform complex operations
    st.SetParent(f.GetID(), b.GetID()); // Move f under b
    st.RemoveEntity(a.GetID());        // Remove a
    st.SetParent(g.GetID(), e.GetID()); // Move g under e

    std::string expectedTree = 
        "Root\n"
        "├───2\n"
        "│   ├───5\n"
        "│   │   └───7\n"
        "│   └───6\n"
        "├───3\n"
        "└───4\n";

    st.Print();
}

// 综合测试
void test3() {
    using ECS::Entity;
    using ID = ECS::EntityID;

    ECS::System::SceneTreeSystem st;
    ECS::Core::ComponentRegister reg;
    st.SetComponentRegister(&reg);

    // Create entities 1~20
    std::vector<Entity> nodes;
    for (int i = 1; i <= 20; ++i) {
        nodes.emplace_back(i);
    }

    // 初始构建树结构：1为根，2~5为其子节点，6~10为2的孙子，11~15为3的孙子，16~20为4的孙子
    st.SetParent(nodes[1].GetID(), nodes[0].GetID());  // 2 -> 1
    st.SetParent(nodes[2].GetID(), nodes[0].GetID());  // 3 -> 1
    st.SetParent(nodes[3].GetID(), nodes[0].GetID());  // 4 -> 1
    st.SetParent(nodes[4].GetID(), nodes[0].GetID());  // 5 -> 1

    for (int i = 5; i < 10; ++i) {  // 6~10 -> 2
        st.SetParent(nodes[i].GetID(), nodes[1].GetID());
    }

    for (int i = 10; i < 15; ++i) {  // 11~15 -> 3
        st.SetParent(nodes[i].GetID(), nodes[2].GetID());
    }

    for (int i = 15; i < 20; ++i) {  // 16~20 -> 4
        st.SetParent(nodes[i].GetID(), nodes[3].GetID());
    }

    std::cout << "\n===== 初始状态 =====\n";
    st.Print();

    // 做一些动态调整
    std::cout << "\n===== 将6移动到5下方 =====\n";
    st.SetParent(nodes[5].GetID(), nodes[4].GetID());   // 6 -> 5
    st.Print();

    std::cout << "\n===== 移除节点2，其子节点成为根节点 =====\n";
    st.RemoveEntity(nodes[1].GetID());                  // Remove 2, its children become roots
    st.Print();

    std::cout << "\n===== 将11移动到6下方 =====\n";
    st.SetParent(nodes[10].GetID(), nodes[5].GetID());  // 11 -> 6
    st.Print();

    std::cout << "\n===== 将12移动到6下方 =====\n";
    st.SetParent(nodes[11].GetID(), nodes[5].GetID());  // 12 -> 6
    st.Print();

    std::cout << "\n===== 将16移动到5下方 =====\n";
    st.SetParent(nodes[15].GetID(), nodes[4].GetID());  // 16 -> 5
    st.Print();

    std::cout << "\n===== 移除节点1 =====\n";
    st.RemoveEntity(nodes[0].GetID());                  // Remove 1
    st.Print();

    std::cout << "\n===== 将6解除父子关系 =====\n";
    st.RemoveFromOldParent(nodes[5].GetID());
    st.Print();

    std::cout << "\n===== 将9放到16 =====\n";
    st.SetParent(nodes[8].GetID(),nodes[15].GetID());
    st.Print();

    std::cout << "\n===== 将16解除父子关系 =====\n";
    st.RemoveFromOldParent(nodes[15].GetID());
    st.Print();
    // 👀 你可以根据 `SceneTreeSystem` 的打印格式手动验证输出
}





int main() {
    st.SetComponentRegister(&reg);
    test3();
    return 0;
}
