
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui.h"
#include "imgui_internal.h"
#include "imhtml.hpp"
#include "litehtml.h"
#include "litehtml/render_item.h"
#include "litehtml/types.h"

namespace ImHTML {

/**
 * Custom element
 */
class CustomElement : public litehtml::html_tag {
 private:
  std::string tag = "";
  std::map<std::string, std::string> attributes = {};

 public:
  CustomElement(const std::shared_ptr<litehtml::document>& doc, const std::string& tag,
                std::map<std::string, std::string> attributes)
      : litehtml::html_tag(doc), tag(tag), attributes(attributes) {
    // Register the tag name so that css selectors can modify it
    set_tagName(tag.c_str());
  }

  /**
   * @brief Force display:block after normal CSS computation.
   *
   * Custom elements are replaced/leaf elements that must always occupy a full
   * line. Overriding compute_styles lets us guarantee block display regardless
   * of what the master CSS or author stylesheet resolves to.
   *
   * @param recursive Whether to recurse into children (passed through to base)
   */
  void compute_styles(bool recursive = true) override {
    litehtml::html_tag::compute_styles(recursive);
    m_css.set_display(litehtml::display_block);
  }

  void draw_background(litehtml::uint_ptr hdc, litehtml::pixel_t x, litehtml::pixel_t y, const litehtml::position* clip,
                       const std::shared_ptr<litehtml::render_item>& ri) override;
};

std::string DefaultFileLoader(const char* url, const char* baseurl) {
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

static ImFont* getFontFromFamily(const FontFamily& family, FontStyle style) {
  switch (style) {
    case FontStyle::Regular:
      return family.Regular;
    case FontStyle::Bold:
      return family.Bold ? family.Bold : family.Regular;
    case FontStyle::Italic:
      return family.Italic ? family.Italic : family.Regular;
    case FontStyle::BoldItalic:
      if (family.BoldItalic) return family.BoldItalic;
      if (family.Bold) return family.Bold;
      if (family.Italic) return family.Italic;
      return family.Regular;
    default:
      return family.Regular;
  }
}

static ImFont* resolveFont(const Config& cfg, const std::string& family_name, FontStyle style) {
  if (!family_name.empty()) {
    auto it = cfg.FontFamilies.find(family_name);
    if (it != cfg.FontFamilies.end()) {
      if (ImFont* f = getFontFromFamily(it->second, style)) {
        return f;
      }
    }
  }

  if (ImFont* f = getFontFromFamily(cfg.DefaultFont, style)) {
    return f;
  }

  return ImGui::GetFont();
}

}  // namespace

void CustomElement::draw_background(litehtml::uint_ptr hdc, litehtml::pixel_t x, litehtml::pixel_t y,
                                    const litehtml::position* clip, const std::shared_ptr<litehtml::render_item>& ri) {
  // Let the base class draw background color/image and borders first.
  litehtml::html_tag::draw_background(hdc, x, y, clip, ri);

  // ri->pos() is the element's own content box relative to its parent.
  // x/y carry the accumulated offset from all ancestors.
  // Together they give the absolute document position and correct size.
  litehtml::position pos = ri->pos();
  pos.x += x;
  pos.y += y;

  if (customElements.find(this->tag) != customElements.end()) {
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    customElements[this->tag](
        ImRect(cursor + ImVec2(pos.x, pos.y), cursor + ImVec2(pos.x + pos.width, pos.y + pos.height)),
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

  struct ResolvedFont {
    ImFont* Font = nullptr;
    FontStyle Style = FontStyle::Regular;
    std::string Family;
    float Size = 16.0f;
    litehtml::font_metrics Metrics{};
  };

  std::vector<std::unique_ptr<ResolvedFont>> fonts_;

  static ResolvedFont* from_handle(litehtml::uint_ptr hFont) { return reinterpret_cast<ResolvedFont*>(hFont); }

  virtual litehtml::uint_ptr create_font(const litehtml::font_description& descr, const litehtml::document* doc,
                                         litehtml::font_metrics* fm) override {
    bool bold = descr.weight > 400;
    bool italic = descr.style == litehtml::font_style_italic;

    FontStyle font_style = FontStyle::Regular;
    if (bold && italic) {
      font_style = FontStyle::BoldItalic;
    } else if (bold) {
      font_style = FontStyle::Bold;
    } else if (italic) {
      font_style = FontStyle::Italic;
    }

    ImFont* font = resolveFont(config, descr.family, font_style);

    auto rf = std::make_unique<ResolvedFont>();
    rf->Font = font;
    rf->Style = font_style;
    rf->Family = descr.family;
    rf->Size = descr.size;

    const float base_size = font ? font->GetFontBaked(descr.size)->Size : ImGui::GetFontSize();
    const float scale = base_size > 0.0f ? (descr.size / base_size) : 1.0f;

    rf->Metrics.font_size = (int)descr.size;
    rf->Metrics.height = (int)(base_size * scale);
    rf->Metrics.ascent = font ? (int)(font->GetFontBaked(descr.size)->Ascent * scale) : (int)(base_size * 0.8f);
    rf->Metrics.descent = font ? (int)(-font->GetFontBaked(descr.size)->Descent * scale) : (int)(base_size * 0.2f);
    rf->Metrics.x_height = rf->Metrics.ascent / 2;

    if (fm) {
      *fm = rf->Metrics;
    }

    ResolvedFont* raw = rf.get();
    fonts_.push_back(std::move(rf));
    return reinterpret_cast<litehtml::uint_ptr>(raw);
  }

  virtual void delete_font(litehtml::uint_ptr hFont) override {
    // do nothing for now
  }

  virtual litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr hFont) override {
    auto* rf = from_handle(hFont);
    if (!rf || !rf->Font || !text) {
      return 0;
    }

    const char* end = text + strlen(text);
    ImVec2 size = rf->Font->CalcTextSizeA(rf->Size, FLT_MAX, 0.0f, text, end, nullptr);
    return (litehtml::pixel_t)size.x;
  }

  virtual void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color,
                         const litehtml::position& pos) override {
    auto* rf = from_handle(hFont);
    if (!rf || !rf->Font || !text) {
      return;
    }

    ImVec2 p = ImGui::GetCursorScreenPos() + ImVec2(pos.x, pos.y);
    ImU32 col = IM_COL32(color.red, color.green, color.blue, color.alpha);

    ImGui::GetWindowDrawList()->AddText(rf->Font, rf->Size, p, col, text);

    const char* end = text + strlen(text);
    ImVec2 size = rf->Font->CalcTextSizeA(rf->Size, FLT_MAX, 0.0f, text, end, nullptr);
    push_bottom_right(ImVec2(pos.x + size.x, pos.y + size.y));
  }

  //
  // Measurement and defaults
  //

  virtual litehtml::pixel_t pt_to_px(float pt) const override { return pt; }
  virtual litehtml::pixel_t get_default_font_size() const override { return config.BaseFontSize; }
  virtual const char* get_default_font_name() const override { return "Default"; }

  //
  // Drawing functions
  //

  struct LayerGeometry {
    ImVec2 border_min;
    ImVec2 border_max;
    ImVec2 clip_min;
    ImVec2 clip_max;
    float tl, tr, br, bl;
  };

  LayerGeometry get_layer_geometry(const litehtml::background_layer& layer) const {
    ImVec2 screen_pos = ImGui::GetCursorScreenPos();

    LayerGeometry g;
    g.border_min = screen_pos + ImVec2((float)layer.border_box.x, (float)layer.border_box.y);
    g.border_max = screen_pos + ImVec2((float)(layer.border_box.x + layer.border_box.width),
                                       (float)(layer.border_box.y + layer.border_box.height));
    g.clip_min = screen_pos + ImVec2((float)layer.clip_box.x, (float)layer.clip_box.y);
    g.clip_max = screen_pos + ImVec2((float)(layer.clip_box.x + layer.clip_box.width),
                                     (float)(layer.clip_box.y + layer.clip_box.height));

    g.tl = (float)layer.border_radius.top_left_x;
    g.tr = (float)layer.border_radius.top_right_x;
    g.br = (float)layer.border_radius.bottom_right_x;
    g.bl = (float)layer.border_radius.bottom_left_x;
    return g;
  }

  virtual void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 center = ImGui::GetCursorScreenPos() +
                    ImVec2(marker.pos.x + marker.pos.width / 2.0f, marker.pos.y + marker.pos.height / 2.0f);
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

  virtual void load_image(const char* src, const char* baseurl, bool redraw_on_ready) override {
    if (!config.LoadImage) {
      return;
    }

    config.LoadImage(src, baseurl);
  }

  virtual void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override {
    if (!config.GetImageMeta) {
      return;
    }

    auto image_meta = config.GetImageMeta(src, baseurl);
    sz.width = image_meta.Width;
    sz.height = image_meta.Height;
  }

  virtual void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url,
                          const std::string& base_url) override {
    if (!config.GetImageTexture) {
      return;
    }

    ImTextureID texture = config.GetImageTexture(url.c_str(), base_url.c_str());
    if (!texture) {
      return;
    }

    LayerGeometry lgm = this->get_layer_geometry(layer);
    ImVec2 p_min = lgm.border_min;
    ImVec2 p_max = lgm.border_max;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float radius = std::min({lgm.tl, lgm.tr, lgm.br, lgm.bl});

    draw_list->PushClipRect(lgm.clip_min, lgm.clip_max, true);

    if (radius > 0.0f) {
      draw_list->AddImageRounded(texture, p_min, p_max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, lgm.tl);
    } else {
      draw_list->AddImage(texture, p_min, p_max);
    }

    draw_list->PopClipRect();

    push_bottom_right(ImVec2(lgm.border_max.x, lgm.border_max.y));
  }

  virtual void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                               const litehtml::web_color& color) override {
    if (color.alpha == 0) {
      return;
    }

    const litehtml::position& bg_box = layer.border_box;
    const litehtml::position& clip_box = layer.clip_box;

    if (bg_box.width <= 0 || bg_box.height <= 0 || clip_box.width <= 0 || clip_box.height <= 0) {
      return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    LayerGeometry lgm = this->get_layer_geometry(layer);

    ImU32 col = IM_COL32(color.red, color.green, color.blue, color.alpha);

    draw_list->PushClipRect(lgm.clip_min, lgm.clip_max, true);

    if (lgm.tl == lgm.tr && lgm.tr == lgm.br && lgm.br == lgm.bl) {
      draw_list->AddRectFilled(lgm.border_min, lgm.border_max, col, lgm.tl);
    } else {
      draw_list->PathClear();

      if (lgm.tl > 0.0f)
        draw_list->PathArcTo(ImVec2(lgm.border_min.x + lgm.tl, lgm.border_min.y + lgm.tl), lgm.tl, IM_PI, IM_PI * 1.5f);
      else
        draw_list->PathLineTo(ImVec2(lgm.border_min.x, lgm.border_min.y));

      if (lgm.tr > 0.0f)
        draw_list->PathArcTo(
            ImVec2(lgm.border_max.x - lgm.tr, lgm.border_min.y + lgm.tr), lgm.tr, IM_PI * 1.5f, IM_PI * 2.0f);
      else
        draw_list->PathLineTo(ImVec2(lgm.border_max.x, lgm.border_min.y));

      if (lgm.br > 0.0f)
        draw_list->PathArcTo(ImVec2(lgm.border_max.x - lgm.br, lgm.border_max.y - lgm.br), lgm.br, 0.0f, IM_PI * 0.5f);
      else
        draw_list->PathLineTo(ImVec2(lgm.border_max.x, lgm.border_max.y));

      if (lgm.bl > 0.0f)
        draw_list->PathArcTo(ImVec2(lgm.border_min.x + lgm.bl, lgm.border_max.y - lgm.bl), lgm.bl, IM_PI * 0.5f, IM_PI);
      else
        draw_list->PathLineTo(ImVec2(lgm.border_min.x, lgm.border_max.y));

      draw_list->PathFillConvex(col);
    }

    draw_list->PopClipRect();

    push_bottom_right(ImVec2((float)(bg_box.x + bg_box.width), (float)(bg_box.y + bg_box.height)));
  }

  static constexpr float kEpsilon = 1e-6f;

  static ImU32 to_im_col32(const litehtml::web_color& c) { return IM_COL32(c.red, c.green, c.blue, c.alpha); }

  static litehtml::web_color sample_gradient_color(const std::vector<litehtml::background_layer::color_point>& points,
                                                   float t) {
    litehtml::web_color out{0, 0, 0, 0};

    if (points.empty()) {
      return out;
    }

    if (t <= points.front().offset) {
      return points.front().color;
    }

    if (t >= points.back().offset) {
      return points.back().color;
    }

    for (size_t i = 1; i < points.size(); ++i) {
      const auto& a = points[i - 1];
      const auto& b = points[i];

      if (t >= a.offset && t <= b.offset) {
        const float span = b.offset - a.offset;
        const float u = (span > 0.0f) ? ((t - a.offset) / span) : 0.0f;

        auto lerp_u8 = [u](unsigned char x, unsigned char y) -> unsigned char {
          return (unsigned char)(x + (y - x) * u);
        };

        out.red = lerp_u8(a.color.red, b.color.red);
        out.green = lerp_u8(a.color.green, b.color.green);
        out.blue = lerp_u8(a.color.blue, b.color.blue);
        out.alpha = lerp_u8(a.color.alpha, b.color.alpha);
        return out;
      }
    }

    return points.back().color;
  }

  static float cross2(const ImVec2& a, const ImVec2& b) { return a.x * b.y - a.y * b.x; }

  static ImVec2 line_intersection(const ImVec2& p1, const ImVec2& p2, const ImVec2& q1, const ImVec2& q2) {
    const ImVec2 r = p2 - p1;
    const ImVec2 s = q2 - q1;
    const float rxs = cross2(r, s);

    if (fabsf(rxs) < kEpsilon) {
      return p1;  // parallel fallback
    }

    const float t = cross2(q1 - p1, s) / rxs;
    return p1 + r * t;
  }

  static bool is_inside_edge(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
    // For counter-clockwise clip polygon, inside is on the left side of edge AB.
    return cross2(b - a, p - a) >= 0.0f;
  }

  static std::vector<ImVec2> clip_polygon_convex(const std::vector<ImVec2>& subject_polygon,
                                                 const std::vector<ImVec2>& clip_polygon) {
    std::vector<ImVec2> result = subject_polygon;

    for (size_t edge_index = 0; edge_index < clip_polygon.size(); ++edge_index) {
      const ImVec2& clip_edge_start = clip_polygon[edge_index];
      const ImVec2& clip_edge_end = clip_polygon[(edge_index + 1) % clip_polygon.size()];

      if (result.empty()) {
        break;
      }

      std::vector<ImVec2> input = std::move(result);
      result.clear();

      ImVec2 previous_point = input.back();
      bool previous_inside = is_inside_edge(previous_point, clip_edge_start, clip_edge_end);

      for (const ImVec2& current_point : input) {
        const bool current_inside = is_inside_edge(current_point, clip_edge_start, clip_edge_end);

        if (current_inside) {
          if (!previous_inside) {
            result.push_back(line_intersection(previous_point, current_point, clip_edge_start, clip_edge_end));
          }

          result.push_back(current_point);

        } else if (previous_inside) {
          result.push_back(line_intersection(previous_point, current_point, clip_edge_start, clip_edge_end));
        }

        previous_point = current_point;
        previous_inside = current_inside;
      }
    }

    return result;
  }

  template <typename ColorFunc>
  static void draw_convex_shaded_polygon(ImDrawList* draw_list, const std::vector<ImVec2>& poly,
                                         ColorFunc&& color_for_point) {
    if (poly.size() < 3) {
      return;
    }

    const ImVec2 uv = draw_list->_Data->TexUvWhitePixel;
    const ImDrawIdx base = draw_list->_VtxCurrentIdx;
    const int vtx_count = (int)poly.size();
    const int idx_count = (vtx_count - 2) * 3;

    draw_list->PrimReserve(idx_count, vtx_count);

    for (int i = 1; i < vtx_count - 1; ++i) {
      draw_list->PrimWriteIdx(base + 0);
      draw_list->PrimWriteIdx(base + i);
      draw_list->PrimWriteIdx(base + i + 1);
    }

    for (const ImVec2& p : poly) {
      draw_list->PrimWriteVtx(p, uv, color_for_point(p));
    }
  }

  template <typename ColorFunc>
  static void draw_clipped_shaded_polygon(ImDrawList* draw_list, const std::vector<ImVec2>& poly,
                                          const std::vector<ImVec2>& clip_poly, ColorFunc&& color_for_point) {
    std::vector<ImVec2> clipped = clip_polygon_convex(poly, clip_poly);
    if (clipped.size() < 3) {
      return;
    }

    draw_convex_shaded_polygon(draw_list, clipped, std::forward<ColorFunc>(color_for_point));
  }

  static void append_point_if_distinct(std::vector<ImVec2>& pts, const ImVec2& p, float eps = 0.01f) {
    if (pts.empty()) {
      pts.push_back(p);
      return;
    }

    const ImVec2& last = pts.back();
    if (fabsf(last.x - p.x) > eps || fabsf(last.y - p.y) > eps) {
      pts.push_back(p);
    }
  }

  static void append_arc_points(std::vector<ImVec2>& pts, const ImVec2& center, float radius, float a_min, float a_max,
                                int segments, bool skip_first) {
    if (radius <= 0.0f || segments <= 0) {
      return;
    }

    for (int i = skip_first ? 1 : 0; i <= segments; ++i) {
      const float t = (float)i / (float)segments;
      const float a = a_min + (a_max - a_min) * t;
      append_point_if_distinct(pts, ImVec2(center.x + cosf(a) * radius, center.y + sinf(a) * radius));
    }
  }

  static std::vector<ImVec2> build_rect_polygon(const ImVec2& p_min, const ImVec2& p_max) {
    return {
        ImVec2(p_min.x, p_min.y),
        ImVec2(p_max.x, p_min.y),
        ImVec2(p_max.x, p_max.y),
        ImVec2(p_min.x, p_max.y),
    };
  }

  static std::vector<ImVec2> build_rounded_rect_polygon(const ImVec2& p_min, const ImVec2& p_max, float tl, float tr,
                                                        float br, float bl, int arc_segments = 8) {
    std::vector<ImVec2> pts;
    pts.reserve(4 * (arc_segments + 1));

    const float w = p_max.x - p_min.x;
    const float h = p_max.y - p_min.y;
    const float max_r = ImMin(w * 0.5f, h * 0.5f);

    tl = ImClamp(tl, 0.0f, max_r);
    tr = ImClamp(tr, 0.0f, max_r);
    br = ImClamp(br, 0.0f, max_r);
    bl = ImClamp(bl, 0.0f, max_r);

    append_point_if_distinct(pts, ImVec2(p_min.x + tl, p_min.y));
    append_point_if_distinct(pts, ImVec2(p_max.x - tr, p_min.y));

    if (tr > 0.0f) {
      append_arc_points(pts, ImVec2(p_max.x - tr, p_min.y + tr), tr, -IM_PI * 0.5f, 0.0f, arc_segments, true);
    }

    append_point_if_distinct(pts, ImVec2(p_max.x, p_max.y - br));

    if (br > 0.0f) {
      append_arc_points(pts, ImVec2(p_max.x - br, p_max.y - br), br, 0.0f, IM_PI * 0.5f, arc_segments, true);
    }

    append_point_if_distinct(pts, ImVec2(p_min.x + bl, p_max.y));

    if (bl > 0.0f) {
      append_arc_points(pts, ImVec2(p_min.x + bl, p_max.y - bl), bl, IM_PI * 0.5f, IM_PI, arc_segments, true);
    }

    append_point_if_distinct(pts, ImVec2(p_min.x, p_min.y + tl));

    if (tl > 0.0f) {
      append_arc_points(pts, ImVec2(p_min.x + tl, p_min.y + tl), tl, IM_PI, IM_PI * 1.5f, arc_segments, true);
    }

    return pts;
  }

  static bool has_rounded_corners(const LayerGeometry& lgm) {
    return lgm.tl > 0.0f || lgm.tr > 0.0f || lgm.br > 0.0f || lgm.bl > 0.0f;
  }

  static std::vector<ImVec2> build_layer_fill_polygon(const LayerGeometry& lgm, int arc_segments = 12) {
    if (has_rounded_corners(lgm)) {
      return build_rounded_rect_polygon(lgm.border_min, lgm.border_max, lgm.tl, lgm.tr, lgm.br, lgm.bl, arc_segments);
    }

    return build_rect_polygon(lgm.border_min, lgm.border_max);
  }

  static std::vector<ImVec2> build_ellipse_polygon(const ImVec2& center, float rx, float ry, float t, int segments) {
    std::vector<ImVec2> pts;
    pts.reserve(segments);

    const float ex = rx * t;
    const float ey = ry * t;

    for (int i = 0; i < segments; ++i) {
      const float a = ((float)i / (float)segments) * IM_PI * 2.0f;
      pts.push_back(ImVec2(center.x + cosf(a) * ex, center.y + sinf(a) * ey));
    }

    return pts;
  }

  static ImVec2 conic_point_on_circle(const ImVec2& center, float radius, float angle_deg) {
    const float a = angle_deg * IM_PI / 180.0f;

    // 0 degrees at top, clockwise positive
    const float x = sinf(a);
    const float y = -cosf(a);

    return ImVec2(center.x + x * radius, center.y + y * radius);
  }

  static std::vector<ImVec2> build_conic_wedge_polygon(const ImVec2& center, float radius, float angle0_deg,
                                                       float angle1_deg, int arc_segments) {
    std::vector<ImVec2> pts;
    pts.reserve(arc_segments + 3);

    pts.push_back(center);

    for (int i = 0; i <= arc_segments; ++i) {
      const float t = (float)i / (float)arc_segments;
      const float a = angle0_deg + (angle1_deg - angle0_deg) * t;
      pts.push_back(conic_point_on_circle(center, radius, a));
    }

    return pts;
  }

  void draw_linear_gradient_impl(const LayerGeometry& lgm,
                                 const litehtml::background_layer::linear_gradient& gradient) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    const ImVec2 start = screen_pos + ImVec2(gradient.start.x, gradient.start.y);
    const ImVec2 end = screen_pos + ImVec2(gradient.end.x, gradient.end.y);

    const ImVec2 axis = end - start;
    const float axis_len_sq = axis.x * axis.x + axis.y * axis.y;
    if (axis_len_sq <= 0.0001f) {
      return;
    }

    const float axis_len = sqrtf(axis_len_sq);
    const ImVec2 dir(axis.x / axis_len, axis.y / axis_len);
    const ImVec2 normal(-dir.y, dir.x);

    const float w = lgm.border_max.x - lgm.border_min.x;
    const float h = lgm.border_max.y - lgm.border_min.y;
    const float extent = sqrtf(w * w + h * h) + 2.0f;
    const float approx_span = ImMax(w, h);

    const int strips = (int)ImClamp(axis_len / 2.0f + approx_span / 4.0f, 16.0f, 128.0f);
    const std::vector<ImVec2> fill_poly = build_layer_fill_polygon(lgm, 8);

    auto color_for_point = [&](const ImVec2& p) -> ImU32 {
      float t = ((p.x - start.x) * axis.x + (p.y - start.y) * axis.y) / axis_len_sq;
      t = ImClamp(t, 0.0f, 1.0f);
      return to_im_col32(sample_gradient_color(gradient.color_points, t));
    };

    for (int i = 0; i < strips; ++i) {
      const float t0 = (float)i / (float)strips;
      const float t1 = (float)(i + 1) / (float)strips;

      const ImVec2 p0 = start + dir * (t0 * axis_len);
      const ImVec2 p1 = start + dir * (t1 * axis_len);

      const std::vector<ImVec2> strip_quad = {
          p0 - normal * extent,
          p0 + normal * extent,
          p1 + normal * extent,
          p1 - normal * extent,
      };

      draw_clipped_shaded_polygon(draw_list, strip_quad, fill_poly, color_for_point);
    }
  }

  void draw_radial_gradient_impl(const LayerGeometry& lgm,
                                 const litehtml::background_layer::radial_gradient& gradient) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    const ImVec2 center = screen_pos + ImVec2(gradient.position.x, gradient.position.y);

    const float rx = gradient.radius.x;
    const float ry = gradient.radius.y;

    if (rx <= 0.0001f || ry <= 0.0001f || gradient.color_points.empty()) {
      return;
    }

    const std::vector<ImVec2> fill_poly = build_layer_fill_polygon(lgm, 12);

    // Draw from outside to inside so smaller inner ellipses overwrite larger ones.
    const int ring_count = 64;
    const int ellipse_segments = 64;

    for (int i = ring_count; i >= 1; --i) {
      const float t = (float)i / (float)ring_count;
      const std::vector<ImVec2> ellipse = build_ellipse_polygon(center, rx, ry, t, ellipse_segments);

      std::vector<ImVec2> clipped = clip_polygon_convex(ellipse, fill_poly);
      if (clipped.size() < 3) {
        continue;
      }

      const ImU32 col = to_im_col32(sample_gradient_color(gradient.color_points, t));
      draw_convex_shaded_polygon(draw_list, clipped, [&](const ImVec2&) -> ImU32 { return col; });
    }

    // Fill the center with t=0 color.
    {
      const std::vector<ImVec2> center_poly =
          build_ellipse_polygon(center, rx, ry, 1.0f / (float)ring_count, ellipse_segments);

      std::vector<ImVec2> clipped = clip_polygon_convex(center_poly, fill_poly);
      if (clipped.size() >= 3) {
        const ImU32 col = to_im_col32(sample_gradient_color(gradient.color_points, 0.0f));
        draw_convex_shaded_polygon(draw_list, clipped, [&](const ImVec2&) -> ImU32 { return col; });
      }
    }
  }

  void draw_conic_gradient_impl(const LayerGeometry& lgm, const litehtml::background_layer::conic_gradient& gradient) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    const ImVec2 center = screen_pos + ImVec2(gradient.position.x, gradient.position.y);

    const float radius = gradient.radius;
    if (radius <= 0.0001f || gradient.color_points.empty()) {
      return;
    }

    const std::vector<ImVec2> fill_poly = build_layer_fill_polygon(lgm, 12);

    const int wedge_count = 128;
    const int arc_segments_per_wedge = 1;

    for (int i = 0; i < wedge_count; ++i) {
      const float t0 = (float)i / (float)wedge_count;
      const float t1 = (float)(i + 1) / (float)wedge_count;

      const float a0 = gradient.angle + t0 * 360.0f;
      const float a1 = gradient.angle + t1 * 360.0f;

      const std::vector<ImVec2> wedge = build_conic_wedge_polygon(center, radius, a0, a1, arc_segments_per_wedge);

      std::vector<ImVec2> clipped = clip_polygon_convex(wedge, fill_poly);
      if (clipped.size() < 3) {
        continue;
      }

      const ImU32 col = to_im_col32(sample_gradient_color(gradient.color_points, t0));

      draw_convex_shaded_polygon(draw_list, clipped, [&](const ImVec2&) -> ImU32 { return col; });
    }
  }

  template <typename Gradient, typename DrawFn>
  void draw_gradient_common(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const Gradient& gradient,
                            DrawFn&& draw_fn) {
    const litehtml::position& bg_box = layer.border_box;
    const litehtml::position& clip_box = layer.clip_box;

    if (bg_box.width <= 0 || bg_box.height <= 0 || clip_box.width <= 0 || clip_box.height <= 0) {
      return;
    }

    if (gradient.color_points.empty()) {
      return;
    }

    LayerGeometry lgm = this->get_layer_geometry(layer);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->PushClipRect(lgm.clip_min, lgm.clip_max, true);
    draw_fn(lgm, gradient);
    draw_list->PopClipRect();

    push_bottom_right(ImVec2((float)(bg_box.x + bg_box.width), (float)(bg_box.y + bg_box.height)));
  }

  virtual void draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                    const litehtml::background_layer::linear_gradient& gradient) override {
    const ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    const ImVec2 start = screen_pos + ImVec2(gradient.start.x, gradient.start.y);
    const ImVec2 end = screen_pos + ImVec2(gradient.end.x, gradient.end.y);
    const ImVec2 axis = end - start;
    const float axis_len_sq = axis.x * axis.x + axis.y * axis.y;

    if (axis_len_sq <= 0.0001f) {
      if (!gradient.color_points.empty()) {
        draw_solid_fill(hdc, layer, gradient.color_points.back().color);
      }
      return;
    }

    draw_gradient_common(
        hdc, layer, gradient, [&](const LayerGeometry& lgm, const auto& g) { draw_linear_gradient_impl(lgm, g); });
  }

  virtual void draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                    const litehtml::background_layer::radial_gradient& gradient) override {
    draw_gradient_common(
        hdc, layer, gradient, [&](const LayerGeometry& lgm, const auto& g) { draw_radial_gradient_impl(lgm, g); });
  }

  virtual void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                   const litehtml::background_layer::conic_gradient& gradient) override {
    draw_gradient_common(
        hdc, layer, gradient, [&](const LayerGeometry& lgm, const auto& g) { draw_conic_gradient_impl(lgm, g); });
  }

  virtual void on_mouse_event(const litehtml::element::ptr& el, litehtml::mouse_event event) override {
    // TODO
  }

  virtual void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders,
                            const litehtml::position& draw_pos, bool root) override {
    ImVec2 base_pos = ImGui::GetCursorScreenPos();
    ImVec2 top_left = base_pos + ImVec2(draw_pos.x, draw_pos.y);
    ImVec2 top_right = base_pos + ImVec2(draw_pos.x + draw_pos.width, draw_pos.y);
    ImVec2 bottom_right = base_pos + ImVec2(draw_pos.x + draw_pos.width, draw_pos.y + draw_pos.height);
    ImVec2 bottom_left = base_pos + ImVec2(draw_pos.x, draw_pos.y + draw_pos.height);

    auto* draw_list = ImGui::GetWindowDrawList();

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

        ImU32 color =
            IM_COL32(borders.top.color.red, borders.top.color.green, borders.top.color.blue, borders.top.color.alpha);

        if (tl == tr && tr == br && br == bl) {
          draw_list->AddRect(p_min, p_max, color, tl, 0, w);
        } else {
          draw_list->PathClear();
          if (tl > 0.0f)
            draw_list->PathArcTo(ImVec2(p_min.x + tl, p_min.y + tl), tl, IM_PI, IM_PI * 1.5f);
          else
            draw_list->PathLineTo(ImVec2(p_min.x, p_min.y));

          if (tr > 0.0f)
            draw_list->PathArcTo(ImVec2(p_max.x - tr, p_min.y + tr), tr, IM_PI * 1.5f, IM_PI * 2.0f);
          else
            draw_list->PathLineTo(ImVec2(p_max.x, p_min.y));

          if (br > 0.0f)
            draw_list->PathArcTo(ImVec2(p_max.x - br, p_max.y - br), br, 0.0f, IM_PI * 0.5f);
          else
            draw_list->PathLineTo(ImVec2(p_max.x, p_max.y));

          if (bl > 0.0f)
            draw_list->PathArcTo(ImVec2(p_min.x + bl, p_max.y - bl), bl, IM_PI * 0.5f, IM_PI);
          else
            draw_list->PathLineTo(ImVec2(p_min.x, p_max.y));

          draw_list->PathStroke(color, ImDrawFlags_Closed, w);
        }
      }
    } else {
      // The Non-Uniform Path (Mitered Borders via Quads)
      auto color32 = [](const litehtml::web_color& c) { return IM_COL32(c.red, c.green, c.blue, c.alpha); };

      // Top border
      if (borders.top.width > 0) {
        draw_list->AddQuadFilled(top_left,
                                 ImVec2(bottom_right.x, top_left.y),
                                 ImVec2(bottom_right.x - borders.right.width, top_left.y + borders.top.width),
                                 ImVec2(top_left.x + borders.left.width, top_left.y + borders.top.width),
                                 color32(borders.top.color));
      }

      // Bottom border
      if (borders.bottom.width > 0) {
        draw_list->AddQuadFilled(ImVec2(top_left.x + borders.left.width, bottom_right.y - borders.bottom.width),
                                 ImVec2(bottom_right.x - borders.right.width, bottom_right.y - borders.bottom.width),
                                 bottom_right,
                                 ImVec2(top_left.x, bottom_right.y),
                                 color32(borders.bottom.color));
      }

      // Left border
      if (borders.left.width > 0) {
        draw_list->AddQuadFilled(top_left,
                                 ImVec2(top_left.x + borders.left.width, top_left.y + borders.top.width),
                                 ImVec2(top_left.x + borders.left.width, bottom_right.y - borders.bottom.width),
                                 ImVec2(top_left.x, bottom_right.y),
                                 color32(borders.left.color));
      }

      // Right border
      if (borders.right.width > 0) {
        draw_list->AddQuadFilled(ImVec2(bottom_right.x - borders.right.width, top_left.y + borders.top.width),
                                 ImVec2(bottom_right.x, top_left.y),
                                 bottom_right,
                                 ImVec2(bottom_right.x - borders.right.width, bottom_right.y - borders.bottom.width),
                                 color32(borders.right.color));
      }
    }

    push_bottom_right(ImVec2(draw_pos.x + draw_pos.width, draw_pos.y + draw_pos.height));
  }
  //
  // Document related functions
  //

  virtual void set_caption(const char* caption) override { title = caption; }
  virtual void set_base_url(const char* base_url) override {}
  virtual void link(const std::shared_ptr<litehtml::document>& doc, const litehtml::element::ptr& el) override {}
  virtual void on_anchor_click(const char* url, const litehtml::element::ptr& el) override {
    history.push_back(currentUrl);
    loadUrl = url;
  }
  virtual void set_cursor(const char* cursor) override {
    if (std::string(cursor) == "pointer" && ImGui::IsWindowHovered()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
  }
  virtual void transform_text(std::string& text, litehtml::text_transform tt) override {}
  virtual void import_css(std::string& text, const std::string& url, std::string& baseurl) override {
    if (!config.LoadCSS) {
      return;
    }
    text = config.LoadCSS(url.c_str(), baseurl.c_str());
  }

  //
  // Clipping functions
  //

  virtual void set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) override {}
  virtual void del_clip() override {}

  //
  // Layout functions
  //

  virtual void get_viewport(litehtml::position& client) const override {
    client.x = 0;
    client.y = 0;
    client.width = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    client.height = ImGui::GetContentRegionAvail().y;
  }

  virtual litehtml::element::ptr create_element(const char* tag_name, const litehtml::string_map& attributes,
                                                const std::shared_ptr<litehtml::document>& doc) override {
    if (customElements.find(tag_name) != customElements.end()) {
      return std::make_shared<CustomElement>(doc, tag_name, attributes);
    }

    return nullptr;
  }

  virtual void get_media_features(litehtml::media_features& media) const override {
    media.color = 8;
    media.resolution = 96;
    media.width = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    media.height = ImGui::GetContentRegionAvail().y;
    media.device_width = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    media.device_height = ImGui::GetContentRegionAvail().y;
    media.type = litehtml::media_type_screen;
  }

  virtual void get_language(litehtml::string& language, litehtml::string& culture) const override {
    language = "en";
    culture = "US";
  }
};

Config* GetConfig() { return &config; }
void SetConfig(const Config& newConfig) { config = newConfig; }
void PushConfig(const Config& config) { configStack.push_back(config); }
void PopConfig() {
  assert(!configStack.empty());
  configStack.pop_back();
}

void RegisterCustomElement(const char* tagName, CustomElementDrawFunction draw) { customElements[tagName] = draw; }

void UnregisterCustomElement(const char* tagName) { customElements.erase(tagName); }

bool Canvas(const char* id, const char* html, float width, std::string* clickedURL) {
  struct state {
    std::shared_ptr<BrowserContainer> container;
    std::shared_ptr<litehtml::document> doc;
    std::string html;
    long long last_active_time;
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
        .last_active_time = std::chrono::high_resolution_clock::now().time_since_epoch().count(),
    };
  }

  auto& state = states[id];

  if (state.html != html) {
    state.doc = litehtml::document::createFromString(html, state.container.get());
    state.html = html;
  }

  state.last_active_time = std::chrono::high_resolution_clock::now().time_since_epoch().count();

  state.container->set_config(getCurrentConfig());
  state.container->reset();

  int render_width = width > 0 ? (int)width : (int)ImGui::GetContentRegionAvail().x;
  state.doc->render(render_width);

  litehtml::position clip(
      0, 0, render_width, std::max((int)state.doc->height(), (int)ImGui::GetContentRegionAvail().y));
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
    if (it->first != id && now - it->second.last_active_time > 1000000000) {
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
