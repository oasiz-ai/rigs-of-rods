# Third-party notices

## Material Maker

`rorsmith/procedural.py` contains CPU (numpy) ports of node definitions from
Material Maker — https://github.com/RodZill4/material-maker

    MIT License
    Copyright (c) Rodolphe Suescun and Material Maker contributors

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

Ported sources, all under `addons/material_maker/`:

| Upstream file | Ported symbols | rorsmith location |
|---|---|---|
| `shader_functions.tres` | `rand`, `rand2`, `rand3` | `_mm_rand`, `_mm_rand2`, `_mm_rand3` |
| `nodes/bricks.mmg` | `oldbrick`, `oldbricks_rb`, `oldbricks_rb2`, `oldbricks_hb` | `_brick_field`, `_bricks_rb`, `_bricks_rb2`, `_bricks_hb` |
| `nodes/arc_pavement.mmg` | `pavement`, `arc_pavement` | `_pavement_mask`, `_arc_pavement` |
| `nodes/scratches.mmg` | `old_scratch`, `old_scratches` | `_scratch`, `_scratches` |

Parameter names are kept identical to the upstream nodes so a Material Maker
graph and a `rorsmith` call describe the same surface.

The `grime`, `moss` and `surface_noise` generators, the value-noise fbm, the
height/roughness/AO derivations and the layer-stack weighting are rorsmith
originals and are labelled as such in every tool response.
