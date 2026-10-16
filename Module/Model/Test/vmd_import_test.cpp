#include "Model/Public/vmd_animation.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>

namespace {
using namespace Model;
int failures=0;
void Expect(bool value,const char* description){std::printf("  %-82s %s\n",description,value?"ok":"FAIL");failures+=!value;}
bool Near(double a,double b,double epsilon=1e-6){return std::abs(a-b)<epsilon;}
struct Bytes {
    std::vector<std::uint8_t> data;
    void U8(std::uint8_t value){data.push_back(value);}
    void U32(std::uint32_t value){for(int i=0;i<4;++i)U8(static_cast<std::uint8_t>(value>>(i*8)));}
    void F(float value){U32(std::bit_cast<std::uint32_t>(value));}
    void Name(std::string_view value,std::size_t length){for(std::size_t i=0;i<length;++i)U8(i<value.size()?static_cast<unsigned char>(value[i]):0);}
    void Header(bool old=false,std::size_t oldWidth=10){Name(old?"Vocaloid Motion Data file":"Vocaloid Motion Data 0002",30);Name("\x93\xaa",old?oldWidth:20);}
    void Controls(bool nonlinear=false,bool physics=false){std::array<std::uint8_t,16> row{};
        for(int axis=0;axis<4;++axis){row[axis]=20;row[axis+4]=20;row[axis+8]=107;row[axis+12]=107;}
        if(nonlinear){row[0]=32;row[4]=13;row[8]=32;row[12]=127;row[1]=0;row[5]=0;row[9]=127;row[13]=127;
            row[2]=0;row[6]=0;row[10]=127;row[14]=0;}
        auto original=row;if(physics){original[2]=31;original[3]=63;row=original;row[2]=99;row[3]=15;}
        for(auto value:row)U8(value);for(int shift=1;shift<=3;++shift){for(int i=shift;i<16;++i)U8(original[i]);for(int i=0;i<shift;++i)U8(0);}}
    void Bone(std::uint32_t frame,float x,float y,float z,bool nonlinear=false,bool physics=false){Name("\x93\xaa",15);U32(frame);F(x);F(y);F(z);
        F(0);F(frame?float(std::sqrt(0.5)):0);F(0);F(frame?float(std::sqrt(0.5)):1);Controls(nonlinear,physics);}
    void Morph(std::uint32_t frame,float weight){Name("\x82\xdc\x82\xce\x82\xbd\x82\xab",15);U32(frame);F(weight);}
    void Write(const std::filesystem::path& path)const{std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));}
};
std::filesystem::path FixtureRoot(){auto directory=std::filesystem::path(MODEL_SOURCE_DIR)/"build/vmd_import_fixtures";std::filesystem::create_directories(directory);return directory;}
std::optional<VmdMotion> Read(const Bytes& bytes,const char* name,std::string& error){auto path=FixtureRoot()/name;bytes.Write(path);return LoadVmdMotion(path,&error);}
void ParserTests(){
    std::puts("VMD binary import:");Bytes bytes;bytes.Header();bytes.U32(3);bytes.Bone(30,10,20,30,true);bytes.Bone(0,99,99,99);bytes.Bone(0,0,0,0);
    bytes.U32(2);bytes.Morph(0,0);bytes.Morph(30,1);bytes.U32(1);bytes.U32(30);for(int i=0;i<7;++i)bytes.F(float(i));
    for(int i=0;i<24;++i)bytes.U8(20);bytes.U32(45);bytes.U8(0);
    bytes.U32(1);bytes.U32(30);for(int i=0;i<6;++i)bytes.F(float(i));bytes.U32(1);bytes.U32(30);bytes.U8(2);bytes.F(0.05f);
    bytes.U32(2);bytes.U32(0);bytes.U8(1);bytes.U32(1);bytes.Name("\x8d\xb6\x91\xab\x82\x68\x82\x6a",20);bytes.U8(0);
    bytes.U32(30);bytes.U8(0);bytes.U32(1);bytes.Name("\x8d\xb6\x91\xab\x82\x68\x82\x6a",20);bytes.U8(1);
    std::string error;auto result=Read(bytes,"complete.vmd",error);
    Expect(bool(result),"Full VMD sections load with bounded record decoding");if(!result){std::puts(error.c_str());return;}
    Expect(result->modelName=="頭"&&result->bones[0].name=="頭"&&result->morphs[0].name=="まばたき","CP932 model, bone and morph names decode to UTF-8");
    Expect(result->bones.size()==1&&result->bones[0].keys.size()==2&&Near(result->bones[0].keys[0].translation.x,0),"Tracks sort by frame and duplicate keys keep the last authored value");
    const auto sample=SampleVmdBone(result->bones[0],0.5);const auto direction=sample.rotation*glm::dvec3(1,0,0);
    Expect(sample.translation.x>7.8&&sample.translation.x<8.2&&Near(sample.translation.y,10)&&Near(sample.translation.z,-3.75),"Arrival Bezier curves sample XYZ independently instead of linear interpolation");
    Expect(Near(direction.x,std::sqrt(0.5))&&Near(direction.z,std::sqrt(0.5)),"Left-handed MMD rotation reflects Z and interpolates by shortest slerp");
    Expect(Near(SampleVmdMorph(result->morphs[0],0.5),0.5)&&Near(SampleVmdMorph(result->morphs[0],3),1),"Morph curves interpolate linearly and clamp at the last key");
    Expect(Near(result->duration,1)&&result->cameraKeys==1&&result->lightKeys==1&&result->shadowKeys==1,"Scene key sections are validated and counted without inventing bone tracks");
    Expect(result->ikKeys.size()==2&&result->ikKeys[0].states[0].name=="左足ＩＫ"&&!result->ikKeys[0].states[0].enabled&&!result->ikKeys[1].visible,"IK enable states and model visibility are preserved");
    Expect(Near(SampleVmdBone(result->bones[0],-1).translation.x,0)&&Near(SampleVmdBone(result->bones[0],2).translation.x,10),"Bone sampling clamps before the first and after the final key");
    auto repeated=Read(bytes,"complete.vmd",error);Expect(repeated&&repeated->id==result->id,"Content IDs are stable across repeated imports");
    Bytes old;old.Header(true);old.U32(0);auto legacy=Read(old,"legacy.vmd",error);Expect(legacy&&legacy->modelName=="頭","Legacy VMD header uses the 10-byte model name field");
    Bytes old15;old15.Header(true,15);old15.U32(1);old15.Bone(0,0,0,0);auto legacy15=Read(old15,"legacy15.vmd",error);
    Expect(legacy15&&legacy15->bones.size()==1,"Legacy 15-byte name variant is accepted only when its records are valid");
    Bytes minimal;minimal.Header();minimal.U32(0);Expect(bool(Read(minimal,"minimal.vmd",error)),"An exact EOF between sections is accepted for legacy exporters");
    auto partial=minimal;partial.U8(0);Expect(!Read(partial,"partial_count.vmd",error),"A partial optional section count is rejected");
    auto huge=minimal;huge.data[50]=255;huge.data[51]=255;huge.data[52]=255;huge.data[53]=127;
    Expect(!Read(huge,"huge_count.vmd",error),"Impossible counts fail before allocation or record iteration");
    auto truncated=bytes;truncated.data.resize(truncated.data.size()-1);Expect(!Read(truncated,"truncated.vmd",error),"A truncated IK record is rejected");
    auto nan=bytes;const auto bits=std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN());for(int i=0;i<4;++i)nan.data[73+i]=static_cast<std::uint8_t>(bits>>(8*i));
    Expect(!Read(nan,"nan.vmd",error),"Nonfinite bone translation is rejected");
    auto zero=bytes;for(int i=0;i<16;++i)zero.data[85+i]=0;Expect(!Read(zero,"zero_quaternion.vmd",error),"A zero quaternion is rejected instead of producing NaNs");
    auto badCurve=bytes;badCurve.data[101]=128;Expect(!Read(badCurve,"bad_curve.vmd",error),"Out-of-range Bezier coordinates are rejected");
    auto badFrame=bytes;for(int i=0;i<4;++i)badFrame.data[69+i]=255;Expect(!Read(badFrame,"bad_frame.vmd",error),"Unbounded frame indices cannot create impractical bake durations");
    auto invalidName=bytes;invalidName.data[54]=0x81;invalidName.data[55]=0;Expect(!Read(invalidName,"bad_cp932.vmd",error),"Invalid CP932 sequences are diagnosed");
    Bytes physics;physics.Header();physics.U32(1);physics.Bone(0,0,0,0,false,true);auto flagged=Read(physics,"physics.vmd",error);
    Expect(flagged&&Near(flagged->bones[0].keys[0].interpolation[2].x1,31/127.0)&&Near(flagged->bones[0].keys[0].interpolation[3].x1,63/127.0),"Overwritten physics bytes recover Z and rotation controls from shifted copies");
    auto signTrack=result->bones[0];signTrack.keys[1].rotation=-signTrack.keys[0].rotation;
    Expect(Near(std::abs(glm::dot(SampleVmdBone(signTrack,0.5).rotation,signTrack.keys[0].rotation)),1),"Quaternion signs do not cause a full-turn interpolation artifact");
}
void MergeTests(){
    std::puts("VMD merge:");VmdMotion first;first.id="a";first.name="body";first.modelName="one";first.duration=1;
    VmdBoneTrack bone;bone.name="head";bone.keys.resize(2);bone.keys[1].frame=30;bone.keys[1].translation.x=1;first.bones={bone};first.ikKeys={{0,true,{{"left",false}}}};
    VmdMotion second;second.id="b";second.name="face";second.modelName="two";second.duration=1;
    bone.keys.erase(bone.keys.begin());bone.keys[0].translation.x=5;second.bones={bone};second.morphs={{"blink",{{0,0},{30,1}}}};second.ikKeys={{0,false,{{"right",true}}}};
    std::string error;auto merged=MergeVmdMotions({first,second},"combined",&error);Expect(bool(merged),"Body, face and IK sources merge into one motion");if(!merged)return;
    Expect(merged->name=="combined"&&merged->bones.size()==1&&merged->bones[0].keys.size()==2&&Near(merged->bones[0].keys[1].translation.x,5),"Later source wins for duplicate bone name/frame without erasing other frames");
    Expect(merged->morphs.size()==1&&merged->ikKeys[0].states.size()==2&&!merged->ikKeys[0].visible,"Disjoint IK states merge by name and the later visibility wins");
    auto reverse=MergeVmdMotions({second,first},"combined",&error);Expect(reverse&&Near(reverse->bones[0].keys[1].translation.x,1)&&reverse->id!=merged->id,"Source order deterministically controls duplicate-key resolution and IDs");
    Expect(!MergeVmdMotions({},"empty",&error),"Merging no source is a clear error");
    second.bones[0].keys[0].translation.x=std::numeric_limits<double>::infinity();Expect(!MergeVmdMotions({second},"bad",&error),"In-memory merge sources are validated as well as binary files");
}
std::size_t BoneKeys(const VmdMotion& motion){std::size_t result=0;for(const auto& track:motion.bones)result+=track.keys.size();return result;}
std::size_t MorphKeys(const VmdMotion& motion){std::size_t result=0;for(const auto& track:motion.morphs)result+=track.keys.size();return result;}
void RealTests(){
    const auto directory=std::filesystem::path(L"C:\\Users\\16620\\Downloads\\抬抬腿_by_哈基鹿鹿鹿_8d36aca8ba85b7e747b1be0bae26388d");
    if(!std::filesystem::exists(directory)){std::puts("Real VMD fixtures unavailable; synthetic tests still run.");return;}
    std::puts("Real raise-leg VMD fixtures:");std::string error;auto body=LoadVmdMotion(directory/L"taitui.vmd",&error);Expect(bool(body),"User body VMD imports");if(!body){std::puts(error.c_str());return;}
    auto face=LoadVmdMotion(directory/L"taitui表情.vmd",&error);Expect(bool(face),"User expression VMD imports");if(!face){std::puts(error.c_str());return;}
    auto camera=LoadVmdMotion(directory/L"taitui镜头.vmd",&error);Expect(bool(camera),"User camera VMD parses without becoming a character motion");if(!camera){std::puts(error.c_str());return;}
    Expect(body->bones.size()==57&&BoneKeys(*body)==15656&&body->morphs.empty(),"Body contains 57 bone tracks and 15,656 bone keys");
    Expect(!body->bones.empty()&&!body->bones[0].keys.empty()&&Near(body->bones[0].keys[0].interpolation[2].x1,20/127.0),"Real motion physics flags recover the original Z Bezier control instead of zero");
    Expect(face->morphs.size()==18&&MorphKeys(*face)==113&&face->bones.empty(),"Face contains 18 expression tracks and 113 expression keys");
    Expect(camera->cameraKeys==277&&camera->bones.empty()&&camera->morphs.empty(),"Camera contains 277 camera keys and no skeletal or expression tracks");
    Expect(Near(body->duration,9.2)&&Near(face->duration,9.2)&&Near(camera->duration,9.2),"All example files end at frame 276 (9.2 seconds)");
    auto combined=MergeVmdMotions({*body,*face},"抬抬腿",&error);
    Expect(combined&&combined->bones.size()==57&&combined->morphs.size()==18&&combined->ikKeys.empty(),"Merged motion retains the independent face tracks and has no invented IK controls");
    bool moved=false;for(const auto& track:body->bones){const auto a=SampleVmdBone(track,0),b=SampleVmdBone(track,4.6);
        moved|=glm::length(a.translation-b.translation)>1e-4||std::abs(glm::dot(a.rotation,b.rotation))<0.99999;}
    Expect(moved,"Real VMD midpoint sampling produces a changing skeletal pose");
    std::printf("  source model: %s; duration %.3f sec; bones %zu keys %zu; expressions %zu keys %zu; cameras %u\n",
        body->modelName.c_str(),body->duration,body->bones.size(),BoneKeys(*body),face->morphs.size(),MorphKeys(*face),camera->cameraKeys);
}
}
int main(){ParserTests();MergeTests();RealTests();std::printf("VMD import: %d failure(s)\n",failures);return failures?1:0;}
