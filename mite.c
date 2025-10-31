/* mite 1.4.1

[mite](https://github.com/hanion/mite)

MInimal TEmplated static site generator with C templates

## what it does
- renders `.md` files to `.html` using `.mite` templates
- templates are just C
- fast, no dependencies, cross-platform
- outputs plain `.html` into the same folder as the `.md`

## usage
$ cc -o mite mite.c && ./mite

## example structure
> This repository matches this structure and includes real working examples

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

## layout and includes
- templates go in `layout/`
- reusable parts go in `include/`
- all `.mite` files in both are globally available
- any of them can call `<? CONTENT() ?>`

to use a layout for a page, set the layout name in its front matter:
	page->layout = "post";

to include a template:
	<? INCLUDE("footer") ?>

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
---
page->layout = "post";
page->title  = "my post title";
page->date   = "2025-12-30";
page->tags   = "math simulation";
---

- front matter is just C
- you can access `page->` and `global.` in templates

## custom data
you can set custom key/value data (`const char *`) using:
PAGE_SET(key, value);
GLOBAL_SET(key, value);

then access it with:
PAGE_GET(key)        // returns char* or NULL
PAGE_HAS(key)        // true if key exists
PAGE_IS(key, value)  // strcmp values

### custom data example
- `post/bezier_curves/bezier_curves.md`
---
page->layout = "post";
page->title  = "Bézier curves";
PAGE_SET("mathjax", "true");
---

- `include/head.mite`
<? if (PAGE_IS("mathjax", "true")) { ?>
	<!-- Load MathJax -->
<? } ?>

## real world use
used for https://hanion.dev
source: https://github.com/hanion/hanion.github.io

 */

#define LAYOUT_DIR "./layout"
#define INCLUDE_DIR "./include"
#define DEFAULT_PAGE_LAYOUT "default"

#ifndef _WIN32
	#define _DEFAULT_SOURCE
	#include <sys/stat.h>
	#include <dirent.h>
	#include <unistd.h>
	#include <signal.h>
	#include <sys/types.h>
#else
	#include <windows.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_PATH_LEN 1024

#define da_reserve(da, expected_capacity)                                                          \
	do {                                                                                           \
		if ((expected_capacity) > (da)->capacity) {                                                \
			if ((da)->capacity == 0) {                                                             \
				(da)->capacity = 1280;                                                             \
			}                                                                                      \
			while ((expected_capacity) > (da)->capacity) {                                         \
				(da)->capacity *= 2;                                                               \
			}                                                                                      \
			(da)->items = realloc((da)->items, (da)->capacity * sizeof(*(da)->items));             \
			assert((da)->items != NULL);                                                           \
		}                                                                                          \
	} while (0)

#define da_append(da, item)                                                                        \
	do {                                                                                           \
		da_reserve((da), (da)->count + 1);                                                         \
		(da)->items[(da)->count++] = (item);                                                       \
	} while (0)

#define da_append_many(da, new_items, new_items_count)                                             \
	do {                                                                                           \
		da_reserve((da), (da)->count + (new_items_count));                                         \
		memcpy((da)->items + (da)->count, (new_items), (new_items_count) * sizeof(*(da)->items));  \
		(da)->count += (new_items_count);                                                          \
	} while (0)

#define da_append_cstr(da, cstr) da_append_many((da), (cstr), strlen((cstr)))
#define da_append_sv(da, sv) da_append_many((da), (sv)->items, (sv)->count)
#define SB_TO_SV(sb) (StringView){ .items=(sb)->items, .count=(sb)->count }
#define SV_ARG(sv) (int)((sv)->count), ((sv)->items)

typedef struct {
	char* items;
	size_t count;
	size_t capacity;
} StringBuilder;

typedef struct {
	char* items;
	size_t count;
} StringView;

bool read_entire_file(const char* filepath_cstr, StringBuilder* sb) {
	if (!filepath_cstr || !sb) return false;

	FILE* f = fopen(filepath_cstr, "rb");
	if (f == NULL) {
		printf("Could not open file %s: %s\n", filepath_cstr, strerror(errno));
		return false;
	}

	if (fseek(f, 0, SEEK_END) < 0) { fclose(f); return false; }

#ifndef _WIN32
	long m = ftell(f);
#else
	__int64 m = _ftelli64(f);
#endif

	if (m < 0)                     { fclose(f); return false; }
	if (fseek(f, 0, SEEK_SET) < 0) { fclose(f); return false; }

	size_t file_size = (size_t)m;

	size_t needed_capacity = sb->count + file_size;
	if (needed_capacity > sb->capacity) {
		char* new_items = realloc(sb->items, needed_capacity);
		if (!new_items) {
			fclose(f);
			return false;
		}
		sb->items = new_items;
		sb->capacity = needed_capacity;
	}

	size_t read_bytes = fread(sb->items + sb->count, 1, file_size, f);
	if (read_bytes != file_size) {
		if (ferror(f)) {
			printf("Error reading file %s: %s\n", filepath_cstr, strerror(errno));
		}
		fclose(f);
		return false;
	}

	sb->count += read_bytes;

	fclose(f);
	return true;
}

bool write_to_file(const char* filepath_cstr, StringBuilder* sb) {
	if (!filepath_cstr || !sb) return false;

	FILE* f = fopen(filepath_cstr, "wb");
	if (f == NULL) {
		printf("Could not open file %s for writing: %s\n", filepath_cstr, strerror(errno));
		return false;
	}

	size_t written = fwrite(sb->items, 1, sb->count, f);
	if (written != sb->count) {
		printf("Error writing to file %s: %s\n", filepath_cstr, strerror(errno));
		fclose(f);
		return false;
	}

	fclose(f);
	return true;
}

#ifdef SECOND_STAGE

static char temp_sprintf_buf[16] = {0};
void unusedtemp() { (void)temp_sprintf_buf; }
#define OUT_HTML(buf, size) da_append_many(out, buf, size);
#define INT(x) do { if (sprintf(temp_sprintf_buf, "%d", (x))) da_append_cstr(out, temp_sprintf_buf); } while (0);

#define RAWSTR2(x) #x
#define RAWSTR(x) da_append_cstr(out, RAWSTR2(x));
#define CSTR(x) if ((x)) da_append_cstr(out, (x));
#define SVP(x) da_append_many(out, (x)->items, (x)->count);
#define SV(x) SVP(&(x))
#define STR(x) CSTR((x))

#define CONTENT() render_content_func(out, page);
#define INCLUDE(mitename) do { SiteTemplate* st = find_template(&global.templates, (mitename)); \
							if (st && st->is_include) st->function(out, page, render_content_func); } while(0);

typedef struct {
	const char* key;
	const char* value;
} SiteMapEntry;

typedef struct {
	SiteMapEntry* items;
	size_t count;
	size_t capacity;
} SiteMap;

typedef struct {
	const char* title;
	union {
		const char* description;
		const char* desc;
	};
	const char* url;
	const char* date;
	const char* tags;
	const char* layout;
	const char* output;
	const char* input;
	SiteMap data;
} SitePage;

typedef struct {
	SitePage** items;
	size_t count;
	size_t capacity;
} SitePages;


typedef void (*render_content_func_t) (StringBuilder* out, SitePage* page);
typedef void (*render_template_func_t)(StringBuilder* out, SitePage* page, render_content_func_t render_content_func);

typedef struct {
	const char* name;
	render_template_func_t function;
	bool is_include;
} SiteTemplate;

typedef struct {
	SiteTemplate* items;
	size_t count;
	size_t capacity;
} SiteTemplates;


typedef struct {
	const char* title;
	const char* description;
	const char* url;
	const char* favicon_path;
	SitePages pages;
	SiteTemplates templates;

	// custom data
	SitePages posts;
	SitePages projects;
	SitePages socials;
	SiteMap data;
} SiteGlobal;



SitePage* find_page(SitePages* pages, const char* input_file) {
	if (!input_file) return NULL;
	for (size_t i = 0; i < pages->count; ++i) {
		if (!pages->items[i]) continue;
		if (!pages->items[i]->url) continue;
		if (0 == strcmp(pages->items[i]->input, input_file)) return pages->items[i];
	}
	return NULL;
}
SiteTemplate* find_template(SiteTemplates* templates, const char* name) {
	if (!name) return NULL;
	for (size_t i = 0; i < templates->count; ++i) {
		if (!templates->items[i].name) continue;
		if (0 == strcmp(templates->items[i].name, name)) return &templates->items[i];
	}
	fprintf(stderr,"[error] template '%s' not found!\n", name);
	exit(1);
	return NULL;
}

SitePage* site_page_new() {
	return calloc(1, sizeof(SitePage));
}
SitePage* site_page_new_tdu(const char* title, const char* desc, const char* url) {
	SitePage* p = site_page_new();
	p->title = title;
	p->description = desc;
	p->url = url;
	p->layout = DEFAULT_PAGE_LAYOUT;
	return p;
}

#define ADD_PROJECT(t, d, u) da_append(&global.projects, site_page_new_tdu((t),(d),(u)));
#define ADD_SOCIAL(t, u)     da_append(&global.socials,  site_page_new_tdu((t),NULL,(u)));

#define ADD_TO_GLOBAL_POSTS(page) da_append(&global.posts, (page));
#define SET_POST()    da_append(&global.posts,    (page));
#define SET_PROJECT() da_append(&global.projects, (page));



void site_map_set(SiteMap* map, const char* key, const char* value) {
	if (map->count == map->capacity) {
		map->capacity = map->capacity ? map->capacity * 2 : 8;
		map->items = realloc(map->items, map->capacity * sizeof(SiteMapEntry));
	}
	map->items[map->count].key = key;
	map->items[map->count].value = value;
	map->count++;
}
const char* site_map_get(SiteMap* map, const char* key) {
	for (size_t i = 0; i < map->count; ++i) {
		if (strcmp(map->items[i].key, key) == 0) {
			return map->items[i].value;
		}
	}
	return NULL;
}

bool site_map_has(SiteMap* map, const char* key) {
	for (size_t i = 0; i < map->count; ++i) {
		if (strcmp(map->items[i].key, key) == 0) return true;
	}
	return false;
}

bool site_map_equals(SiteMap* map, const char* key, const char* value) {
	for (size_t i = 0; i < map->count; ++i) {
		if (strcmp(map->items[i].key, key) == 0) {
			return strcmp(map->items[i].value, value) == 0;
		}
	}
	return false;
}



#define DATA_SET(obj, key, value) site_map_set(&((obj)->data), (key), (value));
#define DATA_GET(obj, key)        site_map_get(&((obj)->data), (key))
#define DATA_HAS(obj, key)        site_map_has(&((obj)->data), (key))
#define DATA_IS(obj, key, value)  site_map_equals(&((obj)->data), (key), (value))

#define GLOBAL_SET(key, value) DATA_SET(&global, key, value)
#define GLOBAL_GET(key)        DATA_GET(&global, key)
#define GLOBAL_HAS(key)        DATA_HAS(&global, key)
#define GLOBAL_IS(key, value)  DATA_IS (&global, key, value)

#define PAGE_SET(key, value) DATA_SET(page, key, value)
#define PAGE_GET(key)        DATA_GET(page, key)
#define PAGE_HAS(key)        DATA_HAS(page, key)
#define PAGE_IS(key, value)  DATA_IS (page, key, value)

void sort_pages(SitePages* sp) {
	for (size_t i = 0; i < sp->count; ++i) {
		for (size_t j = i + 1; j < sp->count; ++j) {
			if (!sp->items[i] || !sp->items[j]) continue;
			if (!sp->items[i]->date || !sp->items[j]->date) continue;
			if (strcmp(sp->items[i]->date, sp->items[j]->date) < 0) {
				SitePage* tmp = sp->items[i];
				sp->items[i] = sp->items[j];
				sp->items[j] = tmp;
			}
		}
	}
}


static int compare_ddmmyyyy(const char* a, const char* b) {
	if (!a || !b) return 0;
	int da, ma, ya;
	int db, mb, yb;
	if (sscanf(a, "%d/%d/%d", &da, &ma, &ya) != 3) return 0;
	if (sscanf(b, "%d/%d/%d", &db, &mb, &yb) != 3) return 0;
	if (ya != yb) return (ya > yb) ? -1 : 1;
	if (ma != mb) return (ma > mb) ? -1 : 1;
	if (da != db) return (da > db) ? -1 : 1;
	return 0;
}

void sort_pages_alt(SitePages* sp) {
	for (size_t i = 0; i < sp->count; ++i) {
		for (size_t j = i + 1; j < sp->count; ++j) {
			if (!sp->items[i] || !sp->items[j]) continue;
			if (!sp->items[i]->date || !sp->items[j]->date) continue;
			if (compare_ddmmyyyy(sp->items[i]->date, sp->items[j]->date) > 0) {
				SitePage* tmp = sp->items[i];
				sp->items[i] = sp->items[j];
				sp->items[j] = tmp;
			}
		}
	}
}

char* format_rfc822(const char *ymd) {
	char* out = calloc(64, sizeof(char));
	struct tm t = {0};
	sscanf(ymd, "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday);
	t.tm_year -= 1900;
	t.tm_mon  -= 1;
	t.tm_hour = 0;
	t.tm_min = 0;
	t.tm_sec = 0;

	mktime(&t);

	strftime(out, 64, "%a, %d %b %Y %H:%M:%S +0000", &t);
	return out;
}





#else // #ifdef SECOND_STAGE





bool file_exists(const char* path) {
#ifndef _WIN32
	return access(path, F_OK) == 0;
#else
	DWORD attrs = GetFileAttributesA(path);
	return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#endif
}

static inline int execute_line(const char* line) {
#ifndef _WIN32
	return system(line);
#else
	#define CMD_LINE_MAX 2000
	char full_command[CMD_LINE_MAX + 16];
	snprintf(full_command, sizeof(full_command), "cmd /C \"%s\"", line);
	return system(full_command);
#endif
}


const char* get_mite_binary_path() {
	if (file_exists("./mite")) {
		return "./mite";
	} else if (file_exists("/usr/bin/mite")) {
		return "/usr/bin/mite";
	}
	return NULL;
}


#ifndef _WIN32
static pid_t g_watcher_pid = -1;
#else
static HANDLE g_watcher_proc = NULL;
#endif

void start_watcher() {
#ifndef _WIN32
	g_watcher_pid = fork();
	if (g_watcher_pid == 0) {
		execl(get_mite_binary_path(), "mite", "--watch", NULL);
		_exit(1);
	}
#else
	PROCESS_INFORMATION pi;
	STARTUPINFO si = {0};
	si.cb = sizeof(si);

	if (CreateProcess(NULL, "mite.exe --watch", NULL, NULL, FALSE,
				   CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
		g_watcher_proc = pi.hProcess;  // store HANDLE
		CloseHandle(pi.hThread);       // thread handle not needed
	}
#endif
}

void watch() {
#ifndef _WIN32
	execl(get_mite_binary_path(), "mite", "--incremental", NULL);
	sleep(1);
#else
	execute_line("mite.exe --incremental");
	Sleep(1000);
#endif
}

void stop_watcher() {
#ifndef _WIN32
	if (g_watcher_pid > 0) {
		kill(g_watcher_pid, SIGTERM);
		g_watcher_pid = -1;
	}
#else
	if (g_watcher_proc) {
		TerminateProcess(g_watcher_proc, 0);
		CloseHandle(g_watcher_proc);
		g_watcher_proc = NULL;
	}
#endif
}

static inline int build_and_run_site() {
#ifndef _WIN32
	return execute_line("cc -o site site.c && ./site");
#else
	return execute_line("gcc -o site.exe site.c && site.exe");
#endif
}

static inline void cleanup_site() {
#ifndef _WIN32
	remove("site.c");
	remove("site");
#else
	DeleteFileA("site.c");
	DeleteFileA("site.exe");
#endif
}




// ------------------- md2html --------------------------
typedef struct {
	StringBuilder* out;
	const char* cursor;
	bool in_paragraph;
	bool in_list;
} MdRenderer;

void da_append_escape_html(StringBuilder* out, const char* in, size_t count) {
	for (size_t i = 0; i < count; ++i) {
		switch ((unsigned char)in[i]) {
			case '<':  da_append_cstr(out, "&lt;");   break;
			case '>':  da_append_cstr(out, "&gt;");   break;
			case '&':  da_append_cstr(out, "&amp;");  break;
			case '\'': da_append_cstr(out, "&#39;");  break;
			case '"':  da_append_cstr(out, "&quot;"); break;
			default:   da_append(out, in[i]);         break;
		}
	}
}

const char* search_str_until_newline(const char* haystack, const char* needle) {
	if (!haystack) return NULL;
	if (!*needle) return haystack;

	const char* hay = haystack;
	const char* ndl = needle;

	while (*hay && *hay != '\n') {
		const char* h_sub = hay;
		const char* n_sub = ndl;

		while (*n_sub) {
			if (*h_sub == '\n' || *h_sub == '\0') break;
			if (*h_sub != *n_sub) break;
			h_sub++;
			n_sub++;
		}
		if (*n_sub == '\0') return hay;

		hay++;
	}

	return NULL;
}

size_t sv_strstr(StringView haystack, StringView needle) {
	if (needle.count == 0) return 0;
	if (haystack.count < needle.count) return haystack.count;

	for (size_t i = 0; i <= haystack.count - needle.count; ++i) {
		if (memcmp(haystack.items + i, needle.items, needle.count) == 0) {
			return i;
		}
	}
	return haystack.count;
}

bool starts_with(const char* line, const char* prefix) {
	return strncmp(line, prefix, strlen(prefix)) == 0;
}
bool word_starts_with(const char* line, const char* prefix) {
	size_t len = strlen(prefix);
	return strncmp(line, prefix, len) == 0 && (*(line+len) != ' ');
}
const char* word_ends_with(const char* line, const char* prefix) {
	const char* end = search_str_until_newline(line, prefix);
	if (end && (*(end-1) == ' ')) return NULL;
	return end;
}

void parse_inline(MdRenderer* r, const char* line) {

#define PARSE_INLINE_TAG(start, end, html_start, html_end)             \
	else if (word_starts_with(p, start)) {                             \
		size_t start_len = sizeof(start) - 1;                          \
		size_t end_len   = sizeof(end) - 1;                            \
		const char* tag_end = word_ends_with(p + start_len, end);      \
		if (tag_end) {                                                 \
			p += start_len;                                            \
			da_append_cstr(r->out, html_start);                        \
			da_append_escape_html(r->out, p, tag_end - p);             \
			da_append_cstr(r->out, html_end);                          \
			p = tag_end + end_len;                                     \
			continue;                                                  \
		}                                                              \
	}

	const char* p = line;
	while (*p && (*p != '\r') && (*p != '\n')) {
		// double space line break
		if (starts_with(p, "  \n") || starts_with(p, "  \r\n")) {
			da_append_cstr(r->out, "<br>\n");
			p += 3;
			break;
		}

		if (false) {}
		PARSE_INLINE_TAG("***", "***", "<strong><i>", "</i></strong>")
		PARSE_INLINE_TAG("**_", "_**", "<strong><i>", "</i></strong>")
		PARSE_INLINE_TAG("_**", "**_", "<strong><i>", "</i></strong>")
		PARSE_INLINE_TAG("**", "**", "<strong>", "</strong>")
		PARSE_INLINE_TAG("*", "*", "<i>", "</i>")
		PARSE_INLINE_TAG("_", "_", "<i>", "</i>")
		PARSE_INLINE_TAG("`", "`", "<code>", "</code>")
		PARSE_INLINE_TAG("\\(", "\\)", "\\(", "\\)")
		else if (*p == '[') {
			const char* end_text = search_str_until_newline(p, "]");
			if (!end_text || end_text[1] != '(') {
				da_append_escape_html(r->out, p, 1);
				++p;
				continue;
			}
			const char* end_url = search_str_until_newline(end_text + 2, ")");
			if (!end_url) break;
			da_append_cstr(r->out, "<a href=\"");
			da_append_many(r->out, end_text + 2, end_url - (end_text + 2));
			da_append_cstr(r->out, "\">");
			da_append_escape_html(r->out, p + 1, end_text - (p + 1));
			da_append_cstr(r->out, "</a>");
			p = end_url + 1;
			continue;
		}
		// figure
		else if (starts_with(p, "![")) {
			const char* end_text = search_str_until_newline(p+2, "]");
			if (!end_text || end_text[1] != '(') {
				da_append_many(r->out, p, 2);
				p += 2;
				continue;
			}
			const char* end_url = search_str_until_newline(end_text + 2, ")");
			if (!end_url) break;

			const char* format = end_url;
			while (format > end_text && *(format-1) != '.') --format;

			const size_t format_len = end_url - format;
			bool video = false;
			if (format_len == 3 && 0 == strncmp(format, "mp4",  3)) video = true;
			if (format_len == 4 && 0 == strncmp(format, "webm", 4)) video = true;

			const char* const start_text = p + 2;
			const char* const start_url  = end_text + 2;
			if (video) {
				da_append_cstr(r->out, "<figure>\n\t<video autoplay controls muted loop playsinline width=\"100%\">\n\t\t<source src=\"");
				da_append_many(r->out, start_url, end_url - start_url);
				da_append_cstr(r->out, "\" type=\"video/");
				da_append_many(r->out, format, format_len);
				da_append_cstr(r->out, "\" alt=\"");
				da_append_escape_html(r->out, start_text, end_text - start_text);
				da_append_cstr(r->out, "\">\n\t</video>\n\t<figcaption>");
				da_append_many(r->out, start_text, end_text - start_text);
				da_append_cstr(r->out, "\n\t</figcaption>\n</figure>\n");
			} else {
				da_append_cstr(r->out, "<figure>\n\t<img src=\"");
				da_append_many(r->out, start_url, end_url - start_url);
				da_append_cstr(r->out, "\" loading=\"lazy\" alt=\"");
				da_append_escape_html(r->out, start_text, end_text - start_text);
				da_append_cstr(r->out, "\">\n\t<figcaption>");
				da_append_many(r->out, start_text, end_text - start_text);
				da_append_cstr(r->out, "</figcaption>\n</figure>\n");
			}
			p = end_url + 1;
			continue;
		}
		else if (starts_with(p, "<?")) {
			const char* tag_end = strstr(p + 2, "?>");
			if (tag_end) {
				da_append_many(r->out, p, tag_end - p + 2);
				p = tag_end + 2;
				r->cursor = p;
				continue;
			}
		}

		da_append(r->out, *p);
		++p;
	}
}

// includes newline
void append_until_newline(StringBuilder* sb, const char* str) {
	size_t count = 0;
	const char* cursor = str;
	while (*cursor && *cursor != '\n') {
		count++;
		cursor++;
	}
	da_append_many(sb, str, count + 1);
}
void skip_after_newline(const char** cursor) {
	while (**cursor && **cursor != '\n') {
		(*cursor)++;
	}
	if (**cursor == '\n') {
		(*cursor)++;
	}
}

void render_md_to_html(StringBuilder* md, StringBuilder* out, StringBuilder* out_fm) {
	MdRenderer r = { .cursor = md->items, .out = out };

#define start_paragraph() if (!r.in_paragraph) { da_append_cstr(out,  "\n<p>\n"); r.in_paragraph = true; }
#define   end_paragraph() if (r.in_paragraph)  { da_append_cstr(out, "</p>\n"); r.in_paragraph = false; }
#define start_list() if (!r.in_list) { da_append_cstr(out,  "<ul>\n"); r.in_list = true; }
#define   end_list() if (r.in_list)  { da_append_cstr(out, "</ul>\n"); r.in_list = false; }

	while (*r.cursor) {
		const char* line_end = r.cursor;
		while (*line_end && *line_end != '\n') line_end++;

		const char* trimmed = r.cursor;
		while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\r') trimmed++;

		// empty line ends paragraph
		if (line_end - trimmed == 0) {
			end_paragraph();
			end_list();

		} else if (starts_with(trimmed, "<?")) {
			const char* end = strstr(trimmed + 2, "?>");
			if (end) {
				da_append_many(out, trimmed, end - trimmed + 2);
				r.cursor = end + 2;
				continue;
			}
		
		} else if (starts_with(trimmed, "---")) {
			if (trimmed != md->items) {
				da_append_cstr(out, "<hr>");
				r.cursor = trimmed + 3;
				continue;
			}

			// frontmatter
			const char* end = strstr(trimmed + 3, "---");
			if (end) {
				trimmed += 3;
				da_append_cstr(out_fm, "<?");
				da_append_many(out_fm, trimmed, end - trimmed);
				da_append_cstr(out_fm, "?>\n");
				r.cursor = end + 3;
				continue;
			}

		// HTML passthrough
		} else if (*trimmed == '<') {
			end_paragraph();
			end_list();

			const char* html_end_start = search_str_until_newline(trimmed, "</");
			const char* html_end_end   = search_str_until_newline(html_end_start, ">");
			if (!html_end_start || !html_end_end) {
				append_until_newline(out, trimmed);
			} else {
				html_end_end++;
				da_append_many(out, trimmed, html_end_end - trimmed);
				r.cursor = html_end_end;
				parse_inline(&r, html_end_end);
			}

		} else if (*trimmed == '#') {
			end_paragraph();
			end_list();

			int level = 0;
			while (*trimmed == '#') { level++; trimmed++; }
			while (*trimmed == ' ') trimmed++;

			char tag[16];
			sprintf(tag, "h%d", level);
			da_append_cstr(out, "\n<"); da_append_cstr(out, tag); da_append(out, '>');
			parse_inline(&r, trimmed);
			da_append_cstr(out, "</"); da_append_cstr(out, tag); da_append_cstr(out, ">\n");

		} else if (starts_with(trimmed, "- [ ] ")) {
			end_paragraph();
			end_list();
			da_append_cstr(out, "<ul><li><input type=\"checkbox\" disabled>");
			parse_inline(&r, trimmed + 6);
			da_append_cstr(out, "</li></ul>\n");

		} else if (starts_with(trimmed, "- ") || starts_with(trimmed, "* ")) {
			end_paragraph();
			start_list();
			da_append_cstr(out, "<li>");
			parse_inline(&r, trimmed + 2);
			da_append_cstr(out, "</li>\n");

		} else if (starts_with(trimmed, "> ")) {
			end_paragraph();
			end_list();
			da_append_cstr(out, "<blockquote>");
			parse_inline(&r, trimmed + 2);
			da_append_cstr(out, "</blockquote>\n");

		} else if (starts_with(trimmed, "```")) {
			if (trimmed == md->items) {
				// frontmatter
				const char* end = strstr(trimmed + 3, "```");
				if (end) {
					skip_after_newline(&trimmed);
					da_append_cstr(out_fm, "<?");
					da_append_many(out_fm, trimmed, end - trimmed);
					da_append_cstr(out_fm, "?>\n");
					r.cursor = end + 3;
					continue;
				}
			}

			end_paragraph();
			end_list();

			const char* code_end = strstr(trimmed + 3, "```");
			if (!code_end) code_end = md->items + md->count;

			skip_after_newline(&trimmed); // skip language
			da_append_cstr(out, "<pre><code>\n");
			da_append_escape_html(out, trimmed, code_end - trimmed);
			da_append_cstr(out, "</code></pre>\n");

			r.cursor = code_end + 3;
			continue;

		} else if (starts_with(trimmed, "![")) {
			// figure
			end_paragraph();
			parse_inline(&r, trimmed);

		} else {
			end_list();
			start_paragraph();
			parse_inline(&r, trimmed);
			da_append(out, '\n');
		}

		if (r.cursor > line_end) continue;
		r.cursor = (*line_end == '\n') ? line_end + 1 : line_end;
	}

	end_paragraph();
	end_list();

	if (out->count)    da_append(out, '\0');
	if (out_fm->count) da_append(out_fm, '\0');

#undef start_paragraph
#undef   end_paragraph
#undef start_list
#undef   end_list
}


// ------------------- /md2html/ ------------------------


typedef struct {
	char* name;
	char* path;
	StringBuilder rendered_code;
	bool is_include;
} MiteTemplate;

typedef struct {
	char* name;
	char* md_path;
	char* final_html_path;

	StringBuilder rendered_code;
	StringBuilder front_matter;
} MitePage;

typedef struct {
	MitePage* items;
	size_t count;
	size_t capacity;
} MitePages;

typedef struct {
	MiteTemplate* items;
	size_t count;
	size_t capacity;
} MiteTemplates;


static inline StringView sv_trim(StringView input) {
	StringView sv = input;

	int l = 0;
	while (l < sv.count && (sv.items[l] == ' ' || sv.items[l] == '\t' || sv.items[l] == '\n' || sv.items[l] == '\r')) l++;

	sv.items += l;
	sv.count -= l;

	int r = sv.count - 1;
	while (r >= 0 && (sv.items[r] == ' ' || sv.items[r] == '\t' || sv.items[r] == '\n' || sv.items[r] == '\r')) r--;

	sv.count = r + 1;

	return sv;
}
static inline StringView sv_trim_empty_lines(StringView input) {
	StringView sv = input;

	int empty = 0;
	while (empty < sv.count && (sv.items[empty] == ' ' || sv.items[empty] == '\t')) empty++;

	if (empty+1 < sv.count && sv.items[empty+1] == '\n') {
		sv.items += empty;
		sv.count -= empty;
	}

	int r = sv.count - 1;
	while (r >= 0 && (sv.items[r] == ' ' || sv.items[r] == '\t' || sv.items[r] == '\n' || sv.items[r] == '\r')) r--;
	sv.count = r + 1;

	return sv;
}

static inline StringView chop_until(StringView* input, const char* delim, const size_t delim_count) {
    StringView line = {0};
    if (!input || !delim)  return line;
    if (input->count == 0) return line;
    if (delim_count == 0)  return line;
    if (input->count < delim_count) return line;

	size_t i = 0;
	while (i < input->count) {
		size_t equal = 0;
		for (size_t d = 0; d < delim_count; ++d) {
			if (i+d >= input->count) break;
			if (input->items[i+d] == delim[d]) equal++;
		}
		if (equal == delim_count) break;
		++i;
	}

	line.items = input->items;
	line.count = i;

	if (i == input->count) {
		input->items += input->count;
		input->count = 0;
	} else {
		input->items += i + delim_count;
		input->count -= i + delim_count;
	}

	return line;
}


void sv_to_c_code(StringView sv, StringBuilder* out) {
	da_append_many(out, sv.items, sv.count);
	da_append(out, '\n');
}

typedef struct {
	StringBuilder string;
	size_t count;
} ByteArray;

void byte_array_to_c_code(ByteArray* ba, StringBuilder* out) {
	if (ba->count == 0) return;
	if (ba->count == 1 && ba->string.count == 4) {
		if (0 == strncmp(ba->string.items, "\\x0a", 4)) return;
	}
	da_append_cstr(out, "OUT_HTML(\"");
	static char buffer[16] = {0};
	da_append_many(out, ba->string.items, ba->string.count);
	sprintf(buffer, "\", %d)\n", (int)ba->count);
	da_append_cstr(out, buffer);
}

char to_hex_char(uint8_t n) {
	return (n < 10) ? ('0' + n) : ('a' + n - 10);
}

void sv_to_byte_array(StringView sv, ByteArray* out) {
	char buffer[4] = { '\\', 'x', 0, 0 };
	for (uint64_t i = 0; i < sv.count; ++i) {
		uint8_t c = (uint8_t)sv.items[i];
		if (c == '\0') break;
		buffer[2] = to_hex_char(c >> 4);
		buffer[3] = to_hex_char(c & 0xF);
		da_append_many(&out->string, buffer, 4);
		out->count++;
	}
}


// NOTE: source must be null terminated
void render_html_to_c(StringView source, StringBuilder* out) {
	bool html_mode = true;
	ByteArray ba = {0};
	while (source.count && source.items[0]) {
		if (html_mode) {
			StringView token = sv_trim_empty_lines(chop_until(&source, "<?", 2));
			sv_to_byte_array(token, &ba);
			byte_array_to_c_code(&ba, out);
			ba.count = 0;
			ba.string.count = 0;
		} else {
			StringView token = sv_trim(chop_until(&source, "?>", 2));
			sv_to_c_code(token, out);
		}
		html_mode = !html_mode;
	}
	free(ba.string.items);
}

bool render_mite_layout(MiteTemplate* mite) {
	if (mite->rendered_code.count > 0) return true;

	StringBuilder tmpl = {0};
	if (!read_entire_file(mite->path, &tmpl)) return false;

	render_html_to_c(SB_TO_SV(&tmpl), &mite->rendered_code);

	free(tmpl.items);
	return true;
}

bool render_page(MitePage* mite_page) {
	if (!file_exists(mite_page->md_path)) return false;

	StringBuilder md = {0};
	if (!read_entire_file(mite_page->md_path, &md)) return false;
	da_append(&md, '\0');

	StringBuilder raw_html = {0};
	StringBuilder raw_fm = {0};
	render_md_to_html(&md, &raw_html, &raw_fm);

	if (raw_fm.count == 0) {
		printf("[warning] page does not have any front matter! '%s'\n", mite_page->md_path+2);
	}

	render_html_to_c(SB_TO_SV(&raw_html), &mite_page->rendered_code);
	render_html_to_c(SB_TO_SV(&raw_fm), &mite_page->front_matter);

	free(md.items);
	free(raw_html.items);
	free(raw_fm.items);

	return true;
}

void render_all(MitePages* pages, MiteTemplates* templates) {
	for (size_t i = 0; i < templates->count; ++i) {
		MiteTemplate* mt = &templates->items[i];
		printf("[mite] %s\n", mt->path+2);
		if (!render_mite_layout(mt)) {
			printf("failed to render mite layout: %s\n", mt->name);
			exit(1);
		}
	}
	for (size_t i = 0; i < pages->count; ++i) {
		MitePage* mp = &pages->items[i];
		printf("[page] %s\n", mp->md_path+2);
		if (!render_page(mp)) {
			printf("failed to render page: %s\n", mp->name);
			exit(1);
		}
	}
}


bool ends_with(const char* name, const char* ext) {
	size_t len = strlen(name);
	size_t ext_len = strlen(ext);
	if (len < ext_len) return false;
	return strcmp(name + len - ext_len, ext) == 0;
}

int is_mite_file(const char* name) {
	return ends_with(name, ".mite");
}

int is_md_file(const char* name) {
	return ends_with(name, ".md");
}

void join_path(char* out, const char* a, const char* b) {
	snprintf(out, MAX_PATH_LEN*2, "%s/%s", a, b);
}


extern char* strdup(const char*);
void register_mite_file(MiteTemplates* templates, const char* mite_dir, const char* mite_name, bool is_include) {
	da_append(templates, (MiteTemplate){0});
	MiteTemplate* mt = &templates->items[templates->count-1];

	mt->path = calloc(MAX_PATH_LEN, sizeof(mite_dir));
	join_path(mt->path, mite_dir, mite_name);

	mt->name = strdup(mite_name);
	size_t len = strlen(mt->name);
	if (len > 5 && strcmp(mt->name + len - 5, ".mite") == 0) {
		mt->name[len-5] = '\0';
	}

	mt->is_include = is_include;
}
void register_md_file(MitePages* pages, const char* md_dir, const char* md_name) {
	da_append(pages, (MitePage){0});
	MitePage* mp = &pages->items[pages->count-1];

	mp->md_path = calloc(MAX_PATH_LEN, sizeof(md_dir));
	join_path(mp->md_path, md_dir, md_name);

	mp->name = strdup(mp->md_path+2);
	size_t len = strlen(mp->name);
	if (len > 3 && strcmp(mp->name + len - 3, ".md") == 0) {
		len -= 3;
	}
	mp->name[len] = '\0';
	for (size_t i = 0; i < len; ++i) {
		if (!isalnum(mp->name[i])) {
			mp->name[i] = '_';
		}
	}

	mp->final_html_path = calloc(MAX_PATH_LEN, sizeof(md_dir));
	join_path(mp->final_html_path, md_dir, "index.html");
}


void search_files(MitePages* pages, MiteTemplates* templates) {
#ifndef _WIN32
	DIR* root = opendir(".");
	if (!root) return;

	struct dirent* entry;

	while ((entry = readdir(root)) != NULL) {
		if (entry->d_type != DT_DIR && entry->d_type != DT_REG) continue;
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

		// index.md
		if (entry->d_type == DT_REG) {
			if (0 == strcmp(entry->d_name, "index.md") || 0 == strcmp(entry->d_name, "rss.md")) {
				register_md_file(pages, ".", entry->d_name);
			}
			continue;
		}

		if (entry->d_type != DT_DIR) continue;

		int dir_type = 1;
		if (strcmp(entry->d_name, "layout") == 0)  dir_type = 2;
		if (strcmp(entry->d_name, "include") == 0) dir_type = 3;

		char subdir_path[MAX_PATH_LEN];
		join_path(subdir_path, ".", entry->d_name);

		DIR* subdir = opendir(subdir_path);
		if (!subdir) continue;

		struct dirent* subentry;
		while ((subentry = readdir(subdir)) != NULL) {
			if (subentry->d_type != DT_REG && subentry->d_type != DT_DIR) continue;
			if (strcmp(subentry->d_name, ".") == 0 || strcmp(subentry->d_name, "..") == 0) continue;

			char path[MAX_PATH_LEN];
			join_path(path, subdir_path, subentry->d_name);

			if (subentry->d_type == DT_REG) {
				if (dir_type == 1 && is_md_file(subentry->d_name)) register_md_file(pages, subdir_path, subentry->d_name);
				if (dir_type > 1 && is_mite_file(subentry->d_name)) register_mite_file(templates, subdir_path, subentry->d_name, dir_type == 3);
			}

			// content
			if (dir_type == 1 && subentry->d_type == DT_DIR) {
				DIR* subsub = opendir(path);
				if (!subsub) continue;
				struct dirent* f;
				while ((f = readdir(subsub)) != NULL) {
					if (f->d_type != DT_REG) continue;
					if (is_md_file(f->d_name)) {
						register_md_file(pages, path, f->d_name);
					}
				}
				closedir(subsub);
			}
		}
		closedir(subdir);
	}
	closedir(root);
#else
	WIN32_FIND_DATAA find_data;
	HANDLE h_find = FindFirstFileA(".\\*", &find_data);
	if (h_find == INVALID_HANDLE_VALUE) return;

	do {
		const char* name = find_data.cFileName;
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

		// index.md
		if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && (0 == strcmp(name, "index.md"))) {
			register_md_file(pages, ".", "index.md");
			continue;
		}

		if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;

		int dir_type = 1;
		if (strcmp(name, "layout") == 0)  dir_type = 2;
		if (strcmp(name, "include") == 0) dir_type = 3;

		char subdir_path[MAX_PATH];
		join_path(subdir_path, ".", name);

		char sub_search[MAX_PATH];
		join_path(sub_search, subdir_path, "*");

		WIN32_FIND_DATAA subentry;
		HANDLE h_sub = FindFirstFileA(sub_search, &subentry);
		if (h_sub == INVALID_HANDLE_VALUE) continue;

		do {
			const char* subname = subentry.cFileName;
			if (strcmp(subname, ".") == 0 || strcmp(subname, "..") == 0) continue;

			char path[MAX_PATH];
			join_path(path, subdir_path, subname);

			if (!(subentry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				if (dir_type == 1 && is_md_file(subname)) register_md_file(pages, subdir_path, subname);
				if (dir_type > 1 && is_mite_file(subname)) register_mite_file(templates, subdir_path, subname, dir_type == 3);
			}

			// content
			if (dir_type == 1 && (subentry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				char subsub_path[MAX_PATH];
				join_path(subsub_path, subdir_path, subname);

				char subsub_search[MAX_PATH];
				join_path(subsub_search, subsub_path, "*");

				WIN32_FIND_DATAA f;
				HANDLE h_f = FindFirstFileA(subsub_search, &f);
				if (h_f != INVALID_HANDLE_VALUE) {
					do {
						if (!(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && is_md_file(f.cFileName)) {
							register_md_file(pages, subsub_path, f.cFileName);
						}
					} while (FindNextFileA(h_f, &f));
					FindClose(h_f);
				}
			}
		} while (FindNextFileA(h_sub, &subentry));
		FindClose(h_sub);
	} while (FindNextFileA(h_find, &find_data));
	FindClose(h_find);
#endif
}


void second_stage_include_header(StringBuilder* out, const char* source_path) {
	da_append_cstr(out, "#define SECOND_STAGE\n");
	da_append_cstr(out, "#include \"");
	da_append_cstr(out, source_path);
	da_append_cstr(out, "\"\n\n");
}

void second_stage_codegen(StringBuilder* out, MitePages* pages, MiteTemplates* templates) {
	da_append_cstr(out,
		"SiteGlobal global = { .title = \"!!!global!title!!!\", .description = \"!!!global!description!!!\" };\n"
	);

	da_append_cstr(out,
		"void construct_global_state(void) {\n"
	);
	for (size_t i = 0; i < pages->count; ++i) {
		MitePage* mp = &pages->items[i];
		da_append_cstr(out, "	{\n");

		da_append_cstr(out, "		SitePage* page = site_page_new_tdu(\"");
		da_append_cstr(out, mp->name);
		da_append_cstr(out, "\", NULL, \"");
		da_append_cstr(out, (mp->final_html_path+1)); // +1 removes the . in the path
		da_append_cstr(out, "\");\n");
		da_append_cstr(out, "		page->output = \"");
		da_append_cstr(out, mp->final_html_path+2); // +2 removes ./
		da_append_cstr(out, "\";\n");
		da_append_cstr(out, "		page->input = \"");
		da_append_cstr(out, mp->md_path);
		da_append_cstr(out, "\";\n");

		da_append_cstr(out, "		da_append(&global.pages, page);\n");

		da_append_sv(out, &mp->front_matter);

		da_append_cstr(out, "	}\n");
	}
	da_append_cstr(out,
		"}\n"
	);

	for (size_t i = 0; i < templates->count; ++i) {
		MiteTemplate* mt = &templates->items[i];

		da_append_cstr(out, "void render_template_");
		da_append_cstr(out, mt->name);
		da_append_cstr(out, "(StringBuilder* out, SitePage* page, render_content_func_t render_content_func) {\n");

		da_append_many(out, mt->rendered_code.items, mt->rendered_code.count);

		da_append_cstr(out, "}\n");
	}


	da_append_cstr(out,
		"void construct_templates(void) {\n"
	);
	for (size_t i = 0; i < templates->count; ++i) {
		MiteTemplate* mt = &templates->items[i];

		da_append_cstr(out,
			"	{\n"
			"		da_append(&global.templates, (SiteTemplate){0});\n"
			"		SiteTemplate* st = &global.templates.items[global.templates.count-1];\n"
			"		st->name = \""
		);
		da_append_cstr(out, mt->name);
		da_append_cstr(out, "\";\n"
			"		st->function = render_template_"
		);
		da_append_cstr(out, mt->name);
		da_append_cstr(out, ";\n"
			"		st->is_include = "
		);
		da_append_cstr(out, mt->is_include ? "1": "0");
		da_append_cstr(out, ";\n\t}\n");
	}
	da_append_cstr(out,
		"}\n"
	);


	for (size_t i = 0; i < pages->count; ++i) {
		MitePage* mp = &pages->items[i];

		da_append_cstr(out, "void render_");
		da_append_cstr(out, mp->name);
		da_append_cstr(out, "(StringBuilder* out, SitePage* page) {\n");
		da_append_cstr(out, "	render_content_func_t render_content_func = NULL;\n");


		da_append_many(out, mp->rendered_code.items, mp->rendered_code.count);

		da_append_cstr(out, "}\n");
	}

	da_append_cstr(out,
		"int main(void) {\n"
		"	construct_global_state();\n"
		"	construct_templates();\n"
		"	StringBuilder out = {0};\n\n"
	);

	for (size_t i = 0; i < pages->count; ++i) {
		MitePage* mp = &pages->items[i];

		da_append_cstr(out, "	{\n");

		da_append_cstr(out, "		SitePage* page = find_page(&global.pages, \"");
		da_append_cstr(out, mp->md_path);
		da_append_cstr(out, "\");\n");
		da_append_cstr(out, "		printf(\"[rendering] %s\\n\", page->output);\n");

		da_append_cstr(out, "		SiteTemplate* st = find_template(&global.templates, page->layout);\n");
		da_append_cstr(out, "		if (st) st->function(&out, page, render_");
		da_append_cstr(out, mp->name);
		da_append_cstr(out, ");\n");
		da_append_cstr(out, "		else render_");
		da_append_cstr(out, mp->name);
		da_append_cstr(out, "(&out, page);\n");

		da_append_cstr(out, "		write_to_file(page->output, &out);\n");
		da_append_cstr(out, "		out.count = 0;\n");


		da_append_cstr(out, "	}\n");
	}

	da_append_cstr(out,
		"	free(out.items);\n"
		"	return 0;\n"
		"}"
	);
}

uint64_t get_modification_time(const char *path_cstr) {
#ifndef _WIN32
	struct stat st;
	if (stat(path_cstr, &st) != 0) return 0;
	return (uint64_t)st.st_mtime;
#else
	WIN32_FILE_ATTRIBUTE_DATA attr;
	if (!GetFileAttributesExA(path_cstr, GetFileExInfoStandard, &attr)) return 0;

	FILETIME ft = attr.ftLastWriteTime;
	ULARGE_INTEGER ull;
	ull.LowPart  = ft.dwLowDateTime;
	ull.HighPart = ft.dwHighDateTime;

	return (ull.QuadPart - 116444736000000000ULL) / 10000000ULL;
#endif
}

bool check_need_to_render(MitePages* pages, MiteTemplates* templates) {
	uint64_t most_recent_template = 0;

	for (size_t i = 0; i < templates->count; ++i) {
		MiteTemplate* mt = &templates->items[i];
		uint64_t time = get_modification_time(mt->path);
		if (time > most_recent_template) most_recent_template = time;
	}

	for (size_t i = 0; i < pages->count; ++i) {
		MitePage* mp = &pages->items[i];
		uint64_t time_html = get_modification_time(mp->final_html_path);
		uint64_t time_md   = get_modification_time(mp->md_path);

		if (time_md > time_html) return true;
		if (most_recent_template > time_html) return true;
	}

	return false;
}


typedef struct {
	MitePages pages;
	MiteTemplates templates;
	const char* mite_source_path;

	StringBuilder second_stage;

	bool arg_first_stage;
	bool arg_keep;
	bool arg_serve;
	bool arg_watch;
	bool arg_incremental;
	bool arg_no_watcher;
} MiteGenerator;

int mite_generate(MiteGenerator* m) {
	while(m->arg_watch) watch();

	if (m->pages.count == 0) {
		printf("[done] nothing to do\n");
		return 0;
	}

	bool need_to_render = m->arg_incremental ? check_need_to_render(&m->pages, &m->templates) : true;
	int result = 0;

	if (need_to_render) {
		render_all(&m->pages, &m->templates);

		second_stage_include_header(&m->second_stage, m->mite_source_path);
		second_stage_codegen(&m->second_stage, &m->pages, &m->templates);
		write_to_file("site.c", &m->second_stage);
		printf("[generated] site\n");

		if (m->arg_first_stage) return 0;

		result = build_and_run_site();
		if (result == 0 && !m->arg_keep) cleanup_site();

		if (result == 0) printf("[done]\n");
		else             printf("[failed]\n");
	}

	if (result == 0 && m->arg_serve) {
		printf("[serving]\n");

#ifndef _WIN32
		if (get_mite_binary_path() == NULL) {
			printf("[error] mite binary not found.\n");
			return 1;
		}
#endif
		if (!m->arg_no_watcher) start_watcher();
		execute_line("python -m http.server");
		if (!m->arg_no_watcher) stop_watcher();
		printf("[done]\n");
	}
	return result;
}

void free_mite_generator(MiteGenerator* m) {
	for (size_t i = 0; i < m->pages.count; ++i) {
		MitePage* page = &m->pages.items[i];
		free(page->md_path);
		free(page->name);
		free(page->final_html_path);
		free(page->rendered_code.items);
		free(page->front_matter.items);
	}
	free(m->pages.items);

	for (size_t i = 0; i < m->templates.count; ++i) {
		MiteTemplate* t = &m->templates.items[i];
		if (t->path) {
			free(t->name);
			free(t->path);
			free(t->rendered_code.items);
		}
	}

	free(m->templates.items);
	free(m->second_stage.items);
}

#define MITE_VERSION_CSTR "[mite v1.4.1]"
void print_usage(const char* prog) {
	printf(MITE_VERSION_CSTR"\n");
	printf("usage: %s [options]\n", prog);
	printf("options:\n");
	printf("  --serve          build and serve the site with 'python -m http.server', then run the watcher\n");
	printf("  --no-watcher     do not start a watcher while serving\n");
	printf("  --incremental    render only if there are changes\n");
	printf("  --first-stage    only generate site.c, do not compile or run\n");
	printf("  --keep           keep the generated site.c file\n");
	printf("  --source <PATH>  path to mite.c source file (default: ./mite.c or /usr/share/mite/mite.c)\n");
}


int main(int argc, char** argv) {
	MiteGenerator m = {0};

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			print_usage(argv[0]);
			return 0;
		} else if (0 == strcmp(argv[i], "--version")) {
			printf(MITE_VERSION_CSTR"\n");
			return 0;
		} else if (0 == strcmp(argv[i], "--first-stage")) { m.arg_first_stage = true;
		} else if (0 == strcmp(argv[i], "--keep"))        { m.arg_keep        = true;
		} else if (0 == strcmp(argv[i], "--serve"))       { m.arg_serve       = true;
		} else if (0 == strcmp(argv[i], "--watch"))       { m.arg_watch       = true;
		} else if (0 == strcmp(argv[i], "--incremental")) { m.arg_incremental = true;
		} else if (0 == strcmp(argv[i], "--no-watcher"))  { m.arg_no_watcher  = true;
		} else if ((0 == strcmp(argv[i], "--source")) && i + 1 < argc) {
			m.mite_source_path = argv[++i];
		} else {
			fprintf(stderr, "unknown option: %s\n", argv[i]);
			print_usage(argv[0]);
			return 1;
		}
	}

	if (!m.mite_source_path) {
		if (file_exists("./mite.c")) {
			m.mite_source_path = "./mite.c";
		} else if (file_exists("/usr/share/mite/mite.c")) {
			m.mite_source_path = "/usr/share/mite/mite.c";
		} else {
			fprintf(stderr, "[error] mite.c source not found.\n\tplease provide it with --source option.\n");
			return 1;
		}

	}
	if (strlen(m.mite_source_path) < 3) {
		print_usage(argv[0]);
		return 1;
	}
	if (!file_exists(m.mite_source_path)) {
		fprintf(stderr, "[error] file '%s' not found.\n", m.mite_source_path);
		return 1;
	}

	search_files(&m.pages, &m.templates);
	int result = mite_generate(&m);
	free_mite_generator(&m);
	return result;
}


#endif // #ifdef SECOND_STAGE #else
