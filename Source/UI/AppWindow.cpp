#include "AppWindow.h"

#include "Engine/ExecutablePathUtil.h"
#include "Logging/Log.h"

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

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <stb_image.h>

namespace
{
void GlfwErrorCallback(int error, const char* description)
{
    LOG_ERROR("GLFW error {}: {}", error, description);
}

// Sets the window's title bar/taskbar/Alt-Tab icon from Assets/Icons/window_icon.png - GLFW
// copies the pixel data internally, so the source buffer only needs to outlive this call.
// Silently leaves the platform default icon in place (just logs) if the file is missing or
// fails to decode - not worth failing startup over.
void SetWindowIcon(GLFWwindow* window)
{
    const std::filesystem::path iconPath = ExecutablePathUtil::GetAssetsDirectory() / "Icons" / "window_icon.png";

    GLFWimage icon{};
    unsigned char* pixels = stbi_load(iconPath.string().c_str(), &icon.width, &icon.height, nullptr, 4);
    if (!pixels)
    {
        LOG_ERROR("SetWindowIcon: failed to load {} - leaving the default window icon in place", iconPath.string());
        return;
    }

    icon.pixels = pixels;
    glfwSetWindowIcon(window, 1, &icon);
    stbi_image_free(pixels);
}

// Fixed UI enlargement applied once at startup so text/widgets are easier to read - not tied
// to window size (see BeginFrame - resize-driven scaling was tried and reverted).
constexpr float kUiScale = 1.5f;

// Baked directly into the font atlas at the final on-screen size (rather than loading at a
// base size and stretching via io.FontGlobalScale) so text stays crisp instead of blurring
// from bitmap upscaling.
constexpr float kBaseFontSize = 16.0f;

// Loads the bundled Roboto Medium as the default font. Falls back to the built-in font
// (AddFontDefault) if the bundled .ttf isn't found next to the executable.
void LoadUiFont(ImGuiIO& io)
{
    const std::filesystem::path fontPath = ExecutablePathUtil::GetAssetsDirectory() / "Fonts" / "Roboto-Medium.ttf";

    ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), kBaseFontSize * kUiScale);
    if (!font)
    {
        LOG_ERROR("Failed to load UI font from {} - falling back to the built-in font", fontPath.string());
        io.Fonts->AddFontDefault();
    }
}

// A near-black "dashboard" dark theme layered on top of ImGui's default dark palette rather
// than replacing it wholesale - anything not overridden below still comes from StyleColorsDark().
void ApplyModernDarkTheme()
{
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 10.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;

    // Single accent used sparingly (checkbox fill, focus/drag/dock highlights) - everything
    // else in the screenshot's reference palette is neutral gray/white.
    constexpr ImVec4 kAccentBlue(0.231f, 0.510f, 0.965f, 1.00f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.96f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.55f, 0.58f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.051f, 0.051f, 0.059f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.051f, 0.051f, 0.059f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.086f, 0.086f, 0.098f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.15f, 0.15f, 0.17f, 0.55f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.102f, 0.102f, 0.118f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.129f, 0.129f, 0.149f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.157f, 0.157f, 0.180f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.039f, 0.039f, 0.047f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.059f, 0.059f, 0.071f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.039f, 0.039f, 0.047f, 0.75f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.075f, 0.075f, 0.086f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.039f, 0.039f, 0.047f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.165f, 0.165f, 0.188f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.208f, 0.208f, 0.235f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = kAccentBlue;
    colors[ImGuiCol_CheckMark] = kAccentBlue;
    colors[ImGuiCol_SliderGrab] = ImVec4(0.85f, 0.85f, 0.87f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.97f, 0.97f, 0.98f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.102f, 0.102f, 0.118f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.145f, 0.145f, 0.165f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.184f, 0.184f, 0.208f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.129f, 0.129f, 0.149f, 0.90f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.157f, 0.157f, 0.180f, 0.90f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.184f, 0.184f, 0.208f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.157f, 0.157f, 0.180f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(kAccentBlue.x, kAccentBlue.y, kAccentBlue.z, 0.60f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(kAccentBlue.x, kAccentBlue.y, kAccentBlue.z, 0.90f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(kAccentBlue.x, kAccentBlue.y, kAccentBlue.z, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(kAccentBlue.x, kAccentBlue.y, kAccentBlue.z, 0.55f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(kAccentBlue.x, kAccentBlue.y, kAccentBlue.z, 0.80f);
    colors[ImGuiCol_Tab] = ImVec4(0.075f, 0.075f, 0.086f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.157f, 0.157f, 0.180f, 0.90f);
    colors[ImGuiCol_TabActive] = ImVec4(0.129f, 0.129f, 0.149f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.059f, 0.059f, 0.071f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.094f, 0.094f, 0.106f, 1.00f);
    colors[ImGuiCol_DockingPreview] = ImVec4(kAccentBlue.x, kAccentBlue.y, kAccentBlue.z, 0.50f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.039f, 0.039f, 0.047f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.75f, 0.75f, 0.78f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = kAccentBlue;
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.85f, 0.85f, 0.87f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.97f, 0.97f, 0.98f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(kAccentBlue.x, kAccentBlue.y, kAccentBlue.z, 0.35f);
    colors[ImGuiCol_DragDropTarget] = kAccentBlue;
    colors[ImGuiCol_NavHighlight] = kAccentBlue;
}
}  // namespace

struct AppWindow::Impl
{
    GLFWwindow* Window = nullptr;

    // io.IniFilename only stores a pointer, not a copy - this has to outlive the ImGui context
    // (set once in Init(), read by Shutdown()'s final SaveIniSettingsToDisk() flush).
    std::string IniFilePath;
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

    SetWindowIcon(m_Impl->Window);

    glfwMakeContextCurrent(m_Impl->Window);
    glfwSwapInterval(1);  // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Pinned next to the executable instead of ImGui's default (CWD-relative) "imgui.ini", so
    // the saved dock layout is found reliably regardless of how the app was launched. App::Run()
    // reads whether this file already existed at startup to decide whether to force its own
    // default dock layout or leave the user's restored one alone (see SetupDefaultDockLayout()).
    m_Impl->IniFilePath = ExecutablePathUtil::GetImGuiIniFilePath().string();
    io.IniFilename = m_Impl->IniFilePath.c_str();

    ApplyModernDarkTheme();
    ImGui::GetStyle().ScaleAllSizes(kUiScale);
    LoadUiFont(io);

    ImGui_ImplGlfw_InitForOpenGL(m_Impl->Window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    return true;
}

void AppWindow::Shutdown()
{
    if (!m_Impl->Window)
        return;

    // Flushes any layout change still pending ImGui's own periodic autosave - without this,
    // rearranging docked windows and closing the app shortly after could silently lose the
    // change.
    ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);

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

void AppWindow::NewFrame()
{
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

unsigned int AppWindow::SetupDockspace()
{
    // Gives every ImGui::Begin() window a full-viewport area to dock into; the central
    // node stays transparent so it doesn't paint over anything drawn behind it. Automatically
    // sized to the viewport's work area, which a main menu bar drawn earlier this frame (see
    // NewFrame()'s comment) already shrunk to make room for itself.
    return ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
}

void AppWindow::EndFrame()
{
    ImGui::Render();

    int displayWidth = 0;
    int displayHeight = 0;
    glfwGetFramebufferSize(m_Impl->Window, &displayWidth, &displayHeight);

    glViewport(0, 0, displayWidth, displayHeight);
    glClearColor(0.039f, 0.039f, 0.047f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(m_Impl->Window);
}
