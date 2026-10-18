# zui-designer

小竹设计：在 zui 上做界面。左边拖组件，中间是桌面或手机画布，右边改属性和复制生成的 HTML。

画布上的控件按 8 像素一档摆放。桌面是自由坐标，手机是从上到下排列。容器最多套三层，里面的组件按横向或纵向排。导出的是一份不带脚本的 HTML，标签是 `h2`、`p`、`button`、`input`、`textarea` 这些，样式是标准 CSS。

## 浏览器里看

直接打开 `ui/index.html`。脚本在上一级的 `js/`，供浏览器对照，zui 运行时不执行它们。

页面会把当前稿子记在 `localStorage`（`zui-designer-v1`）。

## 原生程序

`zui_designer` 只链 `include/zui/app.h`。交互用已经接上的指针、键盘、`set` / `set_model` / `add_class`、剪贴板和保存对话框。编译拷贝 `index.html` 和 `css/`，不拷贝 `js/`。产物在构建目录的 `designer/`，和 `zui_create` 放在一起。`cmake --install <build> --component zui_tools` 把这两个工具装进前缀的 `tools/`。

稿子写在该应用的用户数据目录 `board.json`。导出走系统保存对话框。
