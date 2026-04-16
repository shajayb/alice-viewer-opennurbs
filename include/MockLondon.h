#ifndef MOCK_LONDON_H
#define MOCK_LONDON_H

#include <nlohmann/json.hpp>
#include <string>
#include <cmath>
#include "AliceMath.h"

using json = nlohmann::json;

namespace Alice::Alg {

struct MockLondon
{
    static json Generate(double latDeg, double lonDeg)
    {
        double lat = latDeg * Math::DEG2RAD;
        double lon = lonDeg * Math::DEG2RAD;
        Math::DVec3 center = Math::lla_to_ecef(lat, lon, 0.0);

        json root;
        root["geometricError"] = 512.0;
        root["refine"] = "REPLACE";
        
        // Bounding volume for London area (approx 2km radius)
        root["boundingVolume"]["sphere"] = { center.x, center.y, center.z, 2048.0 };
        
        root["children"] = json::array();
        GenerateRecursive(root["children"], lat, lon, 2048.0, 1, 4);

        json tileset;
        tileset["asset"] = { {"version", "1.0"} };
        tileset["root"] = root;
        return tileset;
    }

private:
    static void GenerateRecursive(json& children, double lat, double lon, double size, int level, int maxLevel)
    {
        // 4 quadrants
        double offsets[4][2] = { {-1,-1}, {1,-1}, {-1,1}, {1,1} };
        double childSize = size * 0.5;
        double angleScale = childSize / 6378137.0;

        for (int i = 0; i < 4; ++i)
        {
            double cLat = lat + offsets[i][1] * angleScale;
            double cLon = lon + offsets[i][0] * angleScale / cos(lat);
            Math::DVec3 cPos = Math::lla_to_ecef(cLat, cLon, 0.0);

            json child;
            child["geometricError"] = 512.0 / pow(2, level);
            child["boundingVolume"]["sphere"] = { cPos.x, cPos.y, cPos.z, childSize * 1.414 };
            
            if (level < maxLevel)
            {
                child["children"] = json::array();
                GenerateRecursive(child["children"], cLat, cLon, childSize, level + 1, maxLevel);
            }
            else
            {
                // Task 1: Landmark Detection (St. Paul's: 51.5138, -0.0983)
                double targetLat = 51.5138 * Math::DEG2RAD;
                double targetLon = -0.0983 * Math::DEG2RAD;
                
                // Approximate distance in meters
                double dLat = cLat - targetLat;
                double dLon = cLon - targetLon;
                double dist = sqrt(dLat*dLat + (dLon*cos(cLat))*(dLon*cos(cLat))) * 6378137.0;

                if (dist < 200.0)
                {
                    child["content"]["uri"] = "mock://cathedral";
                }
                else
                {
                    child["content"]["uri"] = "mock://building";
                }
            }
            children.push_back(child);
        }
    }
};

} // namespace Alice::Alg

#endif
