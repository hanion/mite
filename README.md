# mite

MInimal TEmplated static site generator with C templates

## what it does

- renders `.md` files to `.html` using `.mite` templates
- templates are just C
- fast, no dependencies, cross-platform
- outputs plain `.html` into the same folder as the `.md`

## usage

```sh
$ cc -o mite mite.c && ./mite
```

## example structure

> This repository matches this structure and includes real working examples

```py
.
├── mite.c
├── index.md
├── layout/
│   ├── home.mite
│   └── post.mite
├── include/
│   ├── head.mite
│   └── footer.mite
└── post/
    ├── my-post/
    │   └── my-post.md
    └── another-post/
        └── post.md
```

## layout and includes

- templates go in `layout/`
- reusable parts go in `include/`
- all `.mite` files in both are globally available
- any of them can call `<? CONTENT() ?>`

to use a layout for a page, set the layout name in its front matter:
```c
page->layout = "post";
```

to include a template:
```c
<? INCLUDE("footer") ?>
```

## template syntax

`.mite` files are regular HTML with embedded C between `<? ?>`

```html
<ul>
<? for (int i = 0; i < 3; ++i) { ?>
	<li><? INT(i) ?></li>
<? } ?>
</ul>
```

- code inside `<? ?>` is pasted as-is into C
- `INT(...)`, `STR(...)`, etc. are macros provided by the engine

## front matter

```c
---
page->layout = "post";
page->title  = "my post title";
page->date   = "2025-12-30";
page->tags   = "math simulation";
---
```

- front matter is just C
- you can access `page->` and `global.` in templates

## custom data

you can set custom key/value data (`const char *`) using:
```c
PAGE_SET(key, value);
GLOBAL_SET(key, value);
```

then access it with:
```c
PAGE_GET(key)        // returns char* or NULL
PAGE_HAS(key)        // true if key exists
PAGE_IS(key, value)  // strcmp values
```

### custom data example

- `post/bezier_curves/bezier_curves.md`
```c
---
page->layout = "post";
page->title  = "Bézier curves";
PAGE_SET("mathjax", "true");
---
```

- `include/head.mite`
```c
<? if (PAGE_IS("mathjax", "true")) { ?>
	<!-- Load MathJax -->
<? } ?>
```

## real world use

used for [hanion.dev](https://hanion.dev), source: [github.com/hanion/hanion.github.io](https://github.com/hanion/hanion.github.io)

