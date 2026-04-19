#ifndef CESIUM_MINIMAL_TEST_H
#define CESIUM_MINIMAL_TEST_H

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <cmath>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <nlohmann/json.hpp>
#include "cgltf.h"

#include "AliceViewer.h"
#include "AliceMath.h"
#include "AliceNetwork.h"
#include "NormalShader.h"
#include "stb_image_write.h"

namespace CesiumMinimal {

struct HttpResponse { int statusCode; std::vector<uint8_t> data; std::string error; };

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResponse get(const std::string& url, const std::string& token = "") = 0;
};

class AliceCurlClient : public IHttpClient {
public:
    HttpResponse get(const std::string& url, const std::string& token = "") override {
        HttpResponse res; long sc = 0;
        bool ok = Alice::Network::Fetch(url.c_str(), res.data, &sc, nullptr, nullptr, 10, token.empty() ? nullptr : token.c_str());
        res.statusCode = (int)sc; if (!ok) res.error = "CURL Fetch Failed";
        return res;
    }
};

struct IonAssetEndpoint { std::string url; std::string accessToken; };

class IonDiscovery {
public:
    static std::optional<IonAssetEndpoint> discover(IHttpClient& client, int64_t assetId, const std::string& ionToken) {
        std::string url = "https://api.cesium.com/v1/assets/" + std::to_string(assetId) + "/endpoint?access_token=" + ionToken;
        HttpResponse response = client.get(url);
        if (response.statusCode != 200) return std::nullopt;
        try {
            auto j = nlohmann::json::parse(response.data.begin(), response.data.end());
            IonAssetEndpoint endpoint; endpoint.url = j["url"]; endpoint.accessToken = j["accessToken"];
            return endpoint;
        } catch (...) { return std::nullopt; }
    }
};

enum class LoadState { None, Done };

struct ViewState {
    Math::DVec3 positionECEF;
    Math::DVec3 directionECEF;
    double viewportHeight;
    double fovY;
};

struct BoundingVolume {
    Math::DVec3 centerECEF;
    double radius;
    bool valid = false;
};

struct RenderResources {
    GLuint vao = 0;
    std::vector<GLuint> vbos;
    std::vector<GLuint> ebos;
    struct DrawCmd { GLuint vao, ebo; int count; };
    std::vector<DrawCmd> commands;
    
    ~RenderResources() {
        if (vao) glDeleteVertexArrays(1, &vao);
        for (auto vbo : vbos) glDeleteBuffers(1, &vbo);
        for (auto ebo : ebos) glDeleteBuffers(1, &ebo);
    }
};

class Tile {
public:
    BoundingVolume bounds;
    std::string contentUri;
    std::vector<std::unique_ptr<Tile>> children;
    std::vector<uint8_t> payload;
    bool isLoading = false;
    bool isLoaded = false;
    void* _rendererResources = nullptr;
    glm::dmat4 transform = glm::dmat4(1.0);

    LoadState getState() const { return isLoaded ? LoadState::Done : LoadState::None; }
    void* getRendererResources() const { return _rendererResources; }
    void setRendererResources(void* p) { _rendererResources = p; }
};

class Tileset {
public:
    std::unique_ptr<Tile> root;
    std::string baseUrl;
    std::string token;
    IHttpClient* client;
    std::vector<Tile*> renderList;

    void updateView(const ViewState& view) {
        renderList.clear();
        bool fetchedThisFrame = false;
        if (root) traverseAndLoad(root.get(), view, fetchedThisFrame);
    }

private:
void traverseAndLoad(Tile* node, const ViewState& view, bool& fetchedThisFrame) {
        if (node->bounds.valid) {
            double dx = node->bounds.centerECEF.x - view.positionECEF.x;
            double dy = node->bounds.centerECEF.y - view.positionECEF.y;
            double dz = node->bounds.centerECEF.z - view.positionECEF.z;
            double dist = sqrt(dx*dx + dy*dy + dz*dz);
            // LOD Enhancement: Further reduce cull distance to force deepest traversal
            if (dist > node->bounds.radius + 2000.0) return; 
        }
        if (!node->contentUri.empty() && !node->isLoaded && !node->isLoading && !fetchedThisFrame) {
            node->isLoading = true;
            fetchedThisFrame = true;
            std::string uri = node->contentUri;
            if (uri.find(".json") != std::string::npos) {
                auto extRes = client->get(baseUrl + uri, token);
                if (extRes.statusCode == 200) {
                    try {
                        auto extJ = nlohmann::json::parse(extRes.data.begin(), extRes.data.end());
                        std::string newBase = baseUrl + uri.substr(0, uri.find_last_of('/') + 1);
                        auto extRoot = std::make_unique<Tile>();
                        parseNode(extRoot.get(), extJ["root"], newBase, node->transform);
                        node->children.push_back(std::move(extRoot));
                    } catch (...) {}
                }
                node->isLoaded = true;
                node->isLoading = false;
            } else if (uri.find(".b3dm") != std::string::npos) {
                auto tileRes = client->get(baseUrl + uri, token);
                if (tileRes.statusCode == 200) {
                    node->payload = std::move(tileRes.data);
                }
                node->isLoaded = true;
                node->isLoading = false;
            }
        }

        if (node->isLoaded && node->payload.size() > 0) renderList.push_back(node);
        for (auto& child : node->children) traverseAndLoad(child.get(), view, fetchedThisFrame);
    }

public:
    void parseNode(Tile* tile, const nlohmann::json& jNode, const std::string& currentBase, const glm::dmat4& parentTransform) {
        glm::dmat4 localTransform(1.0);
        if (jNode.contains("transform")) {
            auto arr = jNode["transform"];
            printf("[CesiumMinimal] Found Transform in JSON: ");
            for(auto& val : arr) printf("%f ", (double)val);
            printf("\n");
            if (arr.size() == 16) {
                for(int i=0; i<16; ++i) glm::value_ptr(localTransform)[i] = (double)arr[i];
            }
        }
        tile->transform = parentTransform * localTransform;
        printf("[CesiumMinimal] Tile Transform Matrix (Column-Major):\n");
        for(int i=0; i<4; ++i) {
            printf("[%f %f %f %f]\n", tile->transform[0][i], tile->transform[1][i], tile->transform[2][i], tile->transform[3][i]);
        }
        fflush(stdout);

        if (jNode.contains("boundingVolume")) {
            auto bv = jNode["boundingVolume"];
            if (bv.contains("region")) {
                auto arr = bv["region"];
                if (arr.size() >= 4) {
                    double rw = arr[0], rs = arr[1], re = arr[2], rn = arr[3];
                    Math::DVec3 c = Math::lla_to_ecef((rs+rn)*0.5, (rw+re)*0.5, 0.0);
                    double latD = (rn-rs)*6378137.0; double lonD = (re-rw)*6378137.0*cos((rs+rn)*0.5);
                    tile->bounds.centerECEF = c;
                    tile->bounds.radius = sqrt(latD*latD + lonD*lonD)*0.5;
                    tile->bounds.valid = true;
                }
            } else if (bv.contains("box")) {
                auto arr = bv["box"];
                if (arr.size() >= 12) {
                    glm::dvec4 localCenter((double)arr[0], (double)arr[1], (double)arr[2], 1.0);
                    glm::dvec4 worldCenter = tile->transform * localCenter;
                    tile->bounds.centerECEF = { worldCenter.x, worldCenter.y, worldCenter.z };
                    double ux=(double)arr[3], uy=(double)arr[4], uz=(double)arr[5];
                    tile->bounds.radius = sqrt(ux*ux + uy*uy + uz*uz) * 2.0; 
                    tile->bounds.valid = true;
                }
            } else if (bv.contains("sphere")) {
                auto arr = bv["sphere"];
                if (arr.size() >= 4) {
                    glm::dvec4 localCenter((double)arr[0], (double)arr[1], (double)arr[2], 1.0);
                    glm::dvec4 worldCenter = tile->transform * localCenter;
                    tile->bounds.centerECEF = { worldCenter.x, worldCenter.y, worldCenter.z };
                    tile->bounds.radius = (double)arr[3];
                    tile->bounds.valid = true;
                }
            }
        }
        
        if (jNode.contains("content") && jNode["content"].contains("uri")) {
            tile->contentUri = jNode["content"]["uri"];
        }
        
        if (jNode.contains("children")) {
            for (const auto& jChild : jNode["children"]) {
                auto childTile = std::make_unique<Tile>();
                parseNode(childTile.get(), jChild, currentBase, tile->transform);
                tile->children.push_back(std::move(childTile));
            }
        }
    }
};

} // namespace CesiumMinimal

namespace Alice {

static uint32_t g_FrameCount = 0;
static bool g_Started = false;
static bool g_Inited = false;
static bool g_Framed = false;
static int g_TotalUploadedMeshes = 0;
static std::string g_IonToken = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJqdGkiOiIyZmI0ZWVlYi02Y2FjLTRkYjYtYmIyMy1mMGNmODliZGEwNTEiLCJpZCI6NDE5MDUyLCJpYXQiOjE3NzYzMzQ0MTN9.NuD0nngJ4DbKFrQsjId4Nea_0wQ-LH0mfOIZqDh7aj0";
static int64_t g_AssetId = 96188; // St Paul's Cathedral

class DebugShader {
public:
    GLuint ID;
    void init() {
        const char* vs = R"(
#version 400 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
uniform mat4 uMVP;
out vec3 vNormal;
out vec3 vPos;
void main() {
    vNormal = aNormal;
    vPos = aPos; // Pass ENU position to fragment shader
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";
        const char* fs = R"(
#version 400 core
out vec4 FragColor;
in vec3 vNormal;
in vec3 vPos;
void main() {
    if (vPos.z < -2.0) discard; // Invisible ground clipping plane
    
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.6));
    // Fallback normal if the glTF provided 0,0,0
    vec3 n = length(vNormal) > 0.1 ? normalize(vNormal) : vec3(0.0, 0.0, 1.0);
    // Absolute dot product handles two-sided lighting perfectly
    float diff = max(abs(dot(n, lightDir)), 0.25);
    FragColor = vec4(vec3(0.85) * diff, 1.0);
}
)";
        GLuint vertex, fragment;
        vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &vs, NULL);
        glCompileShader(vertex);
        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fs, NULL);
        glCompileShader(fragment);
        ID = glCreateProgram();
        glAttachShader(ID, vertex);
        glAttachShader(ID, fragment);
        glLinkProgram(ID);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }
    void use() { glUseProgram(ID); }
    void setMVP(const float* mvp) { glUniformMatrix4fv(glGetUniformLocation(ID, "uMVP"), 1, GL_FALSE, mvp); }
};

static CesiumMinimal::AliceCurlClient g_Client;
static CesiumMinimal::Tileset g_Tileset;
static DebugShader g_DebugShader;
static Math::DVec3 g_OriginEcef;
static double g_EnuMatrix[16];

static void performStart() {
    printf("[CesiumMinimal] Starting Test for Asset %lld\n", g_AssetId);
    
    // Set up London local tangent plane (ENU)
    double lat = 51.5138 * Math::DEG2RAD;
    double lon = -0.0984 * Math::DEG2RAD;
    g_OriginEcef = Math::lla_to_ecef(lat, lon, 0.0);
    Math::denu_matrix(g_EnuMatrix, lat, lon); // Row-major
    
    AliceViewer* av = AliceViewer::instance();
    av->m_sceneAABB.initialized = false;
    g_Framed = false;
    g_TotalUploadedMeshes = 0;

    auto endpoint = CesiumMinimal::IonDiscovery::discover(g_Client, g_AssetId, g_IonToken);
    if (endpoint) {
        printf("[CesiumMinimal] Discovery Success. URL: %s\n", endpoint->url.c_str());
        auto res = g_Client.get(endpoint->url, endpoint->accessToken);
        if (res.statusCode == 200) {
            try {
                auto j = nlohmann::json::parse(res.data.begin(), res.data.end());
                g_Tileset.baseUrl = endpoint->url.substr(0, endpoint->url.find_last_of('/') + 1);
                g_Tileset.token = endpoint->accessToken;
                g_Tileset.client = &g_Client;
                g_Tileset.root = std::make_unique<CesiumMinimal::Tile>();
                g_Tileset.parseNode(g_Tileset.root.get(), j["root"], g_Tileset.baseUrl, glm::dmat4(1.0));
                printf("[CesiumMinimal] Tileset JSON Parsed.\n");
            } catch (...) {
                printf("[CesiumMinimal] JSON Error\n");
            }
        }
    }
    g_Started = true;
}

static void update(float dt) {
    (void)dt;
    if (!g_Started) return;
    AliceViewer* av = AliceViewer::instance();

    M4 view = av->camera.getViewMatrix();
    float invView[16];
    Math::mat4_inverse(invView, view.m);

    // 1. Extract Camera Position in ENU (Translation column of inverse view)
    double camEnuX = invView[12];
    double camEnuY = invView[13];
    double camEnuZ = invView[14];

    // 2. Transform ENU Position back to ECEF (using Transpose of denu_matrix)
    double camEcefX = g_OriginEcef.x + g_EnuMatrix[0]*camEnuX + g_EnuMatrix[4]*camEnuY + g_EnuMatrix[8]*camEnuZ;
    double camEcefY = g_OriginEcef.y + g_EnuMatrix[1]*camEnuX + g_EnuMatrix[5]*camEnuY + g_EnuMatrix[9]*camEnuZ;
    double camEcefZ = g_OriginEcef.z + g_EnuMatrix[2]*camEnuX + g_EnuMatrix[6]*camEnuY + g_EnuMatrix[10]*camEnuZ;

    // 3. Extract Forward Vector in ENU (Z-column of inverse view, negated for looking down -Z)
    double fwdEnuX = -invView[8];
    double fwdEnuY = -invView[9];
    double fwdEnuZ = -invView[10];

    // 4. Transform Forward Vector to ECEF
    double fwdEcefX = g_EnuMatrix[0]*fwdEnuX + g_EnuMatrix[4]*fwdEnuY + g_EnuMatrix[8]*fwdEnuZ;
    double fwdEcefY = g_EnuMatrix[1]*fwdEnuX + g_EnuMatrix[5]*fwdEnuY + g_EnuMatrix[9]*fwdEnuZ;
    double fwdEcefZ = g_EnuMatrix[2]*fwdEnuX + g_EnuMatrix[6]*fwdEnuY + g_EnuMatrix[10]*fwdEnuZ;
    
    double len = sqrt(fwdEcefX*fwdEcefX + fwdEcefY*fwdEcefY + fwdEcefZ*fwdEcefZ);
    if (len > 0.0) { fwdEcefX /= len; fwdEcefY /= len; fwdEcefZ /= len; }

    if (g_FrameCount % 100 == 0) {
        printf("[CesiumMinimal] ViewState: CamEcef=(%f, %f, %f) Dist=%f\n", camEcefX, camEcefY, camEcefZ, av->camera.distance);
        fflush(stdout);
    }

    CesiumMinimal::ViewState viewState;
    viewState.positionECEF.x = camEcefX;
    viewState.positionECEF.y = camEcefY;
    viewState.positionECEF.z = camEcefZ;
    
    viewState.directionECEF.x = fwdEcefX;
    viewState.directionECEF.y = fwdEcefY;
    viewState.directionECEF.z = fwdEcefZ;
    
    int w, h; glfwGetFramebufferSize(av->window, &w, &h);
    viewState.viewportHeight = h;
    viewState.fovY = av->fov;
    
    g_Tileset.updateView(viewState);
}

static void processGltfNode(cgltf_node* node, const glm::dmat4& currentTransform, std::vector<float>& vbo, std::vector<uint32_t>& ebo, const Math::DVec3& targetEcef, const double* enuMat, const std::optional<glm::dvec3>& rtcCenter) {
    if (!node) return;
    
    // FIX 1: Safely compute Node Matrix using GLM natively
    glm::dmat4 nodeMatrix(1.0);
    if (node->has_matrix) {
        for(int i=0; i<16; ++i) glm::value_ptr(nodeMatrix)[i] = (double)node->matrix[i];
    } else {
        glm::dmat4 t(1.0), r(1.0), s(1.0);
        if (node->has_translation) t = glm::translate(glm::dmat4(1.0), glm::dvec3(node->translation[0], node->translation[1], node->translation[2]));
        if (node->has_rotation) r = glm::mat4_cast(glm::dquat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2])); // w is index 3
        if (node->has_scale) s = glm::scale(glm::dmat4(1.0), glm::dvec3(node->scale[0], node->scale[1], node->scale[2]));
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

    if (node->mesh) {
        for (size_t j = 0; j < node->mesh->primitives_count; ++j) {
            cgltf_primitive* prim = &node->mesh->primitives[j];
            cgltf_accessor* posAcc = nullptr;
            cgltf_accessor* normAcc = nullptr;
            for (size_t k = 0; k < prim->attributes_count; ++k) {
                if (prim->attributes[k].type == cgltf_attribute_type_position) posAcc = prim->attributes[k].data;
                if (prim->attributes[k].type == cgltf_attribute_type_normal) normAcc = prim->attributes[k].data;
            }
            if (!posAcc) continue;

            uint32_t vertexOffset = (uint32_t)(vbo.size() / 6);

            for (size_t i = 0; i < posAcc->count; ++i) {
                float lp[3]; cgltf_accessor_read_float(posAcc, i, lp, 3);
                
                // FIX 3: Apply RTC Center to Local Position FIRST
                glm::dvec4 localPos = nodeMatrix * glm::dvec4((double)lp[0], (double)lp[1], (double)lp[2], 1.0);
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
                AliceViewer::instance()->m_sceneAABB.expand({lx, ly, lz});

                if (normAcc && i < normAcc->count) {
                    float ln[3]; cgltf_accessor_read_float(normAcc, i, ln, 3);
                    glm::dvec4 localNorm = nodeMatrix * glm::dvec4((double)ln[0], (double)ln[1], (double)ln[2], 0.0);
                    glm::dvec4 wNorm = tileTransform * localNorm;
                    float nx = (float)(enuMat[0]*wNorm.x + enuMat[1]*wNorm.y + enuMat[2]*wNorm.z);
                    float ny = (float)(enuMat[4]*wNorm.x + enuMat[5]*wNorm.y + enuMat[6]*wNorm.z);
                    float nz = (float)(enuMat[8]*wNorm.x + enuMat[9]*wNorm.y + enuMat[10]*wNorm.z);
                    float nlen = sqrt(nx*nx + ny*ny + nz*nz);
                    if(nlen>0){ nx/=nlen; ny/=nlen; nz/=nlen; }
                    vbo.push_back(nx); vbo.push_back(ny); vbo.push_back(nz);
                } else {
                    vbo.push_back(0.0f); vbo.push_back(1.0f); vbo.push_back(0.0f);
                }
            }

            if (prim->indices) {
                for (size_t i = 0; i < prim->indices->count; ++i) {
                    ebo.push_back((uint32_t)cgltf_accessor_read_index(prim->indices, i) + vertexOffset);
                }
            }
        }
    }

    for (size_t i = 0; i < node->children_count; ++i) {
        processGltfNode(node->children[i], currentTransform, vbo, ebo, targetEcef, enuMat, rtcCenter);
    }
}

static void draw() {
    if (!g_Started) performStart();
    AliceViewer* av = AliceViewer::instance();
    
    if (!g_Inited) {
        g_DebugShader.init();
        g_Inited = true;
    }
    
    glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    glDisable(GL_CULL_FACE);
    
    int w, h; glfwGetFramebufferSize(av->window, &w, &h); if (h < 1) h = 1;
    M4 view = av->camera.getViewMatrix(); M4 proj;
    Math::mat4_perspective(proj.m, av->fov, (float)w / h, 0.1f, 100000.0f);
    float mvp[16]; Math::mat4_mul(mvp, proj.m, view.m);
    
    g_DebugShader.use();
    g_DebugShader.setMVP(mvp);

    for (auto* tile : g_Tileset.renderList) {
        if (tile->getState() == CesiumMinimal::LoadState::Done) {
            if (!tile->getRendererResources() && tile->payload.size() > 28) {
                if (memcmp(tile->payload.data(), "b3dm", 4) == 0) {
                    uint32_t ftj = *(uint32_t*)(tile->payload.data() + 12);
                    uint32_t ftb = *(uint32_t*)(tile->payload.data() + 16);
                    uint32_t btj = *(uint32_t*)(tile->payload.data() + 20);
                    uint32_t btb = *(uint32_t*)(tile->payload.data() + 24);
                    size_t glbOffset = 28 + ftj + ftb + btj + btb;
                    
                    if (glbOffset < tile->payload.size() && memcmp(tile->payload.data() + glbOffset, "glTF", 4) == 0) {
                        printf("[CesiumMinimal] B3DM Header: ftj=%u, ftb=%u, btj=%u, btb=%u | glbOffset=%zu\n", ftj, ftb, btj, btb, glbOffset);
                        cgltf_options options = {}; cgltf_data* data = NULL;
                        if (cgltf_parse(&options, tile->payload.data() + glbOffset, tile->payload.size() - glbOffset, &data) == cgltf_result_success) {
                            cgltf_load_buffers(&options, data, nullptr);
                            
                            std::optional<glm::dvec3> rtcCenter = std::nullopt;
                            
                            // Check B3DM Feature Table JSON
                            if (ftj > 0) {
                                std::string ftJson((const char*)(tile->payload.data() + 28), ftj);
                                try {
                                    auto fj = nlohmann::json::parse(ftJson);
                                    if (fj.contains("RTC_CENTER")) {
                                        auto rtc = fj["RTC_CENTER"];
                                        if (rtc.size() == 3) {
                                            rtcCenter = glm::dvec3((double)rtc[0], (double)rtc[1], (double)rtc[2]);
                                            printf("[CesiumMinimal] Found RTC_CENTER in B3DM Feature Table.\n");
                                        }
                                    }
                                } catch (...) {}
                            }
                            
                            if (!rtcCenter.has_value()) {
                                for (size_t i = 0; i < data->data_extensions_count; ++i) {
                                    if (strcmp(data->data_extensions[i].name, "CESIUM_RTC") == 0) {
                                        const char* ext = data->data_extensions[i].data;
                                        if (ext) {
                                            const char* ctr = strstr(ext, "\"center\"");
                                            if (ctr) {
                                                const char* cb = strchr(ctr, '[');
                                                if (cb) {
                                                    const char* p = cb + 1;
                                                    double rx = atof(p); p = strchr(p, ',');
                                                    if (p) {
                                                        double ry = atof(p+1); p = strchr(p+1, ',');
                                                        if (p) {
                                                            double rz = atof(p+1);
                                                            rtcCenter = glm::dvec3(rx, ry, rz);
                                                            printf("[CesiumMinimal] Found CESIUM_RTC extension.\n");
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            auto res = new CesiumMinimal::RenderResources();
                            glGenVertexArrays(1, &res->vao);
                            glBindVertexArray(res->vao);
                            
                            std::vector<float> vbo;
                            std::vector<uint32_t> ebo;
                            
                            if (data->scene) {
                                for (size_t i = 0; i < data->scene->nodes_count; ++i) {
                                    processGltfNode(data->scene->nodes[i], tile->transform, vbo, ebo, g_OriginEcef, g_EnuMatrix, rtcCenter);
                                }
                            } else {
                                for (size_t i = 0; i < data->nodes_count; ++i) {
                                    if (!data->nodes[i].parent) {
                                        processGltfNode(&data->nodes[i], tile->transform, vbo, ebo, g_OriginEcef, g_EnuMatrix, rtcCenter);
                                    }
                                }
                            }

                            if (vbo.size() > 0) {
                                GLuint glVbo, glEbo;
                                glGenBuffers(1, &glVbo); glGenBuffers(1, &glEbo);
                                
                                glBindBuffer(GL_ARRAY_BUFFER, glVbo);
                                glBufferData(GL_ARRAY_BUFFER, vbo.size() * sizeof(float), vbo.data(), GL_STATIC_DRAW);
                                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glEbo);
                                glBufferData(GL_ELEMENT_ARRAY_BUFFER, ebo.size() * sizeof(uint32_t), ebo.data(), GL_STATIC_DRAW);
                                
                                res->vbos.push_back(glVbo); res->ebos.push_back(glEbo);
                                CesiumMinimal::RenderResources::DrawCmd cmd;
                                cmd.vao = res->vao; cmd.ebo = glEbo; cmd.count = (int)ebo.size();
                                res->commands.push_back(cmd);
                            }

                            cgltf_free(data);
                            tile->setRendererResources(res);
                            g_TotalUploadedMeshes++;
                        }
                    }
                }
            }
            
            auto res = static_cast<CesiumMinimal::RenderResources*>(tile->getRendererResources());
            if (res) {
                glBindVertexArray(res->vao);
                for (size_t k=0; k<res->commands.size(); ++k) {
                    glBindBuffer(GL_ARRAY_BUFFER, res->vbos[k]);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res->commands[k].ebo);
                    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*4, (void*)0);
                    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*4, (void*)(3*4));
                    glDrawElements(GL_TRIANGLES, res->commands[k].count, GL_UNSIGNED_INT, 0);
                }
            }
        }
    }
    
    // FIX: Dynamic Camera Framing
    if (!g_Framed && av->m_sceneAABB.initialized && g_TotalUploadedMeshes > 0) {
        av->camera.focusPoint = av->m_sceneAABB.center();
        av->camera.distance = av->m_sceneAABB.radius() * 1.2f; // Tighter framing
        if (av->camera.distance < 10.0f) av->camera.distance = 500.0f;
        
        printf("[CesiumMinimal] Reframed Camera to AABB Center: %f, %f, %f Dist: %f\n", 
            av->m_sceneAABB.center().x, av->m_sceneAABB.center().y, av->m_sceneAABB.center().z,
            av->camera.distance);
        fflush(stdout);
        g_Framed = true;
    }

    // Street-level override at Frame 20
    if (g_FrameCount == 20) {
        av->camera.focusPoint = V3(20.0f, -30.0f, 20.0f); // Offset to perfectly center the cross
        av->camera.distance = 350.0f; // Pull back to fit the whole structure
        av->camera.pitch = 0.55f;     // Steeper pitch to look down at the roof
        av->camera.yaw = 4.0f;        // Keep the opposite side view
        printf("[CesiumMinimal] OVERRIDE: Centered framing Dist: 350\n");
        fflush(stdout);
    }
    
    if (g_FrameCount == 100) {
        unsigned char* px = (unsigned char*)malloc(w * h * 3);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
        stbi_flip_vertically_on_write(true);
        stbi_write_png("framebuffer.png", w, h, 3, px, w * 3);
        free(px);
        printf("[CesiumMinimal] SUCCESS: Frame 100 captured.\n");
        fflush(stdout);
        glfwSetWindowShouldClose(av->window, true);
    }
    
    g_FrameCount++;
}

} // namespace Alice

#ifdef CESIUM_MINIMAL_TEST_RUN_TEST
extern "C" void setup() { printf("[CesiumMinimal] setup\n"); fflush(stdout); }
extern "C" void update(float dt) { Alice::update(dt); }
extern "C" void draw() { Alice::draw(); }
#endif

#endif // CESIUM_MINIMAL_TEST_H