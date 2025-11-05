#include "imhtml.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "litehtml.h"
#include "litehtml/types.h"

namespace ImHTML {

std::string DefaultFileLoader(const char *url, const char *baseurl) {
  if (url == nullptr || strlen(url) == 0) {
    return "";
  }

  std::ifstream file(url);
  if (!file.is_open()) {
    IMHTML_PRINTF("[ImHTML] Failed to open file: %s\n", url);
    return "";
  }

  std::string content((std::istreambuf_iterator<char>(file)), (std::istreambuf_iterator<char>()));
  return content;
}

namespace {

Config config = Config{
    .BaseFontSize = 16,
    .LoadCSS = DefaultFileLoader,
};

std::vector<Config> configStack;
std::unordered_map<std::string, CustomElementDrawFunction> customElements;

Config getCurrentConfig() {
  if (configStack.empty()) {
    return config;
  }
  return configStack.back();
}

ImFont *getFont(FontStyle fontStyle) {
  Config config = getCurrentConfig();
  switch (fontStyle) {
    case FontStyle::Regular:
      return config.FontRegular ? config.FontRegular : ImGui::GetFont();
    case FontStyle::Bold:
      return config.FontBold ? config.FontBold : ImGui::GetFont();
    case FontStyle::Italic:
      return config.FontItalic ? config.FontItalic : ImGui::GetFont();
    case FontStyle::BoldItalic:
      return config.FontBoldItalic ? config.FontBoldItalic : ImGui::GetFont();
    default:
      return config.FontRegular ? config.FontRegular : ImGui::GetFont();
  }
}

}  // namespace

void CustomElement::draw_background(litehtml::uint_ptr hdc, int x, int y, const litehtml::position *clip,
                                    const std::shared_ptr<litehtml::render_item> &ri) {
  litehtml::position placement = this->parent()->get_placement();

  if (customElements.find(this->tag) != customElements.end()) {
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    customElements[this->tag](ImRect(cursor + ImVec2(x, y), cursor + ImVec2(x + placement.width, y + placement.height)),
                              this->attributes);
    ImGui::SetCursorScreenPos(cursor);
  }
}

class BrowserContainer : public litehtml::document_container {
 private:
  ImVec2 bottomRight = ImVec2(0, 0);
  std::string title = "Browser";
  std::string loadUrl = "";
  std::string currentUrl = "";
  std::vector<std::string> history = {};
  float width;
  Config config;

 public:
  BrowserContainer(float width) : width(width) {}

  void reset() { bottomRight = ImVec2(0, 0); }
  ImVec2 get_bottom_right() { return bottomRight; }
  void push_bottom_right(ImVec2 point) {
    bottomRight.x = std::max(bottomRight.x, point.x);
    bottomRight.y = std::max(bottomRight.y, point.y);
  }
  std::string get_title() { return title; }
  std::string pop_load_url() {
    if (loadUrl.empty()) {
      return "";
    }

    auto url = loadUrl;
    loadUrl = "";
    return url;
  }
  void go_back() {
    if (!history.empty()) {
      loadUrl = history.back();
      history.pop_back();
    }
  }
  bool can_go_back() { return !history.empty(); }
  void set_current_url(std::string url) { currentUrl = url; }
  std::string get_current_url() { return currentUrl; }
  void refresh() { loadUrl = currentUrl; }
  void set_config(Config config) { this->config = config; }

  //
  // Font functions
  //

  virtual litehtml::uint_ptr create_font(const char *faceName, int size, int weight, litehtml::font_style style,
                                         unsigned int decoration, litehtml::font_metrics *fm) override {
    bool bold = weight > 400;
    bool italic = style == litehtml::font_style_italic;

    FontStyle fontStyle = FontStyle::Regular;
    if (bold) {
      fontStyle = FontStyle::Bold;
    }
    if (italic) {
      fontStyle = FontStyle::Italic;
    }
    if (bold && italic) {
      fontStyle = FontStyle::BoldItalic;
    }

    ImGui::PushFont(getFont(fontStyle), size);
    fm->height = ImGui::GetTextLineHeight();
    ImGui::PopFont();

    litehtml::uint_ptr hFont = (int)fontStyle << 16 | size;

    return hFont;
  }

  virtual void delete_font(litehtml::uint_ptr hFont) override {
    // do nothing for now
  }

  virtual int text_width(const char *text, litehtml::uint_ptr hFont) override {
    int fontStyle = hFont >> 16;
    int fontSize = hFont & 0xffff;

    ImGui::PushFont(getFont((FontStyle)fontStyle), fontSize);
    auto size = ImGui::CalcTextSize(text);
    ImGui::PopFont();
    return size.x;
  }

  virtual void draw_text(litehtml::uint_ptr hdc, const char *text, litehtml::uint_ptr hFont, litehtml::web_color color,
                         const litehtml::position &pos) override {
    int fontStyle = hFont >> 16;
    int fontSize = hFont & 0xffff;

    ImGui::PushFont(getFont((FontStyle)fontStyle), fontSize);
    ImGui::GetWindowDrawList()->AddText(ImGui::GetCursorScreenPos() + ImVec2(pos.x, pos.y),
                                        IM_COL32(color.red, color.green, color.blue, color.alpha),
                                        text);
    auto size = ImGui::CalcTextSize(text);
    ImGui::PopFont();
    push_bottom_right(ImVec2(pos.x + size.x, pos.y + size.y));
  }

  //
  // Measurement and defaults
  //

  virtual int pt_to_px(int pt) const override { return pt; }
  virtual int get_default_font_size() const override { return config.BaseFontSize; }
  virtual const char *get_default_font_name() const override { return "Default"; }

  //
  // Drawing functions
  //

  virtual void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker &marker) override {
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImGui::GetCursorScreenPos() + ImVec2(marker.pos.x + 4, marker.pos.y + 4),
        2,
        IM_COL32(marker.color.red, marker.color.green, marker.color.blue, marker.color.alpha));
    push_bottom_right(ImVec2(marker.pos.x + 8, marker.pos.y + 8));
  }

  virtual void load_image(const char *src, const char *baseurl, bool redraw_on_ready) override {
    if (!config.LoadImage) {
      return;
    }

    config.LoadImage(src, baseurl);
  }

  virtual void get_image_size(const char *src, const char *baseurl, litehtml::size &sz) override {
    if (!config.GetImageMeta) {
      return;
    }

    auto imageMeta = config.GetImageMeta(src, baseurl);
    sz.width = imageMeta.width;
    sz.height = imageMeta.height;
  }

  virtual void draw_background(litehtml::uint_ptr hdc, const std::vector<litehtml::background_paint> &bg) override {
    for (auto &paint : bg) {
      ImVec2 screen_pos = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddRectFilled(
          screen_pos + ImVec2(paint.border_box.x, paint.border_box.y),
          screen_pos +
              ImVec2(paint.border_box.x + paint.border_box.width, paint.border_box.y + paint.border_box.height),
          IM_COL32(paint.color.red, paint.color.green, paint.color.blue, paint.color.alpha));

      if (!paint.image.empty() && config.GetImageTexture) {
        ImTextureID texture = config.GetImageTexture(paint.image.c_str(), paint.baseurl.c_str());
        ImGui::GetWindowDrawList()->AddImage(
            texture,
            screen_pos + ImVec2(paint.clip_box.x, paint.clip_box.y),
            screen_pos + ImVec2(paint.clip_box.x + paint.clip_box.width, paint.clip_box.y + paint.clip_box.height));
      }

      push_bottom_right(
          ImVec2(paint.border_box.x + paint.border_box.width, paint.border_box.y + paint.border_box.height));
    }
  }

  virtual void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders &borders,
                            const litehtml::position &draw_pos, bool root) override {
    ImVec2 base_pos = ImGui::GetCursorScreenPos();
    ImVec2 top_left = base_pos + ImVec2(draw_pos.x, draw_pos.y);
    ImVec2 top_right = base_pos + ImVec2(draw_pos.x + draw_pos.width, draw_pos.y);
    ImVec2 bottom_right = base_pos + ImVec2(draw_pos.x + draw_pos.width, draw_pos.y + draw_pos.height);
    ImVec2 bottom_left = base_pos + ImVec2(draw_pos.x, draw_pos.y + draw_pos.height);

    auto *draw_list = ImGui::GetWindowDrawList();

    // Top border
    if (borders.top.width > 0) {
      draw_list->AddLine(
          top_left,
          top_right,
          IM_COL32(borders.top.color.red, borders.top.color.green, borders.top.color.blue, borders.top.color.alpha),
          borders.top.width);
    }

    // Right border
    if (borders.right.width > 0) {
      draw_list->AddLine(
          top_right,
          bottom_right,
          IM_COL32(
              borders.right.color.red, borders.right.color.green, borders.right.color.blue, borders.right.color.alpha),
          borders.right.width);
    }

    // Bottom border
    if (borders.bottom.width > 0) {
      draw_list->AddLine(bottom_right,
                         bottom_left,
                         IM_COL32(borders.bottom.color.red,
                                  borders.bottom.color.green,
                                  borders.bottom.color.blue,
                                  borders.bottom.color.alpha),
                         borders.bottom.width);
    }

    // Left border
    if (borders.left.width > 0) {
      draw_list->AddLine(
          bottom_left,
          top_left,
          IM_COL32(borders.left.color.red, borders.left.color.green, borders.left.color.blue, borders.left.color.alpha),
          borders.left.width);
    }

    push_bottom_right(ImVec2(draw_pos.x + draw_pos.width, draw_pos.y + draw_pos.height));
  }

  //
  // Document related functions
  //

  virtual void set_caption(const char *caption) override { title = caption; }
  virtual void set_base_url(const char *base_url) override {}
  virtual void link(const std::shared_ptr<litehtml::document> &doc, const litehtml::element::ptr &el) override {}
  virtual void on_anchor_click(const char *url, const litehtml::element::ptr &el) override {
    history.push_back(currentUrl);
    loadUrl = url;
  }
  virtual void set_cursor(const char *cursor) override {
    if (std::string(cursor) == "pointer" && ImGui::IsWindowHovered()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
  }
  virtual void transform_text(std::string &text, litehtml::text_transform tt) override {}
  virtual void import_css(std::string &text, const std::string &url, std::string &baseurl) override {
    if (!config.LoadCSS) {
      return;
    }
    text = config.LoadCSS(url.c_str(), baseurl.c_str());
  }

  //
  // Clipping functions
  //

  virtual void set_clip(const litehtml::position &pos, const litehtml::border_radiuses &bdr_radius) override {}
  virtual void del_clip() override {}

  //
  // Layout functions
  //

  virtual void get_client_rect(litehtml::position &client) const override {
    client.x = 0;
    client.y = 0;
    client.width = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    client.height = ImGui::GetContentRegionAvail().y;
  }

  virtual litehtml::element::ptr create_element(const char *tag_name, const litehtml::string_map &attributes,
                                                const std::shared_ptr<litehtml::document> &doc) override {
    if (customElements.find(tag_name) != customElements.end()) {
      return std::make_shared<CustomElement>(doc, tag_name, attributes);
    }

    return nullptr;
  }

  virtual void get_media_features(litehtml::media_features &media) const override {
    media.color = 8;
    media.resolution = 96;
    media.width = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    media.height = ImGui::GetContentRegionAvail().y;
    media.device_width = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    media.device_height = ImGui::GetContentRegionAvail().y;
    media.type = litehtml::media_type_screen;
  }

  virtual void get_language(litehtml::string &language, litehtml::string &culture) const override {
    language = "en";
    culture = "US";
  }
};

Config *GetConfig() { return &config; }
void SetConfig(Config config) { config = config; }
void PushConfig(Config config) { configStack.push_back(config); }
void PopConfig() {
  assert(!configStack.empty());
  configStack.pop_back();
}

void RegisterCustomElement(const char *tagName, CustomElementDrawFunction draw) { customElements[tagName] = draw; }

void UnregisterCustomElement(const char *tagName) { customElements.erase(tagName); }

bool Canvas(const char *id, const char *html, float width, std::string *clickedURL) {
  struct state {
    std::shared_ptr<litehtml::document> doc;
    std::shared_ptr<BrowserContainer> container;
    std::string html;
    long long lastActiveTime;
  };

  static std::unordered_map<std::string, state> states = {};

  if (states.find(id) == states.end()) {
    auto container = std::make_shared<BrowserContainer>(width);
    container->set_config(getCurrentConfig());
    container->reset();
    states[id] = state{
        .doc = litehtml::document::createFromString(html, container.get()),
        .container = container,
        .html = html,
        .lastActiveTime = std::chrono::high_resolution_clock::now().time_since_epoch().count(),
    };
  }

  auto &state = states[id];

  if (state.html != html) {
    state.doc = state.doc->createFromString(html, state.container.get());
    state.html = html;
  }

  state.lastActiveTime = std::chrono::high_resolution_clock::now().time_since_epoch().count();

  state.container->set_config(getCurrentConfig());
  state.container->reset();

  state.doc->render(width > 0 ? width : ImGui::GetContentRegionAvail().x);
  state.doc->draw(0, 0, 0, nullptr);

  auto x = ImGui::GetMousePos().x - ImGui::GetCursorScreenPos().x;
  auto y = ImGui::GetMousePos().y - ImGui::GetCursorScreenPos().y;

  litehtml::position::vector pos;
  if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    state.doc->on_lbutton_down(x, y, x, y, pos);
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    state.doc->on_lbutton_up(x, y, x, y, pos);
  }
  state.doc->on_mouse_over(x, y, x, y, pos);

  const ImRect bb(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + state.container->get_bottom_right());
  ImGui::ItemSize(bb.GetSize());
  ImGui::ItemAdd(bb, ImGui::GetID(id));

  if (std::string url = state.container->pop_load_url(); !url.empty()) {
    if (clickedURL) {
      *clickedURL = url;
    }
    return true;
  }

  // Cleanup all inactive states with lastActiveTime > 1 seconds
  auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  for (auto it = states.begin(); it != states.end();) {
    if (it->first != id && now - it->second.lastActiveTime > 1000000000) {
      IMHTML_PRINTF("[ImHTML] Erased state for id=%s\n", it->first.c_str());

      // We have to destruct in this order, otherwise we get a segfault
      it->second.doc.reset();
      it->second.container.reset();

      it = states.erase(it);
    } else {
      ++it;
    }
  }

  return false;
}
};  // namespace ImHTML
