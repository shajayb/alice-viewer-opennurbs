import re

with open("include/cesium_gepr_and_osm.h", "r") as f:
    content = f.read()

# Replace declaration
content = content.replace("static Tileset g_Tileset;", "static Tileset g_GeprTileset;\n    static Tileset g_OsmTileset;")

# Replace loadTilesetForCurrentLocation
old_load = """    static void loadTilesetForCurrentLocation() {
        double lat = g_Locations[g_CurrentLocationIndex].lat * 0.0174532925;
        double lon = g_Locations[g_CurrentLocationIndex].lon * 0.0174532925;
        double alt = g_Locations[g_CurrentLocationIndex].alt;
        g_OriginEcef = lla_to_ecef(lat, lon, alt); 
        denu_matrix(g_EnuMatrix, lat, lon);
        
        char ionToken[2048];
        if (!Alice::ApiKeyReader::GetCesiumToken(ionToken, 2048)) return;
        
        char url[512]; snprintf(url, 512, "https://api.cesium.com/v1/assets/96188/endpoint?access_token=%s", ionToken);
        static uint8_t hsBuf[1024*1024]; CesiumNetwork::FetchBuffer handshake = { hsBuf, 0, sizeof(hsBuf) };
        long sc=0;
        if (CesiumNetwork::Fetch(url, handshake, &sc)) {
            auto jhs = AliceJson::parse(handshake.data, handshake.size);
            std::string turl, atok;
            if (jhs.contains("url")) turl = jhs["url"].get<std::string>();
            else if (jhs.contains("options") && jhs["options"].contains("url")) turl = jhs["options"]["url"].get<std::string>();
            if (jhs.contains("accessToken")) atok = jhs["accessToken"].get<std::string>();
            if (!atok.empty()) strncpy(g_Tileset.token, atok.c_str(), 2047);
            if (!turl.empty()) {
                size_t kPos = turl.find("key=");
                if (kPos != std::string::npos) {
                    size_t kEnd = turl.find('&', kPos);
                    std::string key = turl.substr(kPos + 4, kEnd == std::string::npos ? std::string::npos : kEnd - (kPos + 4));
                    strncpy(g_Tileset.apiKey, key.c_str(), 255);
                }
                strncpy(g_Tileset.baseUrl, turl.substr(0, turl.find_last_of('/')+1).c_str(), 1023);
                static uint8_t tsBuf[16*1024*1024]; CesiumNetwork::FetchBuffer ts = { tsBuf, 0, sizeof(tsBuf) };
                if (CesiumNetwork::Fetch(turl.c_str(), ts, &sc, g_Tileset.token[0] ? g_Tileset.token : nullptr)) {
                    auto jts = AliceJson::parse(ts.data, ts.size);
                    if (jts.type != AliceJson::J_NULL) {
                        g_Tileset.root = (Tile*)Alice::g_Arena.allocate(sizeof(Tile));
                        memset(g_Tileset.root, 0, sizeof(Tile));
                        g_Tileset.parseNode(g_Tileset.root, jts["root"], dmat4_identity(), g_Tileset.baseUrl, 0);
                    }
                }
            }
        }
        printf("[GEPR] Switched Location to %s\\n", g_Locations[g_CurrentLocationIndex].name);
    }"""

new_load = """    static void loadTilesetForCurrentLocation() {
        double lat = g_Locations[g_CurrentLocationIndex].lat * 0.0174532925;
        double lon = g_Locations[g_CurrentLocationIndex].lon * 0.0174532925;
        double alt = g_Locations[g_CurrentLocationIndex].alt;
        g_OriginEcef = lla_to_ecef(lat, lon, alt); 
        denu_matrix(g_EnuMatrix, lat, lon);
        
        char ionToken[2048];
        if (!Alice::ApiKeyReader::GetCesiumToken(ionToken, 2048)) return;
        
        auto loadAsset = [&](uint32_t assetId, Tileset& tileset) {
            char url[512]; snprintf(url, 512, "https://api.cesium.com/v1/assets/%u/endpoint?access_token=%s", assetId, ionToken);
            static uint8_t hsBuf[1024*1024]; CesiumNetwork::FetchBuffer handshake = { hsBuf, 0, sizeof(hsBuf) };
            long sc=0;
            if (CesiumNetwork::Fetch(url, handshake, &sc)) {
                auto jhs = AliceJson::parse(handshake.data, handshake.size);
                std::string turl, atok;
                if (jhs.contains("url")) turl = jhs["url"].get<std::string>();
                else if (jhs.contains("options") && jhs["options"].contains("url")) turl = jhs["options"]["url"].get<std::string>();
                if (jhs.contains("accessToken")) atok = jhs["accessToken"].get<std::string>();
                if (!atok.empty()) strncpy(tileset.token, atok.c_str(), 2047);
                if (!turl.empty()) {
                    size_t kPos = turl.find("key=");
                    if (kPos != std::string::npos) {
                        size_t kEnd = turl.find('&', kPos);
                        std::string key = turl.substr(kPos + 4, kEnd == std::string::npos ? std::string::npos : kEnd - (kPos + 4));
                        strncpy(tileset.apiKey, key.c_str(), 255);
                    }
                    strncpy(tileset.baseUrl, turl.substr(0, turl.find_last_of('/')+1).c_str(), 1023);
                    static uint8_t tsBuf[16*1024*1024]; CesiumNetwork::FetchBuffer ts = { tsBuf, 0, sizeof(tsBuf) };
                    if (CesiumNetwork::Fetch(turl.c_str(), ts, &sc, tileset.token[0] ? tileset.token : nullptr)) {
                        auto jts = AliceJson::parse(ts.data, ts.size);
                        if (jts.type != AliceJson::J_NULL) {
                            tileset.root = (Tile*)Alice::g_Arena.allocate(sizeof(Tile));
                            memset(tileset.root, 0, sizeof(Tile));
                            tileset.parseNode(tileset.root, jts["root"], dmat4_identity(), tileset.baseUrl, 0);
                        }
                    }
                }
            }
        };
        
        loadAsset(2275207, g_GeprTileset);
        loadAsset(96188, g_OsmTileset);
        
        printf("[GEPR] Switched Location to %s\\n", g_Locations[g_CurrentLocationIndex].name);
    }"""
content = content.replace(old_load, new_load)

# Replace init
content = content.replace("g_Tileset.init();", "g_GeprTileset.init();\n        g_OsmTileset.init();")

# Replace update
old_update = """        Frustum& f = g_Tileset.currentFrustum;
        f.planes[0] = {mvp[3]+mvp[0], mvp[7]+mvp[4], mvp[11]+mvp[8], mvp[15]+mvp[12]}; // Left
        f.planes[1] = {mvp[3]-mvp[0], mvp[7]-mvp[4], mvp[11]-mvp[8], mvp[15]-mvp[12]}; // Right
        f.planes[2] = {mvp[3]+mvp[1], mvp[7]+mvp[5], mvp[11]+mvp[9], mvp[15]+mvp[13]}; // Bottom
        f.planes[3] = {mvp[3]-mvp[1], mvp[7]-mvp[5], mvp[11]-mvp[9], mvp[15]-mvp[13]}; // Top
        f.planes[4] = {mvp[3]+mvp[2], mvp[7]+mvp[6], mvp[11]+mvp[10], mvp[15]+mvp[14]}; // Near
        f.planes[5] = {mvp[3]-mvp[2], mvp[7]-mvp[6], mvp[11]-mvp[10], mvp[15]-mvp[14]}; // Far
        for(int i=0; i<6; ++i) f.planes[i].normalize();

        g_TilesLoadedThisFrame = 0;
        g_Tileset.renderListCount = 0; if (g_Tileset.root) g_Tileset.traverse(g_Tileset.root, 0);"""
new_update = """        Frustum& f = g_GeprTileset.currentFrustum;
        f.planes[0] = {mvp[3]+mvp[0], mvp[7]+mvp[4], mvp[11]+mvp[8], mvp[15]+mvp[12]}; // Left
        f.planes[1] = {mvp[3]-mvp[0], mvp[7]-mvp[4], mvp[11]-mvp[8], mvp[15]-mvp[12]}; // Right
        f.planes[2] = {mvp[3]+mvp[1], mvp[7]+mvp[5], mvp[11]+mvp[9], mvp[15]+mvp[13]}; // Bottom
        f.planes[3] = {mvp[3]-mvp[1], mvp[7]-mvp[5], mvp[11]-mvp[9], mvp[15]-mvp[13]}; // Top
        f.planes[4] = {mvp[3]+mvp[2], mvp[7]+mvp[6], mvp[11]+mvp[10], mvp[15]+mvp[14]}; // Near
        f.planes[5] = {mvp[3]-mvp[2], mvp[7]-mvp[6], mvp[11]-mvp[10], mvp[15]-mvp[14]}; // Far
        for(int i=0; i<6; ++i) f.planes[i].normalize();
        
        g_OsmTileset.currentFrustum = f;

        g_TilesLoadedThisFrame = 0;
        g_GeprTileset.renderListCount = 0; if (g_GeprTileset.root) g_GeprTileset.traverse(g_GeprTileset.root, 0);
        g_OsmTileset.renderListCount = 0; if (g_OsmTileset.root) g_OsmTileset.traverse(g_OsmTileset.root, 0);"""
content = content.replace(old_update, new_update)

# Replace evict
content = content.replace(
    "if (g_Tileset.root && (g_CurrentState == STATE_STREAMING || g_CurrentState == STATE_STREAMING_STENCIL_WAIT)) g_Tileset.evictStaleTiles(g_Tileset.root);",
    "if (g_GeprTileset.root && (g_CurrentState == STATE_STREAMING || g_CurrentState == STATE_STREAMING_STENCIL_WAIT)) g_GeprTileset.evictStaleTiles(g_GeprTileset.root);\n        if (g_OsmTileset.root && (g_CurrentState == STATE_STREAMING || g_CurrentState == STATE_STREAMING_STENCIL_WAIT)) g_OsmTileset.evictStaleTiles(g_OsmTileset.root);"
)

# Replace sceneAABB
old_aabb = """        for (uint32_t i=0; i<g_Tileset.renderListCount; ++i) {
            AABB& tb = g_Tileset.renderList[i]->localAABB.initialized ? g_Tileset.renderList[i]->localAABB : g_Tileset.renderList[i]->boundingAABB;
            if (tb.initialized) {
                av->m_sceneAABB.expand(tb.m_min);
                av->m_sceneAABB.expand(tb.m_max);
            }
        }"""
new_aabb = """        for (uint32_t i=0; i<g_GeprTileset.renderListCount; ++i) {
            AABB& tb = g_GeprTileset.renderList[i]->localAABB.initialized ? g_GeprTileset.renderList[i]->localAABB : g_GeprTileset.renderList[i]->boundingAABB;
            if (tb.initialized) {
                av->m_sceneAABB.expand(tb.m_min);
                av->m_sceneAABB.expand(tb.m_max);
            }
        }
        for (uint32_t i=0; i<g_OsmTileset.renderListCount; ++i) {
            AABB& tb = g_OsmTileset.renderList[i]->localAABB.initialized ? g_OsmTileset.renderList[i]->localAABB : g_OsmTileset.renderList[i]->boundingAABB;
            if (tb.initialized) {
                av->m_sceneAABB.expand(tb.m_min);
                av->m_sceneAABB.expand(tb.m_max);
            }
        }"""
content = content.replace(old_aabb, new_aabb)

# Replace frame render list print
content = content.replace(
    "if (frameIdx % 20 == 0) { printf(\"[GEPR] Frame %u, RenderList: %u\\n\", frameIdx, g_Tileset.renderListCount); fflush(stdout); }",
    "if (frameIdx % 20 == 0) { printf(\"[GEPR] Frame %u, GEPR RenderList: %u, OSM RenderList: %u\\n\", frameIdx, g_GeprTileset.renderListCount, g_OsmTileset.renderListCount); fflush(stdout); }"
)

# Replace streaming resource creation
old_streaming = """        if (g_CurrentState == STATE_STREAMING || g_CurrentState == STATE_STREAMING_STENCIL_WAIT) {
            for (uint32_t i=0; i<g_Tileset.renderListCount; ++i) {
                Tile* t = g_Tileset.renderList[i];"""
new_streaming = """        if (g_CurrentState == STATE_STREAMING || g_CurrentState == STATE_STREAMING_STENCIL_WAIT) {
            auto processList = [&](Tileset& ts) {
                for (uint32_t i=0; i<ts.renderListCount; ++i) {
                    Tile* t = ts.renderList[i];"""
content = content.replace(old_streaming, new_streaming)

# And close the processList lambda
old_streaming_end = """                        cgltf_free(data);
                    }
                }
            }
            
            GLint locPass = glGetUniformLocation(g_Program, "uPass");
            for (int pass = 0; pass < 2; ++pass) {
                if (pass == 0) { glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glUniform1i(locPass, 0); }
                else { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -1.0f); glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glUniform1i(locPass, 1); }
                
                for (uint32_t i=0; i<g_Tileset.renderListCount; ++i) {
                    Tile* t = g_Tileset.renderList[i];
                    if (t->rendererResources) { 
                        glBindVertexArray(t->rendererResources->vao); 
                        glDrawElements(GL_TRIANGLES, t->rendererResources->count, GL_UNSIGNED_INT, 0); 
                        if (pass == 0) g_TotalFrustumVertices += (uint64_t)t->rendererResources->count; 
                    }
                }
            }
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glDisable(GL_POLYGON_OFFSET_FILL);"""
new_streaming_end = """                        cgltf_free(data);
                    }
                }
                }
            };
            processList(g_GeprTileset);
            processList(g_OsmTileset);
            
            GLint locPass = glGetUniformLocation(g_Program, "uPass");
            
            // Pass 0: GEPR Filled
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glUniform1i(locPass, 0);
            for (uint32_t i=0; i<g_GeprTileset.renderListCount; ++i) {
                Tile* t = g_GeprTileset.renderList[i];
                if (t->rendererResources) { 
                    glBindVertexArray(t->rendererResources->vao); 
                    glDrawElements(GL_TRIANGLES, t->rendererResources->count, GL_UNSIGNED_INT, 0); 
                    g_TotalFrustumVertices += (uint64_t)t->rendererResources->count; 
                }
            }
            
            // Pass 1: OSM Wireframe
            glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -1.0f); glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glUniform1i(locPass, 1);
            for (uint32_t i=0; i<g_OsmTileset.renderListCount; ++i) {
                Tile* t = g_OsmTileset.renderList[i];
                if (t->rendererResources) { 
                    glBindVertexArray(t->rendererResources->vao); 
                    glDrawElements(GL_TRIANGLES, t->rendererResources->count, GL_UNSIGNED_INT, 0); 
                }
            }
            
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glDisable(GL_POLYGON_OFFSET_FILL);"""
content = content.replace(old_streaming_end, new_streaming_end)

# Replace frame status print
content = content.replace(
    "printf(\"[GEPR] Frame %u, Depth: %u, RenderList: %u, Payloads: %u/%d, Resources: %u/%d\\n\", frameIdx, g_MaxDepthReached, g_Tileset.renderListCount, activePayloads, PAYLOAD_POOL_SIZE, activeResources, RESOURCE_POOL_SIZE);",
    "printf(\"[GEPR] Frame %u, Depth: %u, GEPR: %u, OSM: %u, Payloads: %u/%d, Resources: %u/%d\\n\", frameIdx, g_MaxDepthReached, g_GeprTileset.renderListCount, g_OsmTileset.renderListCount, activePayloads, PAYLOAD_POOL_SIZE, activeResources, RESOURCE_POOL_SIZE);"
)

# Replace checking for silence
content = content.replace(
    "if (g_CurrentState == STATE_STREAMING && frameIdx - g_StateFrameStart > 400 && CesiumNetwork::g_AsyncRequests.size() == 0 && g_Tileset.renderListCount > 0)",
    "if (g_CurrentState == STATE_STREAMING && frameIdx - g_StateFrameStart > 400 && CesiumNetwork::g_AsyncRequests.size() == 0 && g_GeprTileset.renderListCount > 0 && g_OsmTileset.renderListCount > 0)"
)

# Replace AGGREGATE
old_agg = """        } else if (g_CurrentState == STATE_AGGREGATE) {
            std::vector<float> totalVBO; std::vector<uint32_t> totalEBO;
            for (uint32_t i = 0; i < g_Tileset.renderListCount; ++i) {
                Tile* t = g_Tileset.renderList[i];"""
new_agg = """        } else if (g_CurrentState == STATE_AGGREGATE) {
            std::vector<float> totalVBO; std::vector<uint32_t> totalEBO;
            auto appendList = [&](Tileset& ts) {
                for (uint32_t i = 0; i < ts.renderListCount; ++i) {
                    Tile* t = ts.renderList[i];"""
content = content.replace(old_agg, new_agg)

old_agg_end = """                        cgltf_free(data);
                    }
                }
            }
            char binPath[256]; snprintf(binPath, 256, "build/bin/OUTPUT/cache_gepr_loc%d_view%d.bin", g_CurrentLocationIndex, g_CurrentViewIndex);"""
new_agg_end = """                        cgltf_free(data);
                    }
                }
                }
            };
            appendList(g_GeprTileset);
            appendList(g_OsmTileset);
            char binPath[256]; snprintf(binPath, 256, "build/bin/OUTPUT/cache_gepr_loc%d_view%d.bin", g_CurrentLocationIndex, g_CurrentViewIndex);"""
content = content.replace(old_agg_end, new_agg_end)

# Export 3DM
old_export = """                for (uint32_t i = 0; i < g_Tileset.renderListCount; ++i) {
                    Tile* t = g_Tileset.renderList[i];"""
new_export = """                for (uint32_t i = 0; i < g_OsmTileset.renderListCount; ++i) { // Only export OSM for 3DM
                    Tile* t = g_OsmTileset.renderList[i];"""
content = content.replace(old_export, new_export)

with open("include/cesium_gepr_and_osm.h", "w") as f:
    f.write(content)
