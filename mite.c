#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

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
#define SB_TO_SV(sb) (StringView){ .items=(sb).items, .count=(sb).count }

typedef struct {
	char* items;
	size_t count;
	size_t capacity;
} StringBuilder;

typedef struct {
	char* items;
	size_t count;
} StringView;

#ifdef _WIN32
	#define MKDIR(path) mkdir(path)
#else
	#include <sys/stat.h>
	#define MKDIR(path) mkdir(path, 0777)
#endif

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
		printf("Could not open file for writing: %s\n", strerror(errno));
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
	while (l < sv.count && (sv.items[l] == ' ' || sv.items[l] == '\t')) l++;

	sv.items += l;
	sv.count -= l;

	int r = sv.count - 1;
	while (r >= 0 && (sv.items[r] == ' ' || sv.items[r] == '\t')) r--;

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


// ------------------- md2html --------------------------

void escape_html_and_append(StringBuilder* out, const char* line) {
	for (const char* p = line; *p && *p != '\n'; ++p) {
		switch (*p) {
			case '<': da_append_cstr(out, "&lt;");  break;
			case '>': da_append_cstr(out, "&gt;");  break;
			case '&': da_append_cstr(out, "&amp;"); break;
			default:  da_append(out, *p);           break;
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
			da_append_many(out, p, end - p);
			da_append_cstr(out, "</i></strong>");
			p = end + 3;
		} else if (starts_with(p, "_**")) {
			p += 3;
			const char* end = search_str_until_newline(p, "**_");
			if (!end) break;
			da_append_cstr(out, "<strong><i>");
			da_append_many(out, p, end - p);
			da_append_cstr(out, "</i></strong>");
			p = end + 3;
		} else if (starts_with(p, "**_")) {
			p += 3;
			const char* end = search_str_until_newline(p, "_**");
			if (!end) break;
			da_append_cstr(out, "<strong><i>");
			da_append_many(out, p, end - p);
			da_append_cstr(out, "</i></strong>");
			p = end + 3;
		} else if (starts_with(p, "**")) {
			p += 2;
			const char* end = search_str_until_newline(p, "**");
			if (!end) break;
			da_append_cstr(out, "<strong>");
			da_append_many(out, p, end - p);
			da_append_cstr(out, "</strong>");
			p = end + 2;
		} else if (*p == '*' || *p == '_') {
			char marker = *p++;
			const char* end;
			if (marker == '*') end = search_str_until_newline(p, "*");
			if (marker == '_') end = search_str_until_newline(p, "_");
			if (!end) break;
			da_append_cstr(out, "<i>");
			da_append_many(out, p, end - p);
			da_append_cstr(out, "</i>");
			p = end + 1;
		} else if (*p == '`') {
			++p;
			const char* end = search_str_until_newline(p, "`");
			if (!end) break;
			da_append_cstr(out, "<code>");
			da_append_many(out, p, end - p);
			da_append_cstr(out, "</code>");
			p = end + 1;
		} else if (*p == '[') {
			const char* end_text = search_str_until_newline(p, "]");
			if (!end_text || end_text[1] != '(') {
				da_append_many(out, p, 1);
				++p;
				continue;
			}
			const char* end_url = search_str_until_newline(end_text + 2, ")");
			if (!end_url) break;
			da_append_cstr(out, "<a href=\"");
			da_append_many(out, end_text + 2, end_url - (end_text + 2));
			da_append_cstr(out, "\">");
			da_append_many(out, p + 1, end_text - (p + 1));
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

void parse_file(const char* source, StringBuilder* out, StringBuilder* front_matter) {
	const char* l = source;
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
			const char* end = strstr(l + 4, "---\n");
			if (end) {
				l += 3;
				da_append_cstr(front_matter, "<?");
				da_append_many(front_matter, l, end - l);
				da_append_cstr(front_matter, "?>\n");
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
	da_append(front_matter, '\0');
}


// ------------------- /md2html/ ------------------------

void sv_to_c_code(StringView sv, StringBuilder* out) {
	da_append_many(out, sv.items, sv.count);
	da_append(out, '\n');
}

typedef struct {
	StringBuilder string;
	size_t count;
} ByteArray;

void byte_array_to_c_code(const ByteArray* ba, StringBuilder* out) {
	if (ba->count == 0) return;
	da_append_cstr(out, "OUT(\"");
	static char buffer[16] = {0};
	da_append_many(out, ba->string.items, ba->string.count);
	sprintf(buffer, "\", %lu)\n", ba->count);
	da_append_cstr(out, buffer);
}

void sv_to_byte_array(StringView sv, ByteArray* out) {
	static char buffer[16] = {0};
	for (uint64_t i = 0; i < sv.count; ++i) {
		sprintf(buffer, "\\x%02x", sv.items[i]);
		da_append_cstr(&out->string, buffer);
	}
	out->count = sv.count;
}

void render_md_to_html(StringBuilder* md, StringBuilder* out_html, StringBuilder* out_fm) {
	parse_file(md->items, out_html, out_fm);
}

// NOTE: source must be null terminated
void template_html_to_c(StringView source, StringBuilder* out) {
	bool html_mode = true;
	while (source.count) {
		if (html_mode) {
			StringView token = sv_trim(chop_until(&source, "<?", 2));
			static ByteArray ba = {0};
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
}


int main(int argc, char *argv[]) {
	MKDIR("build");

	StringBuilder mdfile = {0};
	if (!read_entire_file("./index.md", &mdfile)) return 1;
	da_append(&mdfile, '\0');

	StringBuilder raw_html = {0};
	StringBuilder raw_fm = {0};
	render_md_to_html(&mdfile, &raw_html, &raw_fm);

	StringBuilder content = {0};
	template_html_to_c(SB_TO_SV(raw_html), &content);
	write_to_file("build/content.c", &content);

	StringBuilder fm = {0};
	template_html_to_c(SB_TO_SV(raw_fm), &fm);
	write_to_file("build/fm.c", &fm);


	StringBuilder template_file = {0};
	if (!read_entire_file("./index.html.mite", &template_file)) { return 1; }

	StringBuilder layout = {0};
	template_html_to_c(SB_TO_SV(template_file), &layout);
	write_to_file("build/layout.c", &layout);

	return 0;
}


