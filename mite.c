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


void sv_to_c_code(StringView sv, StringBuilder* out) {
	da_append_many(out, sv.items, sv.count);
	da_append(out, '\n');
}

void sv_to_byte_array(StringView sv, StringBuilder* out) {
	static char buffer[16] = {0};
	da_append_cstr(out, "OUT(\"");
	for (uint64_t i = 0; i < sv.count; ++i) {
		sprintf(buffer, "\\x%02x", sv.items[i]);
		da_append_cstr(out, buffer);
	}
	sprintf(buffer, "\", %lu)\n", sv.count);
	da_append_cstr(out, buffer);
	//da_append(out, '\0');
}

int main(int argc, char *argv[]) {
	const char *filepath = "./test.mite";

	StringBuilder file = {0};
	if (!read_entire_file(filepath, &file)) { return 1; }
	StringView tl = { .items = file.items, .count = file.count };

	StringBuilder out = {0};

	bool html_mode = true;
	while (tl.count) {
		if (html_mode) {
			StringView token = sv_trim(chop_until(&tl, "<?", 2));
			sv_to_byte_array(token, &out);
		} else {
			StringView token = sv_trim(chop_until(&tl, "?>", 2));
			sv_to_c_code(token, &out);
		}
		html_mode = !html_mode;

	}

	printf("%.*s", (int)out.count, out.items);

	return 0;
}


