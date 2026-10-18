#include <assimp/material.h>

#include <array>
#include <cmath>
#include <iostream>
#include <type_traits>

namespace {
int failures = 0;

void Expect(bool condition, const char* description) {
    std::cout << description << (condition ? " : ok\n" : " : FAIL\n");
    failures += !condition;
}

bool Near(ai_real actual, ai_real expected) {
    constexpr ai_real tolerance = sizeof(ai_real) == sizeof(double) ? ai_real(1e-12) : ai_real(1e-6);
    return std::abs(actual - expected) <= tolerance;
}

template <class T>
auto Component(const T& value) {
    if constexpr (requires { value.mRotation; }) return value.mRotation;
    else if constexpr (requires { value.r; }) return value.r;
    else return value.x;
}

template <class T, std::size_t N>
void CheckTypedProperty(const T& value, const std::array<ai_real, N>& expected, bool explicitTemplate) {
    aiMaterial material;
    constexpr const char* key = "$test.material_precision";
    const auto added = explicitTemplate ? material.AddProperty<T>(&value, 1, key) : material.AddProperty(&value, 1, key);
    Expect(added == AI_SUCCESS, "typed material property can be written");
    const aiMaterialProperty* property = nullptr;
    Expect(aiGetMaterialProperty(&material, key, 0, 0, &property) == AI_SUCCESS && property,
            "typed material property remains accessible");
    if (!property) return;
    // Assimp colors intentionally retain float components even when ai_real
    // (UV transforms and vectors) is double. Assert each value's real layout.
    using Scalar = decltype(Component(value));
    static_assert(std::is_same_v<Scalar, float> || std::is_same_v<Scalar, double>);
    const auto expectedType = std::is_same_v<Scalar, double> ? aiPTI_Double : aiPTI_Float;
    Expect(property->mType == expectedType && property->mDataLength == sizeof(T),
            "ordinary overload and explicit template tag the actual component encoding");
    std::array<ai_real, N> actual{};
    unsigned int count = static_cast<unsigned int>(N);
    bool exact = aiGetMaterialFloatArray(&material, key, 0, 0, actual.data(), &count) == AI_SUCCESS && count == N;
    for (std::size_t i = 0; i < N; ++i) exact = exact && Near(actual[i], expected[i]);
    Expect(exact, "every material component survives the numeric material getter");
}

template <class Encoded>
void CheckEncodedUV(aiPropertyTypeInfo type) {
    aiMaterial material;
    const std::array<Encoded, 5> encoded{Encoded(0.125), Encoded(-0.25), Encoded(1.5), Encoded(0.75), Encoded(0.3125)};
    Expect(material.AddBinaryProperty(encoded.data(), static_cast<unsigned int>(sizeof(encoded)),
                   AI_MATKEY_UVTRANSFORM_DIFFUSE(0), type) == AI_SUCCESS,
            "UV property accepts its explicit float or double encoding");
    aiUVTransform actual;
    const bool read = material.Get(AI_MATKEY_UVTRANSFORM_DIFFUSE(0), actual) == AI_SUCCESS;
    Expect(read && Near(actual.mTranslation.x, ai_real(encoded[0])) && Near(actual.mTranslation.y, ai_real(encoded[1])) &&
                    Near(actual.mScaling.x, ai_real(encoded[2])) && Near(actual.mScaling.y, ai_real(encoded[3])) &&
                    Near(actual.mRotation, ai_real(encoded[4])),
            "UV getter converts all five float/double components into ai_real");
}
} // namespace

int main() {
    aiUVTransform uv;
    uv.mTranslation = aiVector2D(ai_real(0.125), ai_real(-0.25));
    uv.mScaling = aiVector2D(ai_real(1.5), ai_real(0.75));
    uv.mRotation = ai_real(0.3125);
    const aiColor4D rgba(ai_real(0.125), ai_real(0.25), ai_real(0.5), ai_real(0.75));
    const aiColor3D rgb(ai_real(0.125), ai_real(0.25), ai_real(0.5));
    // This double value catches a property wrongly decoded from float-sized halves.
    const aiVector3D vector(ai_real(1.000000000123), ai_real(-2.25), ai_real(3.5));
    for (bool legacy : {false, true}) {
        CheckTypedProperty(uv, std::array<ai_real, 5>{uv.mTranslation.x, uv.mTranslation.y,
                                          uv.mScaling.x, uv.mScaling.y, uv.mRotation}, legacy);
        CheckTypedProperty(rgba, std::array<ai_real, 4>{rgba.r, rgba.g, rgba.b, rgba.a}, legacy);
        CheckTypedProperty(rgb, std::array<ai_real, 3>{rgb.r, rgb.g, rgb.b}, legacy);
        CheckTypedProperty(vector, std::array<ai_real, 3>{vector.x, vector.y, vector.z}, legacy);
    }
    CheckEncodedUV<float>(aiPTI_Float);
    CheckEncodedUV<double>(aiPTI_Double);
    std::cout << "ai_real bytes: " << sizeof(ai_real) << "; failures: " << failures << '\n';
    return failures ? 1 : 0;
}
