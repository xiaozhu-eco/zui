# File explorer

Browser-only mock. There is no native session and no CMake target.

The page is CSS state: radio buttons pick a folder and a file, checkboxes pick view
mode and preview. Sample files live in `files/`. `preview/` holds HTML stand-ins for
those files.

```bash
cd example/applications/file-explorer
python -m http.server 8775
# http://127.0.0.1:8775/
```

```
index.html
css/           tokens, shell, files, preview, motion, theme
files/         sample documents
preview/       HTML stand-ins
```
