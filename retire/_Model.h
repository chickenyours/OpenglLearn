
//格式说明:
//    MESH_VAO:
//       顶点坐标(3):

#pragma once
#include <string>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


#define MAX_BONE_INFLUENCE 4


using std::string;
using std::vector;

namespace MyTool{

class Texture{
public:
    unsigned int id;
    string typeName;
    //文件路径，区别任何texutre
    string path;
    static vector<Texture> textures;
};

struct Vertex{
      // position
      glm::vec3 Position;
      // normal
      glm::vec3 Normal;
      // texCoords
      glm::vec2 TexCoords;
      
      // tangent
      glm::vec3 Tangent;
      // bitangent
      glm::vec3 Bitangent;
  
      //bone indexes which will influence this vertex
      int m_BoneIDs[MAX_BONE_INFLUENCE];
      //weights from each bone
      float m_Weights[MAX_BONE_INFLUENCE];
};

struct BoneInfo
{
    /*id is index in finalBoneMatrices*/
    int id;

    /*offset matrix transforms vertex from model space to bone space*/
    glm::mat4 offset;

};

struct Material {
    // 基础材质属性 (Phong 模型相关)
    glm::vec3 ambient;       // 环境光颜色，反射环境光的颜色
    glm::vec3 diffuse;       // 漫反射颜色，决定材质的基本表面颜色
    glm::vec3 specular;      // 镜面反射颜色，决定高光颜色
    float shininess;         // 高光强度，控制高光的锐利程度

    // 扩展属性 (传统材质参数，如 Wavefront OBJ 的 mtl 文件)
    float Kd;                // 漫反射强度，用于调整 diffuse 光的比例
    float Ks;                // 镜面反射强度，用于调整 specular 光的比例
    float Ka;                // 环境光强度，用于调整 ambient 光的比例
    float Ns;                // 镜面反射指数，控制高光的集中度，与 shininess 类似
    float Ni;                // 折射率，用于透明材质（如玻璃）
    float d;                 // 不透明度，d = 1.0 表示完全不透明，d < 1.0 表示部分透明
    float illum;             // 光照模型，OBJ 材质文件中的枚举值（如漫反射、镜面反射等）

    // PBR (Physically Based Rendering) 相关属性
    float metallic;          // 金属度，表示材质是否为金属（0 = 非金属，1 = 金属）
    float roughness;         // 粗糙度，控制材质的表面光滑程度
    float ao;                // 环境光遮蔽 (Ambient Occlusion)，控制暗部阴影的强度

    
    // 纹理支持
    bool hasBaseColorMap;    // 是否使用基础颜色贴图（代替固定的 diffuse 颜色）
    bool hasMetalnessMap;    // 是否使用金属度贴图（动态控制 metallic 属性）
    bool hasRoughnessMap;    // 是否使用粗糙度贴图（动态控制 roughness 属性）
    bool hasNormalMap;       // 是否使用法线贴图，用于增强表面细节的凹凸感
    bool hasAOMap;           // 是否使用环境光遮蔽贴图，用于提高暗部细节表现

    // 高级特性
    float transparency;      // 透明度，控制材质的透明效果（与 d 类似）
    float ior;               // 折射率 (Index of Refraction)，用于计算透明材质的折射效果
    float clearCoat;         // 透明涂层强度，模拟车漆等表面的额外涂层
    float clearCoatRoughness;// 透明涂层的粗糙度
    float subsurfaceScattering; // 次表面散射强度，用于模拟半透明材质（如皮肤、蜡）
    glm::vec3 subsurfaceColor;  // 次表面散射的颜色，用于控制散射光的颜色

    // 各向异性 (用于金属拉丝等材质)
    float anisotropy;        // 各向异性强度，用于模拟拉丝金属或织物材质
    glm::vec2 anisotropyDirection; // 各向异性方向，用于控制拉丝方向

    // 自发光
    glm::vec3 emissive;      // 自发光颜色，用于模拟材质自身发光
    float emissiveIntensity; // 自发光强度

    // 动态与特效支持
    float parallaxDepth;     // 视差贴图深度，用于模拟材质表面的深度感
    bool hasParallaxMap;     // 是否使用视差贴图
    float thinFilmThickness; // 薄膜厚度，用于模拟薄膜干涉（如肥皂泡或油膜）

    // 环境相关属性
    bool hasReflectionMap;   // 是否使用环境反射贴图
    bool hasRefractionMap;   // 是否使用环境折射贴图

    // 优化与实例化支持
    float lodBias;           // 纹理 LOD 偏移，用于调整不同距离下的纹理细节
    int materialID;          // 材质 ID，用于批处理优化
};



// x,y,z,coordX coordX,NormalX,NormalY,NormalZ
class Mesh{
    public:
        Mesh(vector<float> vertexs,vector<unsigned int> indices,vector<Texture> textures);
        //自绘制
        void Draw();
        unsigned int VAO,EBO,VBO;
        unsigned int vertexsCount;    
        unsigned int indicesCount;
        Material material;
        vector<Texture> textures;
};

class Model{
    public:
        vector<Mesh> _meshArray;
        string directory;
        Model(string path);
        void LoadModel(string path);
        void Draw();
        void ProcessNode(aiNode *node,const aiScene *scene);
        Mesh ProcessMesh(aiMesh *mesh,const aiScene *scene);
        vector<Texture> LoadMaterialTexture(aiMaterial *mat,aiTextureType type,string typeName);
        unsigned int TextureFromFile(const char* fileName);
};
}

