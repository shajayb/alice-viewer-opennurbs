#include "AliceViewer.h"
#include <iostream>

// Implementation of user-defined functions
void setup() {
    std::cout << "Application Setup" << std::endl;
}

void update() {
    // Logic update here
}

void draw() {
    // Modern OpenGL drawing calls here
    
    // Example ImGui window
    ImGui::Begin("Alice Viewer Controls");
    ImGui::Text("Welcome to the Alice Viewer!");
    static float color[3] = { 0.1f, 0.1f, 0.1f };
    ImGui::ColorEdit3("Background Color", color);
    ImGui::End();
}

void mouse(int button, int action, double xpos, double ypos) {
    if (action == GLFW_PRESS) {
        std::cout << "Mouse pressed: " << button << " at (" << xpos << ", " << ypos << ")" << std::endl;
    }
}

int main() {
    AliceViewer app("AliceViewer - OpenGL 4.0", 1280, 720);

    if (app.init()) {
        app.run();
    }

    return 0;
}
