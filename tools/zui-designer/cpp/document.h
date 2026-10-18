#ifndef ZUI_DESIGNER_DOCUMENT_H
#define ZUI_DESIGNER_DOCUMENT_H

#include <memory>
#include <string>
#include <vector>

namespace designer {

struct Node {
    std::string id;
    std::string kind;
    std::string text;
    int x = 0;
    int y = 0;
    int w = 8;
    int h = 4;
    std::string bg;
    std::string fg;
    std::string fs;
    std::string radius;
    std::string align;
    std::vector<std::unique_ptr<Node>> children;
};

struct Located {
    Node* node = nullptr;
    Node* parent = nullptr;
    std::vector<std::unique_ptr<Node>>* list = nullptr;
    int index = -1;
};

struct Document {
    std::string device = "desktop";
    std::string title = "未命名界面";
    int next_id = 1;
    std::string selected;
    std::vector<std::unique_ptr<Node>> roots;
    std::vector<std::string> undo;
    std::vector<std::string> redo;
};

struct DeviceSpec {
    const char* key;
    const char* label;
    int w;
    int h;
    int cols;
    int rows;
    bool free;
};

struct KindSpec {
    const char* kind;
    const char* label;
    const char* text;
    int w;
    int h;
    const char* bg;
    const char* fg;
    const char* fs;
    const char* radius;
    const char* align;
};

constexpr int kCell = 8;

const DeviceSpec& device_of(const Document& doc);
const KindSpec* kind_of(const std::string& kind);
bool is_container(const Node* node);
bool is_container_kind(const std::string& kind);

Located locate(Document& doc, const std::string& id);
int depth_of(Document& doc, const Node* node);
bool is_under(Document& doc, const Node* node, const std::string& ancestor_id);

void clamp_box(const Document& doc, Node& node);
std::unique_ptr<Node> make_node(Document& doc, const std::string& kind);
std::string look_class(const Node& node);
std::string geom_class(const Node& node);
std::string flags_class(const Node& node);
std::string layer_text(Document& doc, const Node& node);
std::string look_field(const Node& node, const std::string& group);
void set_look_field(Node& node, const std::string& group, const std::string& token);

std::string to_json(const Document& doc);
bool from_json(Document& doc, const std::string& text);

void push_undo(Document& doc);
bool undo_doc(Document& doc);
bool redo_doc(Document& doc);

bool can_place(Document& doc, const std::string& kind, const Node* parent);
bool can_reparent(Document& doc, const Node* node, const Node* parent);
void insert_node(Document& doc, std::unique_ptr<Node> node, Node* parent, int index);
bool move_to(Document& doc, const std::string& id, Node* parent, int index);
bool remove_node(Document& doc, const std::string& id);
void load_sample(Document& doc);

void collect_nodes(const std::vector<std::unique_ptr<Node>>& list, std::vector<Node*>& out);

std::string generate_html(const Document& doc);

}  // namespace designer

#endif
