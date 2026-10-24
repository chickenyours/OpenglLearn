#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <json/json.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Model::EditorPreview {
// An optional host presentation study, not recovered native camera/animation.
// One image cannot uniquely determine focal length and distance. Keep the old
// 43-degree host FOV fixed in the initial eye/nose fit and expose both controls.
struct DianaCameraReference {
    std::string label="game-shot-study-v1";
    float verticalFovDegrees=43.f;
    float yawDegrees=12.3828373f,pitchDegrees=-11.6764209f;
    float distanceLocalMetres=.29280494f;
    glm::vec3 focusLocal{.000199801055f,.94091653825f,.073430747f};
    glm::vec3 pivotLocal{.000199801055f,.94091653825f,.073430747f};
    glm::vec3 modelEulerDegrees{0,0,-10.5325646f};
    // Actual off-axis projection principal point, in top-left normalized UV.
    // Moving it changes the camera frustum; no screenshot pixels are warped.
    glm::vec2 principalPoint{.599192126f,.406778325f};
    bool lockReferenceAspect=true;
    std::uint32_t referenceWidth=1210,referenceHeight=678;
};
inline void ValidateDianaCameraReference(const DianaCameraReference& value) {
    const auto finite=[](float x){return std::isfinite(x);};
    const auto finite3=[&](glm::vec3 v){return finite(v.x)&&finite(v.y)&&finite(v.z);};
    if(!finite(value.verticalFovDegrees)||value.verticalFovDegrees<15||value.verticalFovDegrees>90||
       !finite(value.yawDegrees)||std::abs(value.yawDegrees)>360||
       !finite(value.pitchDegrees)||std::abs(value.pitchDegrees)>75||
       !finite(value.distanceLocalMetres)||value.distanceLocalMetres<.12f||value.distanceLocalMetres>5||
       !finite3(value.focusLocal)||!finite3(value.pivotLocal)||!finite3(value.modelEulerDegrees)||
       glm::any(glm::greaterThan(glm::abs(value.focusLocal),glm::vec3(10)))||
       glm::any(glm::greaterThan(glm::abs(value.pivotLocal),glm::vec3(10)))||
       glm::any(glm::greaterThan(glm::abs(value.modelEulerDegrees),glm::vec3(90)))||
       !finite(value.principalPoint.x)||!finite(value.principalPoint.y)||
       glm::any(glm::lessThan(value.principalPoint,glm::vec2(.05f)))||
       glm::any(glm::greaterThan(value.principalPoint,glm::vec2(.95f)))||
       value.referenceWidth<128||value.referenceHeight<128||value.referenceWidth>8192||value.referenceHeight>8192)
        throw std::runtime_error("Invalid Diana reference camera profile");
}
inline glm::dmat4 DianaReferencePlacement(const glm::dmat4& base,const DianaCameraReference& value) {
    ValidateDianaCameraReference(value);
    auto rotation=glm::rotate(glm::dmat4(1),glm::radians(double(value.modelEulerDegrees.z)),glm::dvec3(0,0,1));
    rotation=glm::rotate(rotation,glm::radians(double(value.modelEulerDegrees.y)),glm::dvec3(0,1,0));
    rotation=glm::rotate(rotation,glm::radians(double(value.modelEulerDegrees.x)),glm::dvec3(1,0,0));
    return base*glm::translate(glm::dmat4(1),glm::dvec3(value.pivotLocal))*rotation*
        glm::translate(glm::dmat4(1),-glm::dvec3(value.pivotLocal));
}
inline glm::mat4 DianaReferenceProjection(glm::mat4 projection,const DianaCameraReference& value) {
    projection[2][0]-=2.f*value.principalPoint.x-1.f;
    projection[2][1]-=1.f-2.f*value.principalPoint.y;
    return projection;
}
inline glm::vec2 DianaReferenceViewport(glm::vec2 available,const DianaCameraReference& value) {
    if(!value.lockReferenceAspect)return available;
    const float aspect=float(value.referenceWidth)/float(value.referenceHeight);
    if(available.x/available.y>aspect)available.x=available.y*aspect;
    else available.y=available.x/aspect;
    return available;
}
inline Json::Value DianaReferenceVector(glm::dvec3 value) {
    Json::Value result(Json::arrayValue);for(int i=0;i<3;++i)result.append(value[i]);return result;
}
inline Json::Value DianaReferenceMatrix(const glm::dmat4& matrix) {
    Json::Value result(Json::arrayValue);
    for(int row=0;row<4;++row){Json::Value r(Json::arrayValue);for(int column=0;column<4;++column)r.append(matrix[column][row]);result.append(r);}
    return result;
}
inline Json::Value DianaReferenceProfileJson(const DianaCameraReference& value) {
    Json::Value root;root["schema"]="model.diana-camera-reference.v1";root["label"]=value.label;
    root["verticalFovDegrees"]=value.verticalFovDegrees;root["yawDegrees"]=value.yawDegrees;
    root["pitchDegrees"]=value.pitchDegrees;root["distanceLocalMetres"]=value.distanceLocalMetres;
    root["focusLocal"]=DianaReferenceVector(value.focusLocal);root["pivotLocal"]=DianaReferenceVector(value.pivotLocal);
    root["modelEulerDegrees"]=DianaReferenceVector(value.modelEulerDegrees);
    root["principalPoint"]=Json::Value(Json::arrayValue);root["principalPoint"].append(value.principalPoint.x);root["principalPoint"].append(value.principalPoint.y);
    root["lockReferenceAspect"]=value.lockReferenceAspect;root["referenceWidth"]=value.referenceWidth;root["referenceHeight"]=value.referenceHeight;
    return root;
}
inline DianaCameraReference LoadDianaCameraReference(const std::filesystem::path& path) {
    std::ifstream input(path);if(!input)throw std::runtime_error("Cannot open Diana reference camera profile");
    Json::Value root;input>>root;if(root.isMember("profile"))root=Json::Value(root["profile"]);
    if(root.get("schema","").asString()!="model.diana-camera-reference.v1")throw std::runtime_error("Unsupported Diana reference camera profile schema");
    DianaCameraReference result;
    result.label=root.get("label",result.label).asString();
    const auto scalar=[&](const char* key,float& value){if(root.isMember(key)){if(!root[key].isNumeric())throw std::runtime_error(std::string("Invalid camera scalar: ")+key);value=root[key].asFloat();}};
    const auto vector=[&](const char* key,glm::vec3& value){if(root.isMember(key)){const auto& v=root[key];if(!v.isArray()||v.size()!=3)throw std::runtime_error(std::string("Invalid camera vector: ")+key);for(int i=0;i<3;++i){if(!v[i].isNumeric())throw std::runtime_error("Non-numeric camera vector");value[i]=v[i].asFloat();}}};
    scalar("verticalFovDegrees",result.verticalFovDegrees);scalar("yawDegrees",result.yawDegrees);scalar("pitchDegrees",result.pitchDegrees);scalar("distanceLocalMetres",result.distanceLocalMetres);
    vector("focusLocal",result.focusLocal);vector("pivotLocal",result.pivotLocal);vector("modelEulerDegrees",result.modelEulerDegrees);
    if(root.isMember("principalPoint")){const auto& v=root["principalPoint"];if(!v.isArray()||v.size()!=2||!v[0].isNumeric()||!v[1].isNumeric())throw std::runtime_error("Invalid camera principal point");result.principalPoint={v[0].asFloat(),v[1].asFloat()};}
    if(root.isMember("lockReferenceAspect")){if(!root["lockReferenceAspect"].isBool())throw std::runtime_error("Invalid reference aspect switch");result.lockReferenceAspect=root["lockReferenceAspect"].asBool();}
    if(root.isMember("referenceWidth"))result.referenceWidth=root["referenceWidth"].asUInt();
    if(root.isMember("referenceHeight"))result.referenceHeight=root["referenceHeight"].asUInt();
    ValidateDianaCameraReference(result);return result;
}
struct DianaReferenceLandmark {
    const char* name;glm::dvec3 local;glm::dvec2 referencePixel;const char* provenance;bool fitted;
};
inline const std::array<DianaReferenceLandmark,4>& DianaReferenceLandmarks() {
    static const std::array<DianaReferenceLandmark,4> values{{
        {"eye_viewer_left",{-.02478827862,.9409222603,.073365964},{656,266},"EyeBall_R_Mat bounds XY center / front Z; approximate visible iris anchor",true},
        {"eye_viewer_right",{.02518788073,.9409108162,.073495530},{797,286},"EyeBall_L_Mat bounds XY center / front Z; approximate visible iris anchor",true},
        {"nose_tip",{.0024421296548098326,.9135200381278992,.0915146917104721},{704,350},"Face_mat vertex5685, selected against the old host preview",true},
        {"chin_check",{.00011214871483389288,.8698635697364807,.06794662028551102},{690,500},"Face_mat vertex2703; independent loose silhouette check, expression dependent",false},
    }};return values;
}
inline Json::Value DianaCameraReferenceMetadata(const DianaCameraReference& profile,bool enabled,
    const glm::dmat4& basePlacement,const glm::dmat4& placement,const glm::mat4& view,const glm::mat4& projection,
    glm::vec3 position,glm::vec3 focus,std::uint32_t width,std::uint32_t height) {
    Json::Value root;root["schema"]="model.diana-camera-report.v1";root["enabled"]=enabled;
    root["profile"]=DianaReferenceProfileJson(profile);root["matrix_layout"]="row arrays; column vectors, model then view then projection";
    root["basePlacement"]=DianaReferenceMatrix(basePlacement);root["placement"]=DianaReferenceMatrix(placement);
    root["view"]=DianaReferenceMatrix(glm::dmat4(view));root["projection"]=DianaReferenceMatrix(glm::dmat4(projection));
    const glm::dmat4 vp=glm::dmat4(projection)*glm::dmat4(view);
    root["viewProjection"]=DianaReferenceMatrix(vp);root["localToClip"]=DianaReferenceMatrix(vp*placement);
    root["cameraPosition"]=DianaReferenceVector(position);root["cameraFocus"]=DianaReferenceVector(focus);
    root["viewportWidth"]=width;root["viewportHeight"]=height;root["localUnit"]="metre";
    root["referenceImage"]="output/Diana/IBL/game-reference.png";
    root["landmarkPixelOrigin"]="top-left";root["landmarkReferenceDimensions"]=Json::Value(Json::arrayValue);
    root["landmarkReferenceDimensions"].append(1210);root["landmarkReferenceDimensions"].append(678);
    root["landmarks"]=Json::Value(Json::arrayValue);
    for(const auto& landmark:DianaReferenceLandmarks()) {
        Json::Value value;value["name"]=landmark.name;value["local"]=DianaReferenceVector(landmark.local);
        value["world"]=DianaReferenceVector(glm::dvec3(placement*glm::dvec4(landmark.local,1)));
        value["provenance"]=landmark.provenance;value["fittedAnchor"]=landmark.fitted;
        value["referencePixel"]=Json::Value(Json::arrayValue);value["referencePixel"].append(landmark.referencePixel.x);value["referencePixel"].append(landmark.referencePixel.y);
        const auto clip=vp*placement*glm::dvec4(landmark.local,1);
        const bool valid=std::isfinite(clip.w)&&clip.w>1e-12;value["projectable"]=valid;
        if(valid) {
            const glm::dvec2 uv(clip.x/clip.w*.5+.5,.5-clip.y/clip.w*.5);
            value["projectedUv"]=Json::Value(Json::arrayValue);value["projectedUv"].append(uv.x);value["projectedUv"].append(uv.y);
            value["projectedPixel"]=Json::Value(Json::arrayValue);value["projectedPixel"].append(uv.x*width);value["projectedPixel"].append(uv.y*height);
            const auto residual=uv*glm::dvec2(1210,678)-landmark.referencePixel;
            value["deltaAtReferenceDimensions"]=Json::Value(Json::arrayValue);value["deltaAtReferenceDimensions"].append(residual.x);value["deltaAtReferenceDimensions"].append(residual.y);
        }
        root["landmarks"].append(value);
    }
    root["limits"]=Json::Value(Json::arrayValue);
    for(const auto* note:{"Host presentation profile, not recovered native FOV or animation; focal length/distance remain ambiguous from one screenshot.",
        "Reference pixels were manually annotated with about3-pixel uncertainty. Eye surface centers are approximate gaze anchors.",
        "Current static GLB has four scene nodes, without the original driven facial rig; expression, eyelid/gaze changes and source groom state are not recovered.",
        "Rigid pose applies to the entire character, strands and local opaque capsules together; it does not articulate the native neck/face.",
        "Landmarks describe the source bind pose; do not use their residuals as a whole-image native-fidelity score.",
        "No lighting, exposure, hair color/width/density or image resampling is changed by this camera profile."})root["limits"].append(note);
    return root;
}
inline void SaveDianaCameraReferenceMetadata(const std::filesystem::path& path,const Json::Value& value) {
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);if(!output)throw std::runtime_error("Cannot create Diana camera metadata report");
    Json::StreamWriterBuilder builder;builder["indentation"]="  ";output<<Json::writeString(builder,value)<<'\n';
    if(!output)throw std::runtime_error("Cannot write Diana camera metadata report");
}
} // namespace Model::EditorPreview
