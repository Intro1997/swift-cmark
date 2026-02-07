#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// clang-format off
#include "cmark-gfm_config.h"
#include "cmark-gfm.h"
#include "node.h"
#include "cmark-gfm-extension_api.h"
#include "syntax_extension.h"
#include "parser.h"
#include "registry.h"

#include <cmark-gfm-core-extensions.h>
// clang-format on

static const char* TEST_MD_FILE_PATH = "../../range_test/range_test.md";
static int error = 0;

#define SET_ERROR() \
  do {              \
    error = -1;     \
  } while (0);

static int get_line_count(FILE* file) {
  int count = 0;

  if (file == NULL) {
    SET_ERROR();
    printf("get_line_count: Invalid file stream\n");
    return count;
  }

  fpos_t origin_pos;
  if (fgetpos(file, &origin_pos) != 0) {
    printf("get_line_count: Get file position failed!\n");
    SET_ERROR();
    return count;
  }

  char c;
  char last_c;
  while ((c = fgetc(file)) != EOF) {
    if (c == '\n') {
      count += 1;
    }
    last_c = c;
  }
  if (last_c != '\n') {
    count += 1;
  }

  if (fsetpos(file, &origin_pos) != 0) {
    printf("get_line_count: set file position failed!\n");
    SET_ERROR();
    return count;
  }

  return count;
}

static char* getline_of_file(FILE* stream, int* size) {
  char* line = NULL;
  int c_count = 0;
  char c;

  if (stream == NULL) {
    return line;
  }

  fpos_t origin_pos;
  if (fgetpos(stream, &origin_pos) != 0) {
    printf("getline_of_file: Move position to origin failed!\n");
    SET_ERROR();
    return line;
  }

  while ((c = fgetc(stream)) != EOF && c != '\n') {
    c_count += 1;
  }

  if (c_count == 0) {
    if (c == '\n') {
      line = (char*)malloc(sizeof(char) * 2);
      line[0] = '\n';
      line[1] = '\0';
    }
    return line;
  }

  if (fsetpos(stream, &origin_pos) != 0) {
    printf("getline_of_file: Move position to origin failed!\n");
    SET_ERROR();
    return line;
  }

  line = (char*)malloc(sizeof(char) * (c_count + 1));

  fgets(line, c_count + 1, stream);

  if (size != NULL) {
    *size = c_count + 1;
  }

  c = fgetc(stream);  // skip `\n`

  return line;
}

static char** load_file_lines(int* line_count) {
  FILE* file = fopen(TEST_MD_FILE_PATH, "r");
  if (file == NULL) {
    printf("Range check failed! Load %s failed!\n", TEST_MD_FILE_PATH);
    SET_ERROR();
    return NULL;
  }

  char** data = NULL;

  *line_count = get_line_count(file);
  data = (char**)malloc(sizeof(char*) * (*line_count));

  char* line;
  int index = 0;
  while ((line = getline_of_file(file, NULL)) != NULL) {
    if (index < *line_count) {
      data[index] = line;
    } else {
      printf("load_file_lines: Error, %d larget than line count %d\n", index,
             *line_count);
      SET_ERROR();
      break;
    }
    index += 1;
  }

  fclose(file);
  return data;
}

static void free_data(char** data, const int size) {
  for (int i = 0; i < size; i++) {
    free(data[i]);
  }
  free(data);
}

static cmark_parser* create_parser(int* options) {
  if (options == NULL) {
    printf("create_parser: Error! Options pointer is null!\n");
    SET_ERROR();
    return NULL;
  }

  cmark_gfm_core_extensions_ensure_registered();

  *options = CMARK_OPT_TABLE_SPANS;
  *options |= CMARK_OPT_SOURCEPOS;
  *options |= CMARK_OPT_SMART;
  *options |= CMARK_OPT_FOOTNOTES;

  cmark_parser* parser = cmark_parser_new(*options);

  cmark_parser_attach_syntax_extension(parser,
                                       cmark_find_syntax_extension("table"));
  cmark_parser_attach_syntax_extension(
      parser, cmark_find_syntax_extension("strikethrough"));
  cmark_parser_attach_syntax_extension(parser,
                                       cmark_find_syntax_extension("tasklist"));
  return parser;
}

static char* get_md_content(int* content_size) {
  FILE* md_file = fopen(TEST_MD_FILE_PATH, "r");
  if (md_file == NULL) {
    printf("get_md_content: Error! Cannot open file %s!\n", TEST_MD_FILE_PATH);
    SET_ERROR();
    return NULL;
  }

  int extend_size = 128;
  int pos = 0;
  char* content = (char*)malloc(extend_size);
  *content_size = extend_size;

  char c;
  while (1) {
    c = fgetc(md_file);
    if (pos >= *content_size) {
      content = (char*)realloc(content, *content_size + extend_size);
      *content_size += extend_size;
    }
    *(content + pos) = c;
    if (c == EOF) {
      *(content + pos) = '\0';
      *content_size = pos;
      break;
    }
    pos += 1;
  }

  return content;
}

static void free_parser(cmark_parser* parser, cmark_node* document) {
#if DEBUG
  if (parser) cmark_parser_free(parser);
  if (document) cmark_node_free(document);
#else
  cmark_arena_reset();
#endif
  cmark_release_plugins();
}

static cmark_node* get_document_node(cmark_parser** parser) {
  int options;
  *parser = create_parser(&options);

  int content_size;
  char* md_content = get_md_content(&content_size);

  if (md_content == NULL || content_size == 0) {
    printf(
        "create_parser: Error! Markdown content pointer is null pointer Or "
        "Markdown content is empty!\n");
    SET_ERROR();
    return NULL;
  }

  cmark_parser_feed(*parser, md_content, content_size - 1);
  cmark_node* document = cmark_parser_finish(*parser);

  if (!document /*|| !print_document(document, FORMAT_XML, options, 0, parser)*/) {
    printf("get_document_node: Error! parse markdown code failed!\n");
    free_parser(*parser, document);
    *parser = NULL;
    SET_ERROR();
    return NULL;
  }

  return document;
}

static int check_text(cmark_node* root) {
  if (root == NULL) {
    SET_ERROR();
    return error;
  }

  int line_size;
  char** file_lines = load_file_lines(&line_size);

  cmark_event_type ev_type;
  cmark_node* cur;
  cmark_iter* iter = cmark_iter_new(root);
  while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    cur = cmark_iter_get_node(iter);
    if (cur->type == CMARK_NODE_TEXT) {
      const char* text_literal = cmark_node_get_literal(cur);
      if (cur->start_line == cur->end_line && cur->start_line <= line_size &&
          cur->end_line <= line_size) {
        const char* line_content = file_lines[cur->start_line - 1];
        const int line_len = strlen(line_content);
        int range_len = cur->end_column - cur->start_column + 1;

        if (cur->start_column > 0 && cur->end_column > 0 &&
            cur->start_column <= cur->end_column &&
            cur->start_column <= line_len && cur->end_column <= line_len &&
            range_len <= line_len) {
          char* range_line = (char*)malloc((range_len + 1) * sizeof(char));
          memcpy(range_line, line_content + cur->start_column - 1, range_len);
          range_line[range_len] = '\0';

          if (strcmp(range_line, text_literal) != 0) {
            printf("check_text: Error! range text <%s> != origin text <%s>!\n",
                   range_line, text_literal);
            SET_ERROR();
          }

          free(range_line);
        } else {
          printf("check_text: ERROR! invalid column range of text node!\n");
          SET_ERROR();
        }
      } else {
        printf("check_text: ERROR! invalid line range of text node!\n");
        SET_ERROR();
      }
    }
  }

  free_data(file_lines, line_size);

  cmark_iter_free(iter);
  return error;
}

static const char* get_node_range_string(cmark_node* node, char** buffer) {
  if (buffer == NULL) {
    return "NULL";
  }

  if (*buffer != NULL) {
    free(*buffer);
  }

  int line_size =
      snprintf(NULL, 0, "%d:%d-%d:%d", node->start_line, node->start_column,
               node->end_line, node->end_column);

  *buffer = (char*)malloc(sizeof(char) * (line_size + 1));

  sprintf(*buffer, "%d:%d-%d:%d", node->start_line, node->start_column,
          node->end_line, node->end_column);
  return *buffer;
}

static int check_child_range(cmark_node* root) {
  cmark_event_type ev_type;
  cmark_node* cur;
  cmark_iter* iter = cmark_iter_new(root);
  while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    cur = cmark_iter_get_node(iter);

    if (cur->type == CMARK_NODE_SOFTBREAK) {
      continue;
    }

    if (cur->parent != NULL) {
      if (cur->parent->start_line > cur->start_line ||
          cur->parent->end_line < cur->end_line) {
        SET_ERROR();
      } else if (cur->parent->start_line == cur->start_line &&
                 cur->parent->start_column > cur->start_column) {
        SET_ERROR();
      } else if (cur->parent->end_line == cur->end_line &&
                 cur->parent->end_column < cur->end_column) {
        SET_ERROR();
      }

      if (error != 0) {
        char* p_range = NULL;
        char* c_range = NULL;
        printf(
            "check_child_range: parent node range<%s> do not include child "
            "node "
            "range<%s>!\n",
            get_node_range_string(cur->parent, &p_range),
            get_node_range_string(cur, &c_range));
        free(p_range);
        free(c_range);
        return error;
      }
    }
  }

  cmark_iter_free(iter);
  return error;
}

static int has_crossing_range(cmark_node* child, cmark_node* child_subline) {
  if (child->start_line < child_subline->start_line &&
      child->end_line > child_subline->start_line &&
      child->end_line < child_subline->end_line) {
    SET_ERROR();
  } else if (child->start_line == child_subline->start_line &&
             child->start_column < child_subline->start_column) {
    if (child->end_line < child_subline->end_line) {
      SET_ERROR();
    } else if (child->end_line == child_subline->end_line &&
               child->end_column > child_subline->start_column) {
      SET_ERROR();
    }
  }
  return error;
}

static int check_subling_range(cmark_node* root) {
  cmark_event_type ev_type;
  cmark_node* cur;
  cmark_iter* iter = cmark_iter_new(root);
  while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    cur = cmark_iter_get_node(iter);

    cmark_node* child = cur->first_child;

    while (child != NULL && child->next != NULL) {
      if (child->type == CMARK_NODE_SOFTBREAK ||
          child->next->type == CMARK_NODE_SOFTBREAK) {
        child = child->next;
        continue;
      }

      if (has_crossing_range(child, child->next) != 0 ||
          has_crossing_range(child->next, child) != 0) {
        char* a_range = NULL;
        char* b_range = NULL;
        printf(
            "check_subling_range: Error! Crossing range between child<%s> and "
            "its subling<%s>\n",
            get_node_range_string(child, &a_range),
            get_node_range_string(child->next, &b_range));
        free(a_range);
        free(b_range);
        goto exit_flag;
      }

      child = child->next;
    }
  }
exit_flag:
  cmark_iter_free(iter);
  return error;
}

static int verify_range(void) {
  cmark_parser* parser = NULL;
  cmark_node* document = get_document_node(&parser);

  check_text(document);

  if (error != 0) {
    printf("verify_range: check_text not pass!\n");
    goto exit_verify;
  }

  check_child_range(document);

  if (error != 0) {
    printf("verify_range: check_child_range not pass!\n");
    goto exit_verify;
  }

  check_subling_range(document);
  if (error != 0) {
    printf("verify_range: check_subling_range not pass!\n");
    goto exit_verify;
  }

exit_verify:
  free_parser(parser, document);

  return error;
}

static int valid_range(void) { return verify_range(); }

int main(void) { return valid_range(); }
