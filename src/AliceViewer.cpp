#include "AliceViewer.h"
#include <iostream>

AliceViewer::AliceViewer(const std::string& title, int width, int height)
    : m_title(title), m_width(width), m_height(height), m_window(nullptr) {}

AliceViewer::~AliceViewer() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_window) {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}

bool AliceViewer::init() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);
    if (!m_window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // Enable v-sync

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    // Set callbacks
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebuffer_size_callback);
    glfwSetMouseButtonCallback(m_window, mouse_button_callback);
    glfwSetCursorPosCallback(m_window, cursor_position_callback);
    glfwSetScrollCallback(m_window, scroll_callback);
    glfwSetKeyCallback(m_window, key_callback);

    return true;
}

void AliceViewer::run() {
    setup();

    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        processInput();

        update();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Clear screen
        int display_w, display_h;
        glfwGetFramebufferSize(m_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        draw();

        // Render ImGui
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(m_window);
    }
}

void AliceViewer::processInput() {
    if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(m_window, true);
}

void AliceViewer::framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void AliceViewer::mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    mouse(button, action, xpos, ypos);
}

void AliceViewer::cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    // Optionally handle motion here if needed, or pass to mouse() with a custom action code
}

void AliceViewer::scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    // Handle scroll
}

void AliceViewer::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    // Handle keys
}
