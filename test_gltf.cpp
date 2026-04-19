#include <CesiumGltf/Model.h>
#include <CesiumGltf/Node.h>
#include <CesiumGltf/Mesh.h>
#include <CesiumGltf/AccessorView.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <optional>

namespace Math {
    struct DVec3 { double x, y, z; };
}

// Provided implementation
void processGltfNode(const CesiumGltf::Model& model, int32_t nodeIndex, const glm::dmat4& currentTransform, std::vector<float>& vbo, std::vector<uint32_t>& ebo, const Math::DVec3& targetEcef, const double* enuMat, const std::optional<glm::dvec3>& rtcCenter) {
    if (nodeIndex < 0 || nodeIndex >= model.nodes.size()) return;
    const CesiumGltf::Node& node = model.nodes[nodeIndex];
    
    // FIX 1: Safely compute Node Matrix using GLM natively
    glm::dmat4 nodeMatrix(1.0);
    if (node.matrix.size() == 16) {
        nodeMatrix = glm::make_mat4(node.matrix.data());
    } else {
        glm::dmat4 t(1.0), r(1.0), s(1.0);
        if (node.translation.size() == 3) t = glm::translate(glm::dmat4(1.0), glm::dvec3(node.translation[0], node.translation[1], node.translation[2]));
        if (node.rotation.size() == 4) r = glm::mat4_cast(glm::dquat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2])); // w is index 3
        if (node.scale.size() == 3) s = glm::scale(glm::dmat4(1.0), glm::dvec3(node.scale[0], node.scale[1], node.scale[2]));
        nodeMatrix = t * r * s;
    }

    // FIX 2: Apply Y-Up to Z-Up transform for glTF compatibility
    glm::dmat4 yUpToZUp(
        1.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, -1.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    );

    glm::dmat4 tileTransform = currentTransform * yUpToZUp;

    if (node.mesh >= 0 && node.mesh < model.meshes.size()) {
        const CesiumGltf::Mesh& mesh = model.meshes[node.mesh];
        for (const auto& primitive : mesh.primitives) {
            auto posIt = primitive.attributes.find("POSITION");
            if (posIt == primitive.attributes.end()) continue;

            CesiumGltf::AccessorView<glm::vec3> posView(model, posIt->second);
            if (posView.status() != CesiumGltf::AccessorViewStatus::Valid) continue;

            CesiumGltf::AccessorView<glm::vec3> normView;
            auto normIt = primitive.attributes.find("NORMAL");
            if (normIt != primitive.attributes.end()) {
                normView = CesiumGltf::AccessorView<glm::vec3>(model, normIt->second);
            }

            uint32_t vertexOffset = vbo.size() / 6;

            for (int64_t i = 0; i < posView.size(); ++i) {
                // FIX 3: Apply RTC Center to Local Position FIRST
                glm::dvec4 localPos = nodeMatrix * glm::dvec4(posView[i].x, posView[i].y, posView[i].z, 1.0);
                if (rtcCenter.has_value()) {
                    localPos.x += rtcCenter.value().x;
                    localPos.y += rtcCenter.value().y;
                    localPos.z += rtcCenter.value().z;
                }

                // FIX 4: Transform to World (ECEF) then subtract Target
                glm::dvec4 worldPos = tileTransform * localPos;
                
                double dx = worldPos.x - targetEcef.x;
                double dy = worldPos.y - targetEcef.y;
                double dz = worldPos.z - targetEcef.z;

                float lx = (float)(enuMat[0]*dx + enuMat[1]*dy + enuMat[2]*dz);
                float ly = (float)(enuMat[4]*dx + enuMat[5]*dy + enuMat[6]*dz);
                float lz = (float)(enuMat[8]*dx + enuMat[9]*dy + enuMat[10]*dz);

                vbo.push_back(lx); vbo.push_back(ly); vbo.push_back(lz);

                if (normView.status() == CesiumGltf::AccessorViewStatus::Valid && i < normView.size()) {
                    glm::dvec4 localNorm = nodeMatrix * glm::dvec4(normView[i].x, normView[i].y, normView[i].z, 0.0);
                    glm::dvec4 wNorm = tileTransform * localNorm;
                    float nx = (float)(enuMat[0]*wNorm.x + enuMat[1]*wNorm.y + enuMat[2]*wNorm.z);
                    float ny = (float)(enuMat[4]*wNorm.x + enuMat[5]*wNorm.y + enuMat[6]*wNorm.z);
                    float nz = (float)(enuMat[8]*wNorm.x + enuMat[9]*wNorm.y + enuMat[10]*wNorm.z);
                    vbo.push_back(nx); vbo.push_back(ny); vbo.push_back(nz);
                } else {
                    vbo.push_back(0.0f); vbo.push_back(1.0f); vbo.push_back(0.0f);
                }
            }

            if (primitive.indices >= 0) {
                CesiumGltf::AccessorView<uint32_t> indView32(model, primitive.indices);
                if (indView32.status() == CesiumGltf::AccessorViewStatus::Valid) {
                    for (int64_t i = 0; i < indView32.size(); ++i) ebo.push_back(indView32[i] + vertexOffset);
                } else {
                    CesiumGltf::AccessorView<uint16_t> indView16(model, primitive.indices);
                    if (indView16.status() == CesiumGltf::AccessorViewStatus::Valid) {
                        for (int64_t i = 0; i < indView16.size(); ++i) ebo.push_back(indView16[i] + vertexOffset);
                    }
                }
            }
        }
    }

    for (int32_t childIndex : node.children) {
        processGltfNode(model, childIndex, currentTransform, vbo, ebo, targetEcef, enuMat, rtcCenter);
    }
}

int main() {
    return 0;
}
