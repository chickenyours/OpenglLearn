#pragma once
#include "Model/Public/skeletal_asset.h"
#include <json/json.h>
#include <cmath>
#include <stdexcept>

namespace Model::SourcePhysicsJson {
inline void Need(bool b,const char* e){if(!b)throw std::runtime_error(e);}
inline double Number(const Json::Value& v){Need(v.isNumeric(),"Expected source physical number.");const double n=v.asDouble();Need(std::isfinite(n),"Non-finite source physical number.");return n;}
inline std::int32_t Index(const Json::Value& v){Need(v.isInt(),"Invalid source physical index.");return v.asInt();}
inline unsigned Unsigned(const Json::Value& v,unsigned max){Need(v.isUInt()&&v.asUInt()<=max,"Invalid source physical enum/mask.");return v.asUInt();}
inline std::string Text(const Json::Value& v){Need(v.isString()&&v.asString().size()<=65536,"Invalid source physical name.");return v.asString();}
inline Json::Value Vec(const glm::dvec3& v){Json::Value r(Json::arrayValue);for(int i=0;i<3;++i)r.append(v[i]);return r;}
inline glm::dvec3 Vec(const Json::Value& v){Need(v.isArray()&&v.size()==3,"Invalid source physical vector.");return {Number(v[0]),Number(v[1]),Number(v[2])};}
inline Json::Value Bodies(const std::vector<SourceRigidBody>& bodies){
    Json::Value r(Json::arrayValue);for(const auto& b:bodies){Json::Value v;v["name"]=b.name;v["englishName"]=b.englishName;v["bone"]=b.bone;
        v["collisionGroup"]=b.collisionGroup;v["collisionMask"]=b.collisionMask;v["shape"]=b.shape;v["mode"]=b.mode;
        v["dimensions"]=Vec(b.dimensions);v["position"]=Vec(b.position);v["rotationEuler"]=Vec(b.rotationEuler);
        v["mass"]=b.mass;v["linearDamping"]=b.linearDamping;v["angularDamping"]=b.angularDamping;v["restitution"]=b.restitution;v["friction"]=b.friction;r.append(v);}return r;
}
inline std::vector<SourceRigidBody> Bodies(const Json::Value& r){
    Need(r.isArray()&&r.size()<=100000,"Invalid source rigid body array.");std::vector<SourceRigidBody> out;out.reserve(r.size());
    for(const auto& v:r){Need(v.isObject(),"Invalid source rigid body record.");SourceRigidBody b;b.name=Text(v["name"]);b.englishName=Text(v["englishName"]);b.bone=Index(v["bone"]);
        b.collisionGroup=static_cast<std::uint8_t>(Unsigned(v["collisionGroup"],15));b.collisionMask=static_cast<std::uint16_t>(Unsigned(v["collisionMask"],65535));b.shape=static_cast<std::uint8_t>(Unsigned(v["shape"],2));b.mode=static_cast<std::uint8_t>(Unsigned(v["mode"],2));
        b.dimensions=Vec(v["dimensions"]);b.position=Vec(v["position"]);b.rotationEuler=Vec(v["rotationEuler"]);
        b.mass=Number(v["mass"]);b.linearDamping=Number(v["linearDamping"]);b.angularDamping=Number(v["angularDamping"]);b.restitution=Number(v["restitution"]);b.friction=Number(v["friction"]);out.push_back(b);}return out;
}
inline Json::Value Joints(const std::vector<SourcePhysicsJoint>& joints){
    Json::Value r(Json::arrayValue);for(const auto& j:joints){Json::Value v;v["name"]=j.name;v["englishName"]=j.englishName;v["type"]=j.type;v["rigidBodyA"]=j.rigidBodyA;v["rigidBodyB"]=j.rigidBodyB;
        v["position"]=Vec(j.position);v["rotationEuler"]=Vec(j.rotationEuler);v["translationMinimum"]=Vec(j.translationMinimum);v["translationMaximum"]=Vec(j.translationMaximum);
        v["rotationMinimum"]=Vec(j.rotationMinimum);v["rotationMaximum"]=Vec(j.rotationMaximum);v["translationSpring"]=Vec(j.translationSpring);v["rotationSpring"]=Vec(j.rotationSpring);r.append(v);}return r;
}
inline std::vector<SourcePhysicsJoint> Joints(const Json::Value& r){
    Need(r.isArray()&&r.size()<=100000,"Invalid source joint array.");std::vector<SourcePhysicsJoint> out;out.reserve(r.size());
    for(const auto& v:r){Need(v.isObject(),"Invalid source joint record.");SourcePhysicsJoint j;j.name=Text(v["name"]);j.englishName=Text(v["englishName"]);j.type=static_cast<std::uint8_t>(Unsigned(v["type"],5));j.rigidBodyA=Index(v["rigidBodyA"]);j.rigidBodyB=Index(v["rigidBodyB"]);
        j.position=Vec(v["position"]);j.rotationEuler=Vec(v["rotationEuler"]);j.translationMinimum=Vec(v["translationMinimum"]);j.translationMaximum=Vec(v["translationMaximum"]);
        j.rotationMinimum=Vec(v["rotationMinimum"]);j.rotationMaximum=Vec(v["rotationMaximum"]);j.translationSpring=Vec(v["translationSpring"]);j.rotationSpring=Vec(v["rotationSpring"]);out.push_back(j);}return out;
}
}
