#pragma once

#include <string>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

// Forward declarations of user-defined functions
void setup();
void update();
void draw();
void mouse(int button, int action, double xpos, double ypos);

class AliceViewer {
public:
    AliceViewer(const std::string& title = "AliceViewer", int width = 1280, int height = 720);
    ~AliceViewer();

    bool init();
    void run();

private:
    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
    static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
    static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
    static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

    void processInput();

    GLFWwindow* m_window;
    std::string m_title;
    int m_width;
    int m_height;
};
