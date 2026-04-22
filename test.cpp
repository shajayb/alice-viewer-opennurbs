#include <iostream>
#include <cmath>
int main() {
    double lat = -22.951916 * 0.0174532925;
    double lon = -43.210487 * 0.0174532925;
    double alt = 710.0;
    const double a = 6378137.0;
    const double e2 = 0.0066943799901413165; 
    double sLat = sin(lat), cLat = cos(lat);
    double sLon = sin(lon), cLon = cos(lon);
    double N = a / sqrt(1.0 - e2 * sLat * sLat);
    double x = (N + alt) * cLat * cLon;
    double y = (N + alt) * cLat * sLon;
    double z = (N * (1.0 - e2) + alt) * sLat;
    std::cout << x << \" \" << y << \" \" << z << std::endl;
    return 0;
}
