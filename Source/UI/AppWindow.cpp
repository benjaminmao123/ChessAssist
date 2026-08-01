#include "AppWindow.h"

#include "../Logging/Log.h"

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
    LOG_ERROR("GLFW error {}: {}", error, description);
}

// Fixed UI enlargement applied once at startup so text/widgets are easier to read - not tied
// to window size (see BeginFrame - resize-driven scaling was tried and reverted).
constexpr float kUiScale = 1.5f;

// A flatter, more rounded dark theme with a teal accent, layered on top of ImGui's default
// dark palette rather than replacing it wholesale - anything not overridden below still comes
// from StyleColorsDark().
void ApplyModernDarkTheme()
{
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.94f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.52f, 0.55f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.098f, 0.106f, 0.122f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.098f, 0.106f, 0.122f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.110f, 0.118f, 0.135f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.22f, 0.26f, 0.60f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.150f, 0.163f, 0.188f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.190f, 0.207f, 0.239f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.220f, 0.240f, 0.278f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.078f, 0.086f, 0.098f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.110f, 0.290f, 0.360f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.078f, 0.086f, 0.098f, 0.75f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.110f, 0.118f, 0.135f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.078f, 0.086f, 0.098f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.250f, 0.270f, 0.310f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.310f, 0.340f, 0.390f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.150f, 0.550f, 0.650f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.150f, 0.680f, 0.780f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.150f, 0.600f, 0.700f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.150f, 0.680f, 0.780f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.160f, 0.400f, 0.460f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.150f, 0.550f, 0.650f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.120f, 0.620f, 0.720f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.160f, 0.400f, 0.460f, 0.70f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.150f, 0.550f, 0.650f, 0.85f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.120f, 0.620f, 0.720f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.220f, 0.240f, 0.278f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.150f, 0.550f, 0.650f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.150f, 0.680f, 0.780f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.150f, 0.550f, 0.650f, 0.30f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.150f, 0.550f, 0.650f, 0.65f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.150f, 0.680f, 0.780f, 0.90f);
    colors[ImGuiCol_Tab] = ImVec4(0.110f, 0.118f, 0.135f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.150f, 0.550f, 0.650f, 0.80f);
    colors[ImGuiCol_TabActive] = ImVec4(0.150f, 0.400f, 0.460f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.090f, 0.098f, 0.112f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.130f, 0.230f, 0.260f, 1.00f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.150f, 0.550f, 0.650f, 0.60f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.078f, 0.086f, 0.098f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.62f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.150f, 0.680f, 0.780f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.150f, 0.600f, 0.700f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.150f, 0.680f, 0.780f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.150f, 0.550f, 0.650f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.150f, 0.680f, 0.780f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.150f, 0.680f, 0.780f, 1.00f);
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

    ApplyModernDarkTheme();
    ImGui::GetStyle().ScaleAllSizes(kUiScale);
    io.FontGlobalScale = kUiScale;

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

void AppWindow::Minimize()
{
    if (m_Impl->Window)
        glfwIconifyWindow(m_Impl->Window);
}

void AppWindow::Restore()
{
    if (m_Impl->Window)
        glfwRestoreWindow(m_Impl->Window);
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
