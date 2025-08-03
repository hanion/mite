# mite

minimal templated static site generator with embeddable C

## what it does

- renders `.md` files to `.html` using `.mite` templates
- templates are just C
- fast, no dependencies, cross-platform
- outputs plain `.html` into the same folder as the `.md`

## usage

```sh
$ cc -o mite mite.c && ./mite
```

## required files

you only need:

```txt
mite.c         # the program
index.md       # page content
index.mite     # page template
```

everything else is optional

## example structure

```py
.
├── index.md
├── index.mite
├── post/
│   ├── post.mite
│   ├── my-post/
│   │   └── my-post.md
│   └── another-post/
│       └── post.md
└── archive/
    ├── archive.mite
    └── archive.md
```

- if a directory has a `.mite`, it's a template directory
- `.md` files in it and subdirectories under that are treated as pages

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
page->title = "my post title";
page->date  = "2025-12-30";
page->tags  = "math simulation";
---
```

- front matter is just C
- global values like `global.title` are available in templates and posts

## example

used for [hanion.dev](https://hanion.dev), source: [github.com/hanion/hanion.github.io](https://github.com/hanion/hanion.github.io)

