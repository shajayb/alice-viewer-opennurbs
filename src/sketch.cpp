#include <vector>
#include <cstdio>
#include <GLFW/glfw3.h>
#include "AliceViewer.h"

static std::vector<V3> points;
static float rotation = 0.0f;

void setup()
{
    printf("-------------------------------------------\n");
    printf("Alice CAD Framework - Sketch Version 2.0.5\n");
    printf("Controls:\n");
    printf("  Alt + LMB: Orbit\n");
    printf("  MMB: Pan\n");
    printf("  Alt + RMB Drag or Scroll: Zoom\n");
    printf("  LMB: Add Point\n");
    printf("  'C': Clear Sketch\n");
    printf("-------------------------------------------\n");
    
    // Initial dummy points
    for(int i=-10; i<=10; ++i) 
    {
        points.push_back({(float)i, sinf((float)i * 0.5f)*5.0f, 0.0f});
    }
}

void update(float dt)
{
    rotation += dt;
}

void draw()
{
    drawGrid(20.0f);

    // Highlighted curve
    aliceColor3f(0.8f, 0.2f, 0.3f);
    glPointSize(8.0f);
    glLineWidth(2.5f);
    
    if (points.size() > 1)
    {
        for(size_t i = 0; i + 1 < points.size(); ++i) 
        {
            drawLine(points[i], points[i+1]);
            drawPoint(points[i]);
        }
        drawPoint(points.back());
    }
    else if (points.size() == 1)
    {
        drawPoint(points[0]);
    }

    // Rotating pointer
    aliceColor3f(0.1f, 0.6f, 0.9f);
    drawLine({0,0,0}, {cosf(rotation)*10.0f, sinf(rotation)*10.0f, 0});
}

void keyPress(unsigned char key, int x, int y)
{
    if(key == 'C' || key == 'c') 
    {
        points.clear();
        printf("Sketch cleared.\n");
    }
}

void mousePress(int button, int state, int x, int y)
{
    GLFWwindow* window = AliceViewer::instance()->window;
    bool isAlt = (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);

    // button 0 = LMB, state 1 = PRESS
    if(button == 0 && state == 1 && !isAlt) 
    {
        // Use the new screenToWorld utility for accurate coordinate space transformation
        V3 worldPos = AliceViewer::instance()->screenToWorld(x, y);
        points.push_back(worldPos);
        
        printf("Point added at: %.2f, %.2f, %.2f\n", worldPos.x, worldPos.y, worldPos.z);
    }
}

void mouseMotion(int x, int y)
{
}
