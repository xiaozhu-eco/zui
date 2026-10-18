# 小竹标识

标识是 **竹**（`brand/bamboo`，主题 `themes/bamboo`）。

图标用 PNG。产品图标和纸底、墨底标志是整张设计稿。

工具图标在 `bamboo/icons/`，每张都带周围的纸色。整行原图在 `bamboo/sheets/`。一共 54 张：

- 文件：`document` `folder` `save` `file-new` `copy` `trash` `download` `upload` `search`
- 编辑：`undo` `redo` `cut` `paste` `filter` `link` `settings`
- 导航：`home` `menu` `close` `back` `chevron` `more`
- 文本：`bold` `italic` `underline` `list` `quote` `code`
- 视图：`window` `panes` `grid` `rows` `zoom-in` `zoom-out` `eye` `sidebar`
- 人与状态：`user` `users` `chat` `bell` `check` `alert` `mail`
- 标记：`plus` `minus` `star` `lock` `calendar` `image`
- 媒体：`play` `pause` `info` `external` `refresh`

`App::set_theme("bamboo")` 会找**文档旁边**的 `bamboo.css`，不是本目录。要用这套主题时，把 `resources/themes/bamboo/bamboo.css` 拷到文档同目录。深色用 `bamboo-dark`。变量名和示例应用的 token 一致：`--paper`、`--ink`、`--accent`、`--line`。
