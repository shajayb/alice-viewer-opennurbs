#include "AliceViewer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <vector>
#include <algorithm>

static const char* vertexShaderSource = R"(#version 410 core
layout(location = 0) in vec3 position;
uniform mat4 mvp;
void main()
{
    gl_Position = mvp * vec4(position, 1.0);
}
)";

static const char* fragmentShaderSource = R"(#version 410 core
out vec4 fragColor;
void main()
{
    fragColor = vec4(0.4, 0.4, 0.4, 1.0);
}
)";

M4 ArcballCamera::getViewMatrix() const
{
    float cosPitch = cosf(pitch);
    float sinPitch = sinf(pitch);
    float cosYaw = cosf(yaw);
    float sinYaw = sinf(yaw);

    V3 eye;
    eye.x = focusPoint.x + distance * cosPitch * sinYaw;
    eye.y = focusPoint.y - distance * cosPitch * cosYaw;
    eye.z = focusPoint.z + distance * sinPitch;

    return lookAt(eye, focusPoint, { 0.0f, 0.0f, 1.0f });
}

void AliceViewer::init()
{
    glfwInit();
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    
    window = glfwCreateWindow(1280, 720, "AliceViewer - CAD Engine", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410 core");

    camera.focusPoint = { 0.0f, 0.0f, 0.0f };
    camera.distance = 20.0f;
    camera.yaw = 0.785f; // 45 degrees
    camera.pitch = 0.6f;
    camera.lastMouseX = 0.0f;
    camera.lastMouseY = 0.0f;

    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, nullptr);
    glCompileShader(vs);
    
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fs);
    
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vs);
    glAttachShader(shaderProgram, fs);
    glLinkProgram(shaderProgram);
    
    glDeleteShader(vs);
    glDeleteShader(fs);

    std::vector<float> gridData;
    for (int i = -20; i <= 20; ++i)
    {
        gridData.push_back((float)i); gridData.push_back(-20.0f); gridData.push_back(0.0f);
        gridData.push_back((float)i); gridData.push_back(20.0f); gridData.push_back(0.0f);
        
        gridData.push_back(-20.0f); gridData.push_back((float)i); gridData.push_back(0.0f);
        gridData.push_back(20.0f); gridData.push_back((float)i); gridData.push_back(0.0f);
    }
    gridVertexCount = (int)(gridData.size() / 3);

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, gridData.size() * sizeof(float), gridData.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
}

void AliceViewer::run()
{
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        
        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        float dx = (float)mouseX - camera.lastMouseX;
        float dy = (float)mouseY - camera.lastMouseY;
        camera.lastMouseX = (float)mouseX;
        camera.lastMouseY = (float)mouseY;

        if (!io.WantCaptureMouse)
        {
            // Left Mouse Button: Orbit / Tumble
            if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
            {
                camera.yaw -= dx * 0.005f;
                camera.pitch += dy * 0.005f;
            }
            // Middle Mouse Button: Track / Pan
            else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
            {
                float cosPitch = cosf(camera.pitch);
                float sinPitch = sinf(camera.pitch);
                float cosYaw = cosf(camera.yaw);
                float sinYaw = sinf(camera.yaw);

                V3 eye;
                eye.x = camera.focusPoint.x + camera.distance * cosPitch * sinYaw;
                eye.y = camera.focusPoint.y - camera.distance * cosPitch * cosYaw;
                eye.z = camera.focusPoint.z + camera.distance * sinPitch;

                V3 forward = nrm(sub(camera.focusPoint, eye));
                V3 worldUp = { 0.0f, 0.0f, 1.0f };
                V3 right = nrm(crs(forward, worldUp));
                V3 up = crs(right, forward);

                float panScale = camera.distance * 0.001f;
                camera.focusPoint = add(camera.focusPoint, mul(right, -dx * panScale));
                camera.focusPoint = add(camera.focusPoint, mul(up, dy * panScale));
            }
            // Right Mouse Button: Dolly / Smooth Zoom
            else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
            {
                camera.distance += dy * 0.1f;
            }

            // Scroll Wheel: Stepped Zoom
            if (io.MouseWheel != 0.0f)
            {
                if (io.MouseWheel > 0.0f)
                {
                    camera.distance *= 0.9f;
                }
                else
                {
                    camera.distance *= 1.1f;
                }
            }
        }

        // Constraints
        camera.pitch = std::max(-1.55f, std::min(1.55f, camera.pitch)); // approx -89 to 89 degrees
        if (camera.distance < 0.1f)
        {
            camera.distance = 0.1f;
        }

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (width <= 0 || height <= 0)
        {
            continue;
        }
        
        glViewport(0, 0, width, height);
        glClearColor(0.85f, 0.85f, 0.85f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        float aspectRatio = (float)width / (float)height;
        M4 projection = perspective(0.785f, aspectRatio, 0.1f, 1000.0f);
        M4 view = camera.getViewMatrix();
        M4 mvp = mult(projection, view);

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "mvp"), 1, GL_FALSE, mvp.m);
        
        glBindVertexArray(vao);
        glDrawArrays(GL_LINES, 0, gridVertexCount);

        ImGui::Begin("Camera Debug");
        ImGui::Text("Yaw: %.3f", camera.yaw);
        ImGui::Text("Pitch: %.3f", camera.pitch);
        ImGui::Text("Distance: %.3f", camera.distance);
        ImGui::Text("Focus: %.2f, %.2f, %.2f", camera.focusPoint.x, camera.focusPoint.y, camera.focusPoint.z);
        if (ImGui::Button("Reset Camera"))
        {
            camera.focusPoint = { 0.0f, 0.0f, 0.0f };
            camera.distance = 20.0f;
            camera.yaw = 0.785f;
            camera.pitch = 0.6f;
        }
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        glfwSwapBuffers(window);
    }
}

void AliceViewer::cleanup()
{
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(shaderProgram);
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    
    glfwDestroyWindow(window);
    glfwTerminate();
}
