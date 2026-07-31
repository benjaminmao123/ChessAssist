#include "AppWindow.h"

#include <spdlog/spdlog.h>

#include <imgui.h>

#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef _WIN32
// GL/gl.h relies on WINGDIAPI/APIENTRY, which it expects windows.h to have already
// defined - unlike Linux's GL/gl.h, which has no such dependency.
#define NOMINMAX
#include <windows.h>
#endif

#include <GL/gl.h>

namespace
{
void GlfwErrorCallback(int error, const char* description)
{
    spdlog::error("GLFW error {}: {}", error, description);
}
}  // namespace

struct AppWindow::Impl
{
    GLFWwindow* Window = nullptr;
};

AppWindow::AppWindow()
    : m_Impl(std::make_unique<Impl>())
{
}

AppWindow::~AppWindow()
{
    Shutdown();
}

bool AppWindow::Init(int width, int height, const std::string& title)
{
    glfwSetErrorCallback(GlfwErrorCallback);

    if (!glfwInit())
        return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    m_Impl->Window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_Impl->Window)
    {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_Impl->Window);
    glfwSwapInterval(1);  // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(m_Impl->Window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    return true;
}

void AppWindow::Shutdown()
{
    if (!m_Impl->Window)
        return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(m_Impl->Window);
    m_Impl->Window = nullptr;

    glfwTerminate();
}

bool AppWindow::ShouldClose() const
{
    return m_Impl->Window && glfwWindowShouldClose(m_Impl->Window);
}

void AppWindow::BeginFrame()
{
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Gives every ImGui::Begin() window a full-viewport area to dock into; the central
    // node stays transparent so it doesn't paint over anything drawn behind it.
    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
}

void AppWindow::EndFrame()
{
    ImGui::Render();

    int displayWidth = 0;
    int displayHeight = 0;
    glfwGetFramebufferSize(m_Impl->Window, &displayWidth, &displayHeight);

    glViewport(0, 0, displayWidth, displayHeight);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(m_Impl->Window);
}
