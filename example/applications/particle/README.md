# Particle bench

A tree of effects on the left, a particle field in the middle, a log below, parameters
on the right. Splitters drag in 40 px class steps (`tree-0`…`tree-8`, `param-0`…`param-8`)
on `#bench`. The log shares the field's width.

The field is absolutely positioned HTML dots. Positions come from a session stepper on
`Document::set_interval`. Each dot's `left` / `top` is a template `style` percentage.
zui does not run GLSL.

Panel knobs that the session understands (`U_COUNT` / `U_SPEED` / `U_SPREAD`) rewrite
those `#define` numbers in imported Khronos GLSL (`.glsl` / `.vert` / `.frag`). The rest
of the shader text is exported unchanged. Native import/export uses path fields and
reads the disk. The browser builds a file picker and a download.

## Run

```bash
cd example/applications/particle
python -m http.server 8772
# http://127.0.0.1:8772/
```

```bash
cmake --build --preset windows-x64 --target zui_particle
build/windows-x64/particle/zui_particle.exe
```

The browser port is 8772 so it does not collide with kanban on 8770.

## How it is cut

The same HTML and CSS run in the browser and in zui. zui does not run `js/`. Native
handlers live in `cpp/`.

```
index.html
css/           tokens, shell, bench, motion, theme
js/            boot, bridge, sim, view
main.cpp
cpp/           session, field, motion, shader, controls, rows
```
