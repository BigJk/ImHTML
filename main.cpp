#include <stdio.h>

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

static void glfw_error_callback(int error, const char *description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Main code
int main(int, char **) {
  glfwSetErrorCallback(glfw_error_callback);
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
  ImFontAtlas * fonts = io.Fonts;
  fonts->AddFontDefault();
  ImFont* sansFont = fonts->AddFontFromFileTTF("fonts/NotoSans-Regular.ttf", 18.0f);
  ImFont* monoFont = fonts->AddFontFromFileTTF("fonts/JetBrainsMono-Regular.ttf", 18.0f);

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

  // Our state
  bool show_demo_window = true;
  bool show_another_window = false;
  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

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
      static int clicks = 0;

      ImGui::Begin("HTML Canvas");
      ImHTML::Canvas("my_canvas_xyz",
                     R"(
    <html>
        <head>
        <title>ImHTML Example</title>
        <style>
            p, h1, h2, h3, h4, h5, h6 {
              margin: 0;
            }
        </style>
        </head>
        <body>
        <h1>ImHTML Example</h1>
        <p style="line-height: 1.2;">Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua. At vero eos et accusam et justo duo dolores et ea rebum. Stet clita kasd gubergren, no sea takimata sanctus est Lorem ipsum dolor sit amet. Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua. At vero eos et accusam et justo duo dolores et ea rebum. Stet clita kasd gubergren, no sea takimata sanctus est Lorem ipsum dolor sit amet.</p>
        <div style="border: 1px solid white; background-color: green; padding: 30px;">Box Test</div>
        <a href="https://github.com/">GitHub</a>
        </body>
    </html>
    )");
      ImGui::End();

      ImGui::Begin("Advanced borders, fonts & stuff");
      ImHTML::Canvas("borders_and_stuff",
                     R"(
<!DOCTYPE html>
<html>
<head>
    <style>
        /* 1. Root Background test */
        body {
            background-color: #1e1e24; /* A dark theme background */
            color: #ffffff;
            font-family: sans-serif;
            font-size: 18px;
            margin: 20px;
        }

        h2 {
            border-bottom: 2px solid #555;
            padding-bottom: 5px;
            color: #ffd700;
        }

        /* 2. Wonky Borders test */
        .miter-box {
            width: 150px;
            height: 100px;
            background-color: #333;
            /* Uneven colors and widths to force the Quad miter drawing */
            border-top: 20px solid #ff595e;
            border-right: 20px solid #ffca3a;
            border-bottom: 20px solid #8ac926;
            border-left: 20px solid #1982c4;
            margin-bottom: 20px;
        }

        /* 3. Per-corner Border Radius test */
        .radius-box {
            width: 250px;
            height: 100px;
            background-color: #6a4c93;
            /* Top-Left: 40px, Top-Right: 0px, Bottom-Right: 20px, Bottom-Left: 60px */
            border-radius: 40px 0px 20px 60px;
            padding: 15px;
            box-sizing: border-box;
            margin-bottom: 20px;
        }

        /* 4. Rounded Image test */
        .rounded-image {
            width: 120px;
            height: 120px;
            border-radius: 30px; /* Applies to the ImGui AddImage logic */
            margin-bottom: 20px;
            background-color: #444; /* Fallback if img loading isn't hooked up */
        }

        /* 5. List Markers test */
        ul.disc { list-style-type: disc; }
        ul.circle { list-style-type: circle; }
        ul.square { list-style-type: square; }
        
        li { margin-bottom: 5px; }

        /* 7. Gradient Fill test */
         .gradient-linear {
            width: 250px;
            height: 100px;
            margin: 10px;
            background-image: linear-gradient(
              45deg,
              hsl(240deg 100% 20%) 0%,
              hsl(281deg 100% 21%) 0%,
              hsl(304deg 100% 23%) 2%,
              hsl(319deg 100% 30%) 7%,
              hsl(329deg 100% 36%) 18%,
              hsl(336deg 100% 41%) 41%,
              hsl(346deg 83% 51%) 68%,
              hsl(3deg 95% 61%) 83%,
              hsl(17deg 100% 59%) 92%,
              hsl(30deg 100% 55%) 97%,
              hsl(40deg 100% 50%) 99%,
              hsl(48deg 100% 50%) 100%,
              hsl(55deg 100% 50%) 100%
            );
        }

        .gradient-radial {
            width: 250px;
            height: 100px;
            margin: 10px;
            background-image: radial-gradient(
              circle at 30% 30%,
              hsl(0deg 100% 50%) 0%,
              hsl(60deg 100% 50%) 25%,
              hsl(120deg 100% 50%) 50%,
              hsl(180deg 100% 50%) 75%,
              hsl(240deg 100% 50%) 100%
            );
        }

        .gradient-conic {
            width: 250px;
            height: 100px;
            margin: 10px;
            border-radius: 20px 0px 20px 0px;
            background-image: conic-gradient(
              from 90deg at 50% 50%,
              hsl(0deg 100% 50%) 0%,
              hsl(60deg 100% 50%) 20%,
              hsl(120deg 100% 50%) 40%,
              hsl(180deg 100% 50%) 60%,
              hsl(240deg 100% 50%) 80%,
              hsl(300deg 100% 50%) 100%
            );
        }
    </style>
</head>
<body>

    <h2>1. Root Background</h2>
    <p>Notice how the dark #1e1e24 background fills the entire ImGui window space natively, not just the bounding box of the text.</p>

    <h2>2. Individual/Mitered Borders</h2>
    <p>The corners below should join perfectly at 45-degree angles.</p>
    <div class="miter-box"></div>

    <h2>3. Per-Corner Border Radius</h2>
    <p>40px top-left, 0px top-right, 20px bottom-right, 60px bottom-left.</p>
    <div class="radius-box">
        Hello World!
    </div>

    <h2>4. Border Radius on Images</h2>
    <p>If your ImGui image loader is hooked up, this image will have 30px rounded corners.</p>
    <!-- Placeholder image, requires config.GetImageTexture to be set up to fetch! -->
    <img src="./images/example.jpg" class="rounded-image" />

    <h2>5. List Marker Styles</h2>
    <ul class="disc">
        <li>Disc bullet (default filled circle)</li>
    </ul>
    <ul class="circle">
        <li>Circle bullet (unfilled outline)</li>
    </ul>
    <ul class="square">
        <li>Square bullet (filled rectangle)</li>
    </ul>

    <h2>6. Font Families</h2>
    <p>The main font of this canvas should appear as a "sans-serif" font, different from the default ImGui font.</p>
    <p style="font-family: monospace;">While this paragraph should appear as a "monospace" font.
    <pre>
        Line 1: The quick brown fox jumps over the lazy dog.
        Line 2: 0123456789 {} !@#$%^&*()_+-=
        Line 3: `~ ImHTML Font Test
    </pre>
    </p>

    <h2>7. Gradient Fills</h2>
    <p>As well as solid colors, ImHTML supports CSS gradients! Below are examples of linear, radial and conic gradients.</p>
    <div class="gradient-linear"></div>
    <div class="gradient-radial"></div>
    <p>And even with rounded corners!</p>
    <div class="gradient-conic"></div>
</body>
</html>
    )");
      ImGui::End();

      ImGui::Begin("Custom Components");
      ImHTML::Canvas("custom_components",
                     R"(
                     <div style="display: flex;">
                        <div>
                        Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua. At vero eos et accusam et justo duo dolores et ea rebum. Stet clita kasd gubergren, no sea takimata sanctus est Lorem ipsum dolor sit amet.
                        </div>
                        <div style="border: 1px solid green; width: 100px; height: 100px; padding: 15px; flex-shrink: 0;">
                            <custom-element test="tooltip whatever"></custom-element>
                        </div>
                        <div style="border: 1px solid green; height: 100px; width: 100px; padding: 15px; flex-shrink: 0;">
                            <custom-button text="Click me" tooltip="Tooltip"></custom-button>
                        </div>
                      </div>
                      )");
      ImGui::End();

      ImGui::Begin("Hello, world!");

      std::string clickedURL = "";
      if (ImHTML::Canvas(("my_canvas_" + std::to_string(clicks)).c_str(),
                         (R"(
<html>
   <head>
      <title>ImHTML Example</title>
      <style>
         p {
         margin: 0;
         }
         h1, h2, h3, h4, h5, h6 {
         margin-top: 15px;
         margin-bottom: 5px;
         padding-bottom: 5px;
         border-bottom: 1px solid #ccc;
         }
         a:hover {
         color: red;
         }
      </style>
   </head>
   <body>
      <h1>ImHTML Example)" +
                          std::to_string(clicks) + R"(</h1>
      <div style="display: flex;">
         <div style="margin-right: 15px;">
           
            <h2>Text</h2>
            <p style="line-height: 1.2;">Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua. At vero eos et accusam et justo duo dolores et ea rebum. Stet clita kasd gubergren, no sea takimata sanctus est Lorem ipsum dolor sit amet. Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua. At vero eos et accusam et justo duo dolores et ea rebum. Stet clita kasd gubergren, no sea takimata sanctus est Lorem ipsum dolor sit amet.</p>
            <h2>Background Color & Border</h2>
            <div style="border: 3px solid red; background-color: green; padding: 30px;">Box Test</div>
            <h2>Links</h2>
            <a href="https://github.com/">GitHub</a>
            <h2>Flexbox</h2>
            <div style="display: flex; flex-direction: row;">
               <div style="flex: 1; background-color: red; padding: 10px;">1</div>
               <div style="flex: 1; background-color: green; padding: 10px;">2</div>
               <div style="flex: 1; background-color: blue; padding: 10px;">3</div>
            </div>
         </div>
         <div>
            <h2>Image</h2>
            <img src="./images/example.jpg" style="width: 100%" />
            <h2>Lists</h2>
            <ul>
               <li>Item 1</li>
               <li>Item 2</li>
               <li>Item 3</li>
               <ul>
                  <li>Subitem 1</li>
                  <li>Subitem 2</li>
                  <li>Subitem 3</li>
               </ul>
            </ul>
         </div>
      </div>
   </body>
</html>
      )")
                             .c_str(),
                         0.0f,
                         &clickedURL)) {
        clicks++;
      }

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