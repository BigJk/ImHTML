
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui.h"
#include "imgui_internal.h"
#include "litehtml.h"
#include "litehtml/types.h"

#include "imhtml.hpp"


namespace ImHTML {

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
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 center = ImGui::GetCursorScreenPos() + ImVec2(marker.pos.x + marker.pos.width / 2.0f, marker.pos.y + marker.pos.height / 2.0f);
    float radius = marker.pos.width / 2.0f;
    ImU32 color = IM_COL32(marker.color.red, marker.color.green, marker.color.blue, marker.color.alpha);

    switch (marker.marker_type) {
      case litehtml::list_style_type_circle:
        draw_list->AddCircle(center, radius, color, 0, 1.5f);
        break;
      case litehtml::list_style_type_disc:
        draw_list->AddCircleFilled(center, radius, color);
        break;
      case litehtml::list_style_type_square: {
        ImVec2 p_min = ImGui::GetCursorScreenPos() + ImVec2(marker.pos.x, marker.pos.y);
        ImVec2 p_max = p_min + ImVec2(marker.pos.width, marker.pos.height);
        draw_list->AddRectFilled(p_min, p_max, color);
        break;
      }
      default:
        draw_list->AddCircleFilled(center, radius, color);
        break;
    }

    push_bottom_right(ImVec2(marker.pos.x + marker.pos.width, marker.pos.y + marker.pos.height));
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
      
      litehtml::position bg_box = paint.border_box;
      litehtml::position clip_box = paint.clip_box;
      
      ImVec2 p_min = screen_pos + ImVec2(bg_box.x, bg_box.y);
      ImVec2 p_max = screen_pos + ImVec2(bg_box.x + bg_box.width, bg_box.y + bg_box.height);

      float tl = paint.border_radius.top_left_x;
      float tr = paint.border_radius.top_right_x;
      float br = paint.border_radius.bottom_right_x;
      float bl = paint.border_radius.bottom_left_x;
      ImU32 bg_color = IM_COL32(paint.color.red, paint.color.green, paint.color.blue, paint.color.alpha);

      if (paint.color.alpha > 0) {
        if (tl == tr && tr == br && br == bl) {
          ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, bg_color, tl);
        } else {
          auto* draw_list = ImGui::GetWindowDrawList();
          draw_list->PathClear();
          if (tl > 0.0f) draw_list->PathArcTo(ImVec2(p_min.x + tl, p_min.y + tl), tl, IM_PI, IM_PI * 1.5f);
          else draw_list->PathLineTo(ImVec2(p_min.x, p_min.y));

          if (tr > 0.0f) draw_list->PathArcTo(ImVec2(p_max.x - tr, p_min.y + tr), tr, IM_PI * 1.5f, IM_PI * 2.0f);
          else draw_list->PathLineTo(ImVec2(p_max.x, p_min.y));

          if (br > 0.0f) draw_list->PathArcTo(ImVec2(p_max.x - br, p_max.y - br), br, 0.0f, IM_PI * 0.5f);
          else draw_list->PathLineTo(ImVec2(p_max.x, p_max.y));

          if (bl > 0.0f) draw_list->PathArcTo(ImVec2(p_min.x + bl, p_max.y - bl), bl, IM_PI * 0.5f, IM_PI);
          else draw_list->PathLineTo(ImVec2(p_min.x, p_max.y));

          draw_list->PathFillConvex(bg_color);
        }
      }

      if (!paint.image.empty() && config.GetImageTexture) {
        ImTextureID texture = config.GetImageTexture(paint.image.c_str(), paint.baseurl.c_str());
        ImVec2 img_p_min = screen_pos + ImVec2(clip_box.x, clip_box.y);
        ImVec2 img_p_max = screen_pos + ImVec2(clip_box.x + clip_box.width, clip_box.y + clip_box.height);
        
        float radius = std::max({tl, tr, bl, br});
        if (radius > 0.0f) {
          ImGui::GetWindowDrawList()->AddImageRounded(
              texture,
              img_p_min, img_p_max,
              ImVec2(0, 0), ImVec2(1, 1),
              IM_COL32_WHITE,
              radius);
        } else {
          ImGui::GetWindowDrawList()->AddImage(
              texture,
              img_p_min, img_p_max);
        }
      }

      push_bottom_right(ImVec2(bg_box.x + bg_box.width, bg_box.y + bg_box.height));
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

    // Check if all sides and colors are equal
    if (borders.top.width == borders.right.width && borders.top.width == borders.bottom.width &&
        borders.top.width == borders.left.width && borders.top.color == borders.right.color &&
        borders.top.color == borders.bottom.color && borders.top.color == borders.left.color) {
        
      float w = borders.top.width;
      if (w > 0) {
        // ImGui path strokes are centered. We must offset the path inward by half the width 
        // to conform to the CSS Box Model (borders grow inwards from the bounding box).
        float half_w = w * 0.5f;
        ImVec2 p_min = top_left + ImVec2(half_w, half_w);
        ImVec2 p_max = bottom_right - ImVec2(half_w, half_w);

        // We also must reduce the border radius by half the width so the outer edge matches CSS.
        float tl = std::max(0.0f, (float)borders.radius.top_left_x - half_w);
        float tr = std::max(0.0f, (float)borders.radius.top_right_x - half_w);
        float br = std::max(0.0f, (float)borders.radius.bottom_right_x - half_w);
        float bl = std::max(0.0f, (float)borders.radius.bottom_left_x - half_w);

        ImU32 color = IM_COL32(borders.top.color.red, borders.top.color.green, borders.top.color.blue, borders.top.color.alpha);

        if (tl == tr && tr == br && br == bl) {
          draw_list->AddRect(p_min, p_max, color, tl, 0, w);
        } else {
          draw_list->PathClear();
          if (tl > 0.0f) draw_list->PathArcTo(ImVec2(p_min.x + tl, p_min.y + tl), tl, IM_PI, IM_PI * 1.5f);
          else draw_list->PathLineTo(ImVec2(p_min.x, p_min.y));

          if (tr > 0.0f) draw_list->PathArcTo(ImVec2(p_max.x - tr, p_min.y + tr), tr, IM_PI * 1.5f, IM_PI * 2.0f);
          else draw_list->PathLineTo(ImVec2(p_max.x, p_min.y));

          if (br > 0.0f) draw_list->PathArcTo(ImVec2(p_max.x - br, p_max.y - br), br, 0.0f, IM_PI * 0.5f);
          else draw_list->PathLineTo(ImVec2(p_max.x, p_max.y));

          if (bl > 0.0f) draw_list->PathArcTo(ImVec2(p_min.x + bl, p_max.y - bl), bl, IM_PI * 0.5f, IM_PI);
          else draw_list->PathLineTo(ImVec2(p_min.x, p_max.y));

          draw_list->PathStroke(color, ImDrawFlags_Closed, w);
        }
      }
    } else {
      // The Non-Uniform Path (Mitered Borders via Quads)
      auto color32 = [](const litehtml::web_color &c) {
        return IM_COL32(c.red, c.green, c.blue, c.alpha);
      };

      // Top border
      if (borders.top.width > 0) {
        draw_list->AddQuadFilled(
            top_left, ImVec2(bottom_right.x, top_left.y),
            ImVec2(bottom_right.x - borders.right.width, top_left.y + borders.top.width),
            ImVec2(top_left.x + borders.left.width, top_left.y + borders.top.width),
            color32(borders.top.color));
      }

      // Bottom border
      if (borders.bottom.width > 0) {
        draw_list->AddQuadFilled(
            ImVec2(top_left.x + borders.left.width, bottom_right.y - borders.bottom.width),
            ImVec2(bottom_right.x - borders.right.width, bottom_right.y - borders.bottom.width),
            bottom_right, ImVec2(top_left.x, bottom_right.y),
            color32(borders.bottom.color));
      }

      // Left border
      if (borders.left.width > 0) {
        draw_list->AddQuadFilled(
            top_left, ImVec2(top_left.x + borders.left.width, top_left.y + borders.top.width),
            ImVec2(top_left.x + borders.left.width, bottom_right.y - borders.bottom.width),
            ImVec2(top_left.x, bottom_right.y),
            color32(borders.left.color));
      }

      // Right border
      if (borders.right.width > 0) {
        draw_list->AddQuadFilled(
            ImVec2(bottom_right.x - borders.right.width, top_left.y + borders.top.width),
            ImVec2(bottom_right.x, top_left.y), bottom_right,
            ImVec2(bottom_right.x - borders.right.width, bottom_right.y - borders.bottom.width),
            color32(borders.right.color));
      }
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
    std::shared_ptr<BrowserContainer> container;
    std::shared_ptr<litehtml::document> doc;
    std::string html;
    long long lastActiveTime;
  };

  static std::unordered_map<std::string, state> states = {};

  if (states.find(id) == states.end()) {
    auto container = std::make_shared<BrowserContainer>(width);
    container->set_config(getCurrentConfig());
    container->reset();
    states[id] = state{
        .container = container,
        .doc = litehtml::document::createFromString(html, container.get()),
        .html = html,
        .lastActiveTime = std::chrono::high_resolution_clock::now().time_since_epoch().count(),
    };
  }

  auto &state = states[id];

  if (state.html != html) {
    state.doc = litehtml::document::createFromString(html, state.container.get());
    state.html = html;
  }

  state.lastActiveTime = std::chrono::high_resolution_clock::now().time_since_epoch().count();

  state.container->set_config(getCurrentConfig());
  state.container->reset();

  int render_width = width > 0 ? (int)width : (int)ImGui::GetContentRegionAvail().x;
  state.doc->render(render_width);

  litehtml::position clip(0, 0, render_width, std::max((int)state.doc->height(), (int)ImGui::GetContentRegionAvail().y));
  state.doc->draw(0, 0, 0, &clip);

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
