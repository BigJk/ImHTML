#pragma once

#include <functional>
#include <string>

#include "litehtml.h"
#include "litehtml/html_tag.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"

#ifdef IMHTML_DEBUG_PRINTF
#define IMHTML_PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define IMHTML_PRINTF(fmt, ...)
#endif

namespace ImHTML {
/**
 * Font styles (only used internally)
 */
enum class FontStyle : unsigned char { Regular, Bold, Italic, BoldItalic };

/**
 * Meta data for an image
 */
struct ImageMeta {
  int width;
  int height;
};

/**
 * Configuration for the HTML renderer
 */
struct Config {
  float BaseFontSize = 16.0f;
  ImFont *FontRegular = nullptr;
  ImFont *FontBold = nullptr;
  ImFont *FontItalic = nullptr;
  ImFont *FontBoldItalic = nullptr;
  std::function<void(const char *src, const char *baseurl)> LoadImage;
  std::function<ImageMeta(const char *src, const char *baseurl)> GetImageMeta;
  std::function<ImTextureID(const char *src, const char *baseurl)> GetImageTexture;
  std::function<std::string(const char *url, const char *baseurl)> LoadCSS;
};

/**
 * A custom element draw function
 *
 * @param bounds The available bounds of the parent element. This is the usable space for the custom element.
 * @param attributes The attributes of the element
 */
typedef std::function<void(ImRect bounds, std::map<std::string, std::string> attributes)> CustomElementDrawFunction;

/**
 * Custom element
 */
class CustomElement : public litehtml::html_tag {
 private:
  std::string tag = "";
  std::map<std::string, std::string> attributes = {};

 public:
  CustomElement(const std::shared_ptr<litehtml::document> &doc, const std::string &tag,
                std::map<std::string, std::string> attributes)
      : litehtml::html_tag(doc), tag(tag), attributes(attributes) {}

  void draw_background(litehtml::uint_ptr hdc, int x, int y, const litehtml::position *clip,
                       const std::shared_ptr<litehtml::render_item> &ri) override;
};

/**
 * Default file loader for loading CSS files
 *
 * @param url Expects a relative local path to the CSS file
 * @param baseurl The base URL of the CSS file (not used)
 * @return The content of the CSS file
 */
std::string DefaultFileLoader(const char *url, const char *baseurl);

/**
 * Get the current configuration
 *
 * @return The current configuration
 */
Config *GetConfig();

/**
 * Set the configuration
 *
 * @param config The new configuration
 */
void SetConfig(Config config);

/**
 * Push the configuration
 *
 * @param config The new configuration
 */
void PushConfig(Config config);

/**
 * Pop the configuration
 */
void PopConfig();

/**
 * Register a custom element. The draw function will be called with the position and attributes of the element.
 *
 * @param tagName The tag name of the custom element (e.g. <custom arg="value"></custom>)
 * @param draw The draw function
 */
void RegisterCustomElement(const char *tagName, CustomElementDrawFunction draw);

/**
 * Unregister a custom element.
 *
 * @param tagName The tag name of the custom element (e.g. <custom arg="value"></custom>)
 */
void UnregisterCustomElement(const char *tagName);

/**
 * Render the HTML
 *
 * @param id The ID of the canvas
 * @param html The HTML to render
 * @param width The width of the canvas (0.0f for using available space)
 * @param clickedURL The URL that was clicked (if any)
 * @return True if any link was clicked, false otherwise
 */
bool Canvas(const char *id, const char *html, float width = 0.0f, std::string *clickedURL = nullptr);
};  // namespace ImHTML