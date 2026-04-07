#include <stdio.h>

#include <fstream>
#include <sstream>
#include <string>

// ImHTML
#include "imhtml.hpp"

// ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
// Include the loader FIRST
#include <glad/glad.h>

// Then GLFW
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

GLuint LoadTextureFromFile(const char *filename, int *width, int *height) {
  int channels;
  unsigned char *data = stbi_load(filename, width, height, &channels, 4);
  if (!data) {
    fprintf(stderr, "Failed to load image: %s\n", filename);
    return 0;
  }

  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);

  // Upload to GPU
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *width, *height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

  // Texture parameters
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  stbi_image_free(data);
  return tex;
}

static void glfwErrorCallback(int error, const char *description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static std::string loadFile(const char *path) {
  std::ifstream f(path);
  if (!f) {
    fprintf(stderr, "Failed to load file: %s\n", path);
    return "";
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static std::string replaceAll(std::string str, const std::string &from, const std::string &to) {
  size_t pos = 0;
  while ((pos = str.find(from, pos)) != std::string::npos) {
    str.replace(pos, from.size(), to);
    pos += to.size();
  }
  return str;
}

// Main code
int main(int, char **) {
  glfwSetErrorCallback(glfwErrorCallback);
  if (!glfwInit()) return 1;

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
  // GL ES 2.0 + GLSL 100 (WebGL 1.0)
  const char *glsl_version = "#version 100";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
  // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
  const char *glsl_version = "#version 300 es";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
  // GL 3.2 + GLSL 150
  const char *glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
  // GL 3.0 + GLSL 130
  const char *glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  // glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+
  // only glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // 3.0+ only
#endif

  // Create window with graphics context
  float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());  // Valid on GLFW 3.3+ only
  GLFWwindow *window =
      glfwCreateWindow((int)(1280 * main_scale), (int)(800 * main_scale), "ImHTML example", nullptr, nullptr);
  if (window == nullptr) return 1;
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);  // Enable vsync

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    fprintf(stderr, "Failed to initialize GLAD\n");
    return 1;
  }

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls
  ImGui::StyleColorsLight();

  ImHTML::Config *config = ImHTML::GetConfig();

  std::unordered_map<std::string, std::tuple<GLuint, ImHTML::ImageMeta>> imageCache;

  config->GetImageMeta = [&](const char *url, const char *baseurl) {
    if (imageCache.find(url) != imageCache.end()) {
      return std::get<1>(imageCache[url]);
    }

    int width, height;
    GLuint id = LoadTextureFromFile(url, &width, &height);
    imageCache[url] = std::make_tuple(id, ImHTML::ImageMeta{width, height});
    printf("[GetImageMeta] %s width: %d height: %d id: %d\n", url, width, height, id);
    return std::get<1>(imageCache[url]);
  };
  config->LoadImage = [&](const char *url, const char *baseurl) {
    if (imageCache.find(url) != imageCache.end()) {
      return std::get<0>(imageCache[url]);
    }
    int width, height;
    GLuint id = LoadTextureFromFile(url, &width, &height);
    imageCache[url] = std::make_tuple(id, ImHTML::ImageMeta{width, height});
    printf("[LoadImage] %s width: %d height: %d id: %d\n", url, width, height, id);
    return id;
  };
  config->GetImageTexture = [&](const char *url, const char *baseurl) {
    if (imageCache.find(url) != imageCache.end()) {
      GLuint id = std::get<0>(imageCache[url]);
      return (ImTextureID)id;
    }
    return (ImTextureID)0;
  };

  // Setup fonts
  ImFontAtlas *fonts = io.Fonts;
  fonts->AddFontDefault();
  ImFont *sansFont = fonts->AddFontFromFileTTF("fonts/NotoSans-Regular.ttf", 18.0f);
  ImFont *monoFont = fonts->AddFontFromFileTTF("fonts/JetBrainsMono-Regular.ttf", 18.0f);

  ImHTML::FontFamily mono = {.Regular = monoFont, .Bold = monoFont, .Italic = monoFont, .BoldItalic = monoFont};
  config->FontFamilies["monospace"] = mono;
  ImHTML::FontFamily sans = {.Regular = sansFont, .Bold = sansFont, .Italic = sansFont, .BoldItalic = sansFont};
  config->FontFamilies["sans-serif"] = sans;

  // Setup scaling
  ImGuiStyle &style = ImGui::GetStyle();
  style.ScaleAllSizes(main_scale);
  style.FontScaleDpi = main_scale;

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __EMSCRIPTEN__
  ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
  ImGui_ImplOpenGL3_Init(glsl_version);

  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  // Load example HTML files
  struct Example {
    const char *label;
    std::function<void(std::string)> render;
  };
  int clicks = 0;
  std::string hello_world_tmpl = loadFile("examples/hello_world.html");
  std::vector<Example> examples = {
      {"Hello, World!",
       [&clicks, &hello_world_tmpl](std::string id) {
         std::string html = replaceAll(hello_world_tmpl, "{clicks}", std::to_string(clicks));
         std::string clickedURL;
         if (ImHTML::Canvas((id + "_" + std::to_string(clicks)).c_str(), html.c_str(), 0.0f, &clickedURL)) clicks++;
       }},
      {"HTML Canvas",
       [html = loadFile("examples/html_canvas.html")](std::string id) { ImHTML::Canvas(id.c_str(), html.c_str()); }},
      {"Borders, Fonts & Gradients",
       [html = loadFile("examples/borders_and_stuff.html")](std::string id) {
         ImHTML::Canvas(id.c_str(), html.c_str());
       }},
      {"Custom Components",
       [html = loadFile("examples/custom_components.html")](std::string id) {
         ImHTML::Canvas(id.c_str(), html.c_str());
       }},
  };
  int selected = 0;

  ImHTML::RegisterCustomElement("custom-element", [](ImRect bounds, std::map<std::string, std::string> attributes) {
    ImGui::GetWindowDrawList()->AddRectFilled(bounds.Min, bounds.Max, IM_COL32(255, 0, 0, 100));

    ImGui::SetCursorScreenPos(bounds.Min);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bounds.GetWidth());
    ImGui::TextWrapped("Custom Element Here");
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", attributes["test"].c_str());
    }
    ImGui::PopTextWrapPos();
  });

  ImHTML::RegisterCustomElement("custom-button", [](ImRect bounds, std::map<std::string, std::string> attributes) {
    ImGui::SetCursorScreenPos(bounds.Min);
    ImGui::Button(attributes["text"].c_str(), bounds.GetSize());
    if (ImGui::IsItemHovered() && attributes.count("tooltip") > 0) {
      ImGui::SetTooltip("%s", attributes["tooltip"].c_str());
    }
  });

  // Main loop
#ifdef __EMSCRIPTEN__
  // For an Emscripten build we are disabling file-system access, so let's not
  // attempt to do a fopen() of the imgui.ini file. You may manually call
  // LoadIniSettingsFromMemory() to load settings from your own storage.
  io.IniFilename = nullptr;
  EMSCRIPTEN_MAINLOOP_BEGIN
#else
  while (!glfwWindowShouldClose(window))
#endif
  {
    glfwPollEvents();
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
      ImGui_ImplGlfw_Sleep(10);
      continue;
    }

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    {
      // Full-screen borderless window
      ImGuiIO &io = ImGui::GetIO();
      ImGui::SetNextWindowPos(ImVec2(0, 0));
      ImGui::SetNextWindowSize(io.DisplaySize);
      ImGui::Begin("ImHTML Examples",
                   nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

      // Left panel: example selector
      const float sidebar_width = 220.0f * main_scale;
      ImGui::BeginChild("##sidebar", ImVec2(sidebar_width, 0), ImGuiChildFlags_Borders);
      ImGui::SeparatorText("Examples");
      for (int i = 0; i < (int)examples.size(); i++) {
        if (ImGui::Selectable(examples[i].label, selected == i)) selected = i;
      }
      ImGui::EndChild();

      ImGui::SameLine();

      // Right panel: HTML canvas
      ImGui::BeginChild("##canvas", ImVec2(0, 0), ImGuiChildFlags_None);

      examples[selected].render(examples[selected].label);

      ImGui::EndChild();

      ImGui::End();
    }

    // Rendering
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(
        clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
  }
#ifdef __EMSCRIPTEN__
  EMSCRIPTEN_MAINLOOP_END;
#endif

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
