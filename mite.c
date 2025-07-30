#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PATH_LEN 1024

#define da_reserve(da, expected_capacity)                                                          \
	do {                                                                                           \
		if ((expected_capacity) > (da)->capacity) {                                                \
			if ((da)->capacity == 0) {                                                             \
				(da)->capacity = 256;                                                              \
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



#ifndef _WIN32
	#include <sys/stat.h>
	#include <dirent.h>
	#include <unistd.h>
#else
	#include <windows.h>
#endif

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

static inline int build_and_run_site() {
#ifndef _WIN32
	return execute_line("cc -o site site.c && ./site");
#else
	return execute_line("cl /Fe:site.exe site.c && site.exe");
#endif
}


bool read_entire_file(const char* filepath_cstr, StringBuilder* sb) {
	bool result = true;

	FILE* f = fopen(filepath_cstr, "rb");
	if (f == NULL)                 { result = false; goto defer; }
	if (fseek(f, 0, SEEK_END) < 0) { result = false; goto defer; }

#ifndef _WIN32
	long m = ftell(f);
#else
	long long m = _ftelli64(f);
#endif

	if (m < 0)                     { result = false; goto defer; }
	if (fseek(f, 0, SEEK_SET) < 0) { result = false; goto defer; }

	size_t new_count = sb->count + m;
	if (new_count > sb->capacity) {
		sb->items = realloc(sb->items, new_count);
		assert(sb->items != NULL);
		sb->capacity = new_count;
	}

	fread(sb->items + sb->count, m, 1, f);
	if (ferror(f)) { result = false; goto defer; }
	sb->count = new_count;

defer:
	if (!result) {
		printf("Could not read file %s: %s\n", filepath_cstr, strerror(errno));
	}
	if (f) { fclose(f); }
	return result;
}

bool write_to_file(const char* filepath_cstr, StringBuilder* sb) {
	FILE* f = fopen(filepath_cstr, "wb");
	if (f == NULL) {
		printf("Could not open file for writing: %s %s\n", filepath_cstr, strerror(errno));
		return false;
	}

	size_t written = fwrite(sb->items, 1, sb->count, f);
	if (written != sb->count) {
		printf("Error writing to file: %s\n", strerror(errno));
		fclose(f);
		return false;
	}

	fclose(f);
	return true;
}

static inline StringView sv_trim(StringView input) {
	StringView sv = input;

	int l = 0;
	while (l < sv.count && (sv.items[l] == ' ' || sv.items[l] == '\t' || sv.items[l] == '\n')) l++;

	sv.items += l;
	sv.count -= l;

	int r = sv.count - 1;
	while (r >= 0 && (sv.items[r] == ' ' || sv.items[r] == '\t' || sv.items[r] == '\n')) r--;

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


typedef struct {
	char* path;
	StringBuilder rendered_code;
} MiteTemplate;

typedef struct {
	char* name;
	char* md_path;
	char* final_html_path;

	char* mite_template;

	StringBuilder rendered_code;
	StringBuilder front_matter;
} MitePage;

typedef struct {
	MitePage* items;
	size_t count;
	size_t capacity;
} MitePages;



//#define SECOND_STAGE // for development
#ifdef SECOND_STAGE

static char temp_sprintf_buf[16] = {0};
void unusedtemp() { (void)temp_sprintf_buf; }
#define OUT_HTML(buf, size) da_append_many(out, buf, size);
#define INT(x) do { if (sprintf(temp_sprintf_buf, "%d", (x))) da_append_cstr(out, temp_sprintf_buf); } while (0);

#define RAWSTR2(x) #x
#define RAWSTR(x) da_append_cstr(out, RAWSTR2(x));
#define CSTR(x) da_append_cstr(out, (x));
#define SVP(x) da_append_many(out, (x)->items, (x)->count);
#define SV(x) SVP(&(x))
#define STR(x) CSTR((x))

// hallucinations
typedef struct {
	const char* title;
	const char* description;
	const char* url;
	const char* favicon_path;
} SiteGlobal;

typedef struct {
	const char* title;
	const char* date;
	const char* description;
	const char* url;
} SitePage;


#endif
// --- SECOND STAGE END ---


// ------------------- md2html --------------------------

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

void parse_inline(const char* line, StringBuilder* out) {
	const char* p = line;
	while (*p && *p != '\n') {
		if (starts_with(p, "***")) {
			p += 3;
			const char* end = search_str_until_newline(p, "***");
			if (!end) break;
			da_append_cstr(out, "<strong><i>");
			da_append_escape_html(out, p, end - p);
			da_append_cstr(out, "</i></strong>");
			p = end + 3;
		} else if (starts_with(p, "_**")) {
			p += 3;
			const char* end = search_str_until_newline(p, "**_");
			if (!end) break;
			da_append_cstr(out, "<strong><i>");
			da_append_escape_html(out, p, end - p);
			da_append_cstr(out, "</i></strong>");
			p = end + 3;
		} else if (starts_with(p, "**_")) {
			p += 3;
			const char* end = search_str_until_newline(p, "_**");
			if (!end) break;
			da_append_cstr(out, "<strong><i>");
			da_append_escape_html(out, p, end - p);
			da_append_cstr(out, "</i></strong>");
			p = end + 3;
		} else if (starts_with(p, "**")) {
			p += 2;
			const char* end = search_str_until_newline(p, "**");
			if (!end) break;
			da_append_cstr(out, "<strong>");
			da_append_escape_html(out, p, end - p);
			da_append_cstr(out, "</strong>");
			p = end + 2;
		} else if (*p == '*' || *p == '_') {
			char marker = *p++;
			const char* end;
			if (marker == '*') end = search_str_until_newline(p, "*");
			if (marker == '_') end = search_str_until_newline(p, "_");
			if (!end) break;
			da_append_cstr(out, "<i>");
			da_append_escape_html(out, p, end - p);
			da_append_cstr(out, "</i>");
			p = end + 1;
		} else if (*p == '`') {
			++p;
			const char* end = search_str_until_newline(p, "`");
			if (!end) break;
			da_append_cstr(out, "<code>");
			da_append_escape_html(out, p, end - p);
			da_append_cstr(out, "</code>");
			p = end + 1;
		} else if (*p == '[') {
			const char* end_text = search_str_until_newline(p, "]");
			if (!end_text || end_text[1] != '(') {
				da_append_escape_html(out, p, 1);
				++p;
				continue;
			}
			const char* end_url = search_str_until_newline(end_text + 2, ")");
			if (!end_url) break;
			da_append_cstr(out, "<a href=\"");
			da_append_many(out, end_text + 2, end_url - (end_text + 2));
			da_append_cstr(out, "\">");
			da_append_escape_html(out, p + 1, end_text - (p + 1));
			da_append_cstr(out, "</a>");
			p = end_url + 1;
		} else {
			da_append(out, *p);
			++p;
		}
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
	const char* l = md->items;
	bool started_paragraph = false;
	while (*l) {
		while (*l == '\t' || *l == ' ' || *l == '\n')
			l++;

		if (*l == '<') {
			if (started_paragraph) {
				da_append_cstr(out, "</p>\n");
				started_paragraph = false;
			}
			append_until_newline(out, l);
		} else if (*l == '#') {
			int level = 0;
			while (*l == '#') {
				level++;
				l++;
			}
			while (*l == ' ') l++;
			char tag[12];
			sprintf(tag, "h%d", level);
			da_append(out, '<');
			da_append_cstr(out, tag);
			da_append(out, '>');
			parse_inline(l, out);
			da_append_cstr(out, "</");
			da_append_cstr(out, tag);
			da_append_cstr(out, ">\n");

		} else if (starts_with(l, "- [ ] ")) {
			da_append_cstr(out, "<ul><li><input type=\"checkbox\" disabled>");
			parse_inline(l + 6, out);
			da_append_cstr(out, "</li></ul>\n");

		} else if (starts_with(l, "- ")) {
			da_append_cstr(out, "<ul><li>");
			parse_inline(l + 2, out);
			da_append_cstr(out, "</li></ul>\n");
		} else if (starts_with(l, "---\n")) {
			StringView sv = {
				.items = (char*)(l + 4),
				.count = md->items + md->count - (l + 4)
			};
			StringView needle = { .items = "---\n", .count = 4 };
			const size_t match = sv_strstr(sv, needle);
			if (match != sv.count) {
				const char* end = sv.items + match;
				l += 3;
				da_append_cstr(out_fm, "<?");
				da_append_escape_html(out_fm, l, end - l);
				da_append_cstr(out_fm, "?>\n");
				l = end + 3;
			}

		} else if (*l != '\n' && *l != '\0') {
			if (!started_paragraph) {
				da_append_cstr(out, "<p>");
				parse_inline(l, out);
				started_paragraph = true;
			} else {
				da_append(out, ' ');
				parse_inline(l, out);

				const char* temp = l;
				skip_after_newline(&temp);
				if (*temp == '\n') {
					started_paragraph = false;
					da_append_cstr(out, "</p>\n");
				}
				if (*temp == ' ' && *(temp + 1) == ' ') {
					started_paragraph = false;
					da_append_cstr(out, "</p>\n");
				}
			}
		}

		skip_after_newline(&l);
	}
	if (started_paragraph) {
		da_append_cstr(out, "</p>\n");
		started_paragraph = false;
	}
	da_append(out, '\0');
	da_append(out_fm, '\0');
}


// ------------------- /md2html/ ------------------------


StringBuilder print_content_into_layout(const StringBuilder* const layout, const StringBuilder* const content) {
	StringBuilder result = {0};

	StringView macro = { .items = "CONTENT()", .count = 9 };
	size_t content_loc = sv_strstr(SB_TO_SV(layout), macro);

	if (content_loc == layout->count) {
		// no content found, could be untemplated page, print as is
		da_append_many(&result, layout->items, layout->count);
		return result;
	}

	da_append_many(&result, layout->items, content_loc);
	da_append_many(&result, content->items, content->count);

	const char* after_macro  = layout->items + content_loc + macro.count;
	size_t after_macro_count = layout->count - content_loc - macro.count;
	da_append_many(&result, after_macro, after_macro_count);

	return result;
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
	da_append_cstr(out, "OUT_HTML(\"");
	static char buffer[16] = {0};
	da_append_many(out, ba->string.items, ba->string.count);
	sprintf(buffer, "\", %d)\n", (int)ba->count);
	da_append_cstr(out, buffer);
}

void sv_to_byte_array(StringView sv, ByteArray* out) {
	static char buffer[16] = {0};
	for (uint64_t i = 0; i < sv.count; ++i) {
		if (sv.items[i] == '\0') break;
		sprintf(buffer, "\\x%02x", sv.items[i]);
		da_append_cstr(&out->string, buffer);
		out->count++;
	}
}


// NOTE: source must be null terminated
void render_html_to_c(StringView source, StringBuilder* out) {
	bool html_mode = true;
	ByteArray ba = {0};
	while (source.count) {
		if (html_mode) {
			StringView token = sv_trim(chop_until(&source, "<?", 2));
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

bool apply_template(MiteTemplate* mite_template, MitePage* mite_page) {
	if (!file_exists(mite_template->path) || !file_exists(mite_page->md_path)) return false;

	StringBuilder md = {0};
	if (!read_entire_file(mite_page->md_path, &md)) return false;
	da_append(&md, '\0');

	StringBuilder raw_html = {0};
	StringBuilder raw_fm = {0};
	render_md_to_html(&md, &raw_html, &raw_fm);

	StringBuilder content = {0};
	render_html_to_c(SB_TO_SV(&raw_html), &content);
	render_html_to_c(SB_TO_SV(&raw_fm), &mite_page->front_matter);

	if (mite_template->rendered_code.count == 0) {
		if (!render_mite_layout(mite_template)) return false;
	}
	mite_page->rendered_code = print_content_into_layout(&mite_template->rendered_code, &content);

	free(md.items);
	free(raw_html.items);
	free(raw_fm.items);
	free(content.items);

	return true;
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
	sprintf(out, "%s/%s", a, b);
}


void handle_md_file(MitePages* pages, const char* template_path, const char* md_dir, const char* md_name) {
	MitePage mp = {0};

	mp.md_path = calloc(MAX_PATH_LEN, sizeof(md_dir));
	join_path(mp.md_path, md_dir, md_name);

	mp.name = strdup(mp.md_path);
	size_t len = strlen(mp.name);
	if (len > 3 && strcmp(mp.name + len - 3, ".md") == 0) {
		len -= 3;
	}
	mp.name[len] = '\0';
	for (size_t i = 0; i < len; ++i) {
		if (!isalnum(mp.name[i])) {
			mp.name[i] = '_';
		}
	}

	mp.final_html_path = calloc(MAX_PATH_LEN, sizeof(md_dir));
	join_path(mp.final_html_path, md_dir, "index.html");

	mp.mite_template = strdup(template_path);

	da_append(pages, mp);
}

void search_mite_pages(MitePages* pages) {
#ifndef _WIN32
	DIR* root = opendir(".");
	if (!root) return;

	struct dirent* entry;

	while ((entry = readdir(root)) != NULL) {
		if (entry->d_type != DT_DIR) continue;
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		char dir_path[MAX_PATH_LEN];
		join_path(dir_path, ".", entry->d_name);

		DIR* dir = opendir(dir_path);
		if (!dir) continue;

		int has_mite = 0;
		char mite_path[MAX_PATH_LEN];
		struct dirent* subentry = NULL;

		while ((subentry = readdir(dir)) != NULL) {
			if (subentry->d_type != DT_REG) continue;
			if (is_mite_file(subentry->d_name)) {
				has_mite = 1;
				join_path(mite_path, ".", entry->d_name);
				join_path(mite_path, mite_path, subentry->d_name);
				break;
			}
		}
		closedir(dir);
		if (!has_mite) continue;

		dir = opendir(dir_path);
		if (!dir) continue;

		while ((subentry = readdir(dir)) != NULL) {
			if (subentry->d_type != DT_DIR) continue;
			if (strcmp(subentry->d_name, ".") == 0 || strcmp(subentry->d_name, "..") == 0)
				continue;

			char child_dir[MAX_PATH_LEN];
			join_path(child_dir, dir_path, subentry->d_name);

			DIR* post_dir = opendir(child_dir);
			if (!post_dir) continue;

			struct dirent* post_entry;
			while ((post_entry = readdir(post_dir)) != NULL) {
				if (post_entry->d_type != DT_REG) continue;
				if (is_md_file(post_entry->d_name)) {
					handle_md_file(pages, mite_path, child_dir, post_entry->d_name);
					break;
				}
			}
			closedir(post_dir);
		}
		closedir(dir);
	}
	closedir(root);
#else
	WIN32_FIND_DATAA find_data;
	HANDLE h_find = FindFirstFileA(".\\*", &find_data);
	if (h_find == INVALID_HANDLE_VALUE) return;

	do {
		if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
		if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;

		char dir_path[MAX_PATH];
		join_path(dir_path, ".", find_data.cFileName);

		char mite_search[MAX_PATH];
		join_path(mite_search, dir_path, "*.mite");

		WIN32_FIND_DATAA mite_data;
		HANDLE h_mite = FindFirstFileA(mite_search, &mite_data);
		if (h_mite == INVALID_HANDLE_VALUE) continue;

		char mite_path[MAX_PATH];
		join_path(mite_path, dir_path, mite_data.cFileName);
		FindClose(h_mite);

		char child_search[MAX_PATH];
		join_path(child_search, dir_path, "*");

		WIN32_FIND_DATAA child_data;
		HANDLE h_child = FindFirstFileA(child_search, &child_data);
		if (h_child == INVALID_HANDLE_VALUE) continue;

		do {
			if (!(child_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
			if (strcmp(child_data.cFileName, ".") == 0 || strcmp(child_data.cFileName, "..") == 0) continue;

			char post_dir[MAX_PATH];
			join_path(post_dir, dir_path, child_data.cFileName);

			char md_pattern[MAX_PATH];
			join_path(md_pattern, post_dir, "*.md");

			WIN32_FIND_DATAA md_data;
			HANDLE h_md = FindFirstFileA(md_pattern, &md_data);
			if (h_md != INVALID_HANDLE_VALUE) {
				do {
					if (!(md_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
						if (is_md_file(md_data.cFileName)) {
							handle_md_file(pages, mite_path, post_dir, md_data.cFileName);
							break;
						}
					}
				} while (FindNextFileA(h_md, &md_data));
				FindClose(h_md);
			}
		} while (FindNextFileA(h_child, &child_data));
		FindClose(h_child);
	} while (FindNextFileA(h_find, &find_data));
	FindClose(h_find);
#endif
}


void second_stage_extract_header(StringBuilder* out) {
	StringBuilder source = {0};
	if (!read_entire_file("mite.c", &source)) exit(1);
	StringView delim = { .items = "// --- SECOND STAGE END ---", .count = 27 };
	size_t loc = sv_strstr(SB_TO_SV(&source), delim);
	da_append_cstr(out, "\n#define SECOND_STAGE\n\n");
	da_append_many(out, source.items, loc);
	free(source.items);
}

void second_stage_codegen(StringBuilder* out, MitePages* pages) {
	da_append_cstr(out,
		"SiteGlobal global = { .title = \"hanion.dev global title\", .description = \"recreational programmer\" };\n"
	);

	for (size_t i = 0; i < pages->count; ++i) {
		MitePage* mp = &pages->items[i];

		da_append_cstr(out, "void render_");
		da_append_cstr(out, mp->name);
		da_append_cstr(out, "(StringBuilder* out) {\n");

		// TODO: construct the page var
		da_append_cstr(out, "SitePage page = { .title = \"");
		da_append_cstr(out, mp->name);
		da_append_cstr(out, "\", .date = \"");
		da_append_cstr(out, "2099");
		da_append_cstr(out, "\"};\n");

		da_append_cstr(out, "\nprintf(\"[rendering] %s\\n\", \"");
		da_append_cstr(out, mp->final_html_path+2);
		da_append_cstr(out, "\");\n\n");

		da_append_many(out, mp->rendered_code.items, mp->rendered_code.count);

		da_append_cstr(out, "}\n");
	}


	da_append_cstr(out,
		"int main(void) {\n"
		"	StringBuilder out = {0};\n"
	);
	for (size_t i = 0; i < pages->count; ++i) {
		MitePage* mp = &pages->items[i];
		da_append_cstr(out, "	out.count = 0;");
		da_append_cstr(out, "	render_");
		da_append_cstr(out, mp->name);
		da_append_cstr(out, "(&out);\n");
		da_append_cstr(out, "	write_to_file(\"");
		da_append_cstr(out, mp->final_html_path);
		da_append_cstr(out, "\", &out);\n");
	}

	da_append_cstr(out,
		"	return 0;\n"
		"}"
	);
}



int main(int argc, char *argv[]) {
	if (!file_exists("index.mite")) {
		fprintf(stderr, "[error] missing 'index.mite'\n");
		return 1;
	}

	if (!file_exists("index.md")) {
		fprintf(stderr, "[error] missing 'index.md'\n");
		return 1;
	}

	MitePages pages = {0};

	MitePage home = { .name = "home", .md_path = "./index.md", .final_html_path = "./index.html", .mite_template = "./index.mite" };
	da_append(&pages, home);

	search_mite_pages(&pages);

	MiteTemplate templates[24] = {0};
	size_t template_count = 0;

	for (size_t i = 0; i < pages.count; ++i) {
		MitePage* page = &pages.items[i];

		MiteTemplate* mite_template = NULL;
		for (size_t i = 0; i < template_count; ++i) {
			if (templates[i].path && 0 == strcmp(page->mite_template, templates[i].path)) {
				mite_template = &templates[i];
				break;
			}
		}

		if (!mite_template) {
			mite_template = &templates[template_count++];
			mite_template->path = strdup(page->mite_template);
		}

		printf("[templating] %s\n", page->md_path+2);
// 		printf("[templating] %s\n          -> %s\n          w/ %s\n\n",
// 			page->md_path, page->final_html_path, page->mite_template);

		apply_template(mite_template, page);
	}

	StringBuilder second_stage = {0};
	second_stage_extract_header(&second_stage);
	second_stage_codegen(&second_stage, &pages);

	write_to_file("site.c", &second_stage);

	int result = build_and_run_site();
	
	if (result == 0) {
		printf("[done]\n");
	} else {
		printf("[failed]\n");
	}

	for (size_t i = 1; i < pages.count; ++i) {
		MitePage* page = &pages.items[i];
		free(page->md_path);
		free(page->name);
		free(page->mite_template);
		free(page->final_html_path);
		free(page->rendered_code.items);
		free(page->front_matter.items);
	}

	for (size_t i = 0; i < template_count; ++i) {
		MiteTemplate* t = &templates[i];
		if (t->path) {
			free(t->path);
			free(t->rendered_code.items);
		}
	}

	free(second_stage.items);
	return result;
}


