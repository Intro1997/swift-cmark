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

static const char* VERIFIER_DATA_PATH =
    "../../range_test/range_verifier_data.txt";
static const char* TEST_MD_FILE_PATH = "../../range_test/range_test.md";
static int error = 0;

#define SET_ERROR() \
  do {              \
    error = -1;     \
  } while (0);

static size_t get_line_count(FILE* file) {
  size_t count = 0;

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

static char* getline_of_file(FILE* stream, size_t* size) {
  char* line = NULL;
  size_t c_count = 0;
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

static char** load_file_lines(size_t* line_count) {
  FILE* file = fopen(TEST_MD_FILE_PATH, "r");
  if (file == NULL) {
    printf("Range check failed! Load %s failed!\n", VERIFIER_DATA_PATH);
    SET_ERROR();
    return NULL;
  }

  char** data = NULL;

  *line_count = get_line_count(file);
  data = (char**)malloc(sizeof(char*) * (*line_count));

  char* line;
  size_t index = 0;
  while ((line = getline_of_file(file, NULL)) != NULL) {
    if (index < *line_count) {
      data[index] = line;
    } else {
      printf("load_file_lines: Error, %zu larget than line count %zu\n", index,
             *line_count);
      SET_ERROR();
      break;
    }
    index += 1;
  }

  fclose(file);
  return data;
}

static void free_data(char** data, const size_t size) {
  for (size_t i = 0; i < size; i++) {
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

static char* get_md_content(size_t* content_size) {
  FILE* md_file = fopen(TEST_MD_FILE_PATH, "r");
  if (md_file == NULL) {
    printf("get_md_content: Error! Cannot open file %s!\n", TEST_MD_FILE_PATH);
    SET_ERROR();
    return NULL;
  }

  size_t extend_size = 128;
  size_t pos = 0;
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

static const char* get_cmark_type_string(cmark_node* node) {
  if (node == NULL) {
    return "NONE";
  }

  if (node->extension && node->extension->get_type_string_func) {
    const char* name =
        node->extension->get_type_string_func(node->extension, node);
    if (strcmp(name, "table_header") == 0) {
      return "tablerow";
    } else if (strcmp(name, "table_row") == 0) {
      return "tablecrow";
    } else if (strcmp(name, "table_cell") == 0) {
      return "tablecell";
    }
    return name;
  }

  switch (node->type) {
    case CMARK_NODE_NONE:
      return "none";
    case CMARK_NODE_DOCUMENT:
      return "document";
    case CMARK_NODE_BLOCK_QUOTE:
      return "blockquote";
    case CMARK_NODE_LIST:
      return "list";
    case CMARK_NODE_ITEM:
      return "item";
    case CMARK_NODE_CODE_BLOCK:
      return "codeblock";
    case CMARK_NODE_HTML_BLOCK:
      return "htmlblock";
    case CMARK_NODE_CUSTOM_BLOCK:
      return "customblock";
    case CMARK_NODE_PARAGRAPH:
      return "paragraph";
    case CMARK_NODE_HEADING:
      return "heading";
    case CMARK_NODE_THEMATIC_BREAK:
      return "thematicbreak";
    case CMARK_NODE_TEXT:
      return "text";
    case CMARK_NODE_SOFTBREAK:
      return "softbreak";
    case CMARK_NODE_LINEBREAK:
      return "linebreak";
    case CMARK_NODE_CODE:
      return "code";
    case CMARK_NODE_HTML_INLINE:
      return "htmlinline";
    case CMARK_NODE_CUSTOM_INLINE:
      return "custominline";
    case CMARK_NODE_EMPH:
      return "emphasis";
    case CMARK_NODE_STRONG:
      return "strong";
    case CMARK_NODE_LINK:
      return "link";
    case CMARK_NODE_IMAGE:
      return "image";
    case CMARK_NODE_ATTRIBUTE:
      return "attribute";
    case CMARK_NODE_FOOTNOTE_DEFINITION:
      return "footnotedefinition";
    case CMARK_NODE_FOOTNOTE_REFERENCE:
      return "footnotereference";
  }

  return "<unknown>";
}

static char** save_a_element_range(cmark_node* node, char** data,
                                   size_t* data_size) {
  if (node->type == CMARK_NODE_DOCUMENT) {
    return data;
  }

  if (data == NULL) {
    data = (char**)malloc(sizeof(char*));
  } else {
    data = (char**)realloc(data, sizeof(char*) * ((*data_size) + 1));
  }

  int line_size = snprintf(
      NULL, 0, "%s %d:%d-%d:%d", get_cmark_type_string(node), node->start_line,
      node->start_column, node->end_line, node->end_column);

  data[*data_size] = (char*)malloc(sizeof(char) * (line_size + 1));

  sprintf(data[*data_size], "%s %d:%d-%d:%d", get_cmark_type_string(node),
          node->start_line, node->start_column, node->end_line,
          node->end_column);

  (*data_size) += 1;
  return data;
}

static char** save_ranges_to_data(cmark_node* root, char** data,
                                  size_t* data_size) {
  // NOTE: `*data_size` must be 0!
  if (data_size == NULL || root->type == CMARK_NODE_SOFTBREAK) {
    return data;
  }

  data = save_a_element_range(root, data, data_size);

  cmark_node* child = root->first_child;
  while (child != NULL) {
    data = save_ranges_to_data(child, data, data_size);
    child = child->next;
  }

  return data;
}

static char** get_parser_range(size_t* line_count) {
  if (line_count == NULL) {
    printf("get_parser_range: Error! Line count pointer is NULL!\n");
    SET_ERROR();
    return NULL;
  }

  int options;
  cmark_parser* parser = create_parser(&options);

  size_t content_size;
  char* md_content = get_md_content(&content_size);

  if (md_content == NULL || content_size == 0) {
    printf(
        "create_parser: Error! Markdown content pointer is null pointer Or "
        "Markdown content is empty!\n");
    SET_ERROR();
    return NULL;
  }

  cmark_parser_feed(parser, md_content, content_size - 1);
  cmark_node* document = cmark_parser_finish(parser);

  if (!document /*|| !print_document(document, FORMAT_XML, options, 0, parser)*/) {
    printf("get_parser_range: Error! parse markdown code failed!\n");
    free_parser(parser, document);
    SET_ERROR();
    return NULL;
  }

  char** parser_range_data = NULL;
  size_t parser_range_data_size = 0;

  parser_range_data =
      save_ranges_to_data(document, parser_range_data, &parser_range_data_size);

  // cmark_event_type ev_type;
  // cmark_node* cur;
  // cmark_iter* iter = cmark_iter_new(document);
  // while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
  //   cur = cmark_iter_get_node(iter);
  //   if (cur->type == CMARK_NODE_DOCUMENT || cur->type == CMARK_NODE_SOFTBREAK
  //   ||
  //       cur->start_line == -1) {
  //     printf("Skip type:%s\n", get_cmark_type_string(cur));
  //     continue;
  //   }

  //   if (parser_range_data == NULL) {
  //     parser_range_data =
  //         (char**)malloc(sizeof(char*) * (parser_range_data_size + 1));
  //   } else {
  //     parser_range_data = (char**)realloc(
  //         parser_range_data, sizeof(char*) * (parser_range_data_size + 1));
  //   }

  //   int line_size = snprintf(
  //       NULL, 0, "%s %d:%d-%d:%d", get_cmark_type_string(cur),
  //       cur->start_line, cur->start_column, cur->end_line, cur->end_column +
  //       1);

  //   parser_range_data[parser_range_data_size] =
  //       (char*)malloc(sizeof(char) * (line_size + 1));

  //   sprintf(parser_range_data[parser_range_data_size], "%s %d:%d-%d:%d",
  //           get_cmark_type_string(cur), cur->start_line, cur->start_column,
  //           cur->end_line, cur->end_column + 1);

  //   // printf("%s %d:%d-%d:%d\n", cmark_node_get_type_string(cur),
  //   // cur->start_line,
  //   //        cur->start_column, cur->end_line, cur->end_column + 1);

  //   parser_range_data_size += 1;

  //   cur->start_line = -1;  // sign to has checked
  // }

  // cmark_iter_free(iter);
  free_parser(parser, document);

  *line_count = parser_range_data_size - 1;
  return parser_range_data;
}

static void print_ranges(char** ranges, size_t size) {
  for (size_t i = 0; i < size; i++) {
    printf("%s\n", ranges[i]);
  }
}

// static int check_range(void) {
//   size_t parser_line_count;
//   char** parser_range_data = get_parser_range(&parser_line_count);

//   size_t verifier_line_count = 0;
//   char** verifier_range_data = load_verifier_data(&verifier_line_count);

//   if (parser_range_data == NULL || verifier_range_data == NULL) {
//     printf(
//         "check_range: Error! Null parser range data Or verifier range
//         data!\n");
//     SET_ERROR();
//     return error;
//   }
//   if (parser_line_count != verifier_line_count) {
//     printf(
//         "check_range: Error! parser line count<%zu> not equals to verifier "
//         "line "
//         "count<%zu>\n",
//         parser_line_count, verifier_line_count);
//     printf("========\n");
//     printf("Correct ranges:\n");
//     print_ranges(verifier_range_data, verifier_line_count);
//     printf("=+=+=+=+=+=+=+=+=+=+=+\n");
//     printf("Your ranges:\n");
//     print_ranges(parser_range_data, parser_line_count);
//     printf("========\n");

//     size_t i = 0;
//     size_t min_count = parser_line_count > verifier_line_count
//                            ? verifier_line_count
//                            : parser_line_count;

//     for (i = 0; i < min_count; i++) {
//       if (strcmp(parser_range_data[i], verifier_range_data[i]) != 0) {
//         printf("A diff is Correct: [%s] vs Your: [%s] at Line %zu \n",
//                verifier_range_data[i], parser_range_data[i], i);
//         break;
//       }
//     }
//     if (i == min_count) {
//       printf("All diff start from Line %zu\n", i);
//     }

//     SET_ERROR();
//     return error;
//   }

//   for (size_t i = 0; i < parser_line_count; i++) {
//     if (strcmp(parser_range_data[i], verifier_range_data[i]) != 0) {
//       printf(
//           "check_range: Error! Line %zu not equal! Correct range [%s] != your
//           " "range [%s].\n", i, verifier_range_data[i],
//           parser_range_data[i]);
//       SET_ERROR();
//       return error;
//     }
//   }

//   free_data(parser_range_data, parser_line_count);
//   free_data(verifier_range_data, verifier_line_count);

//   return 0;
// }

// int main(void) { return check_range(); }

cmark_node* get_document_node(cmark_parser** parser) {
  int options;
  *parser = create_parser(&options);

  size_t content_size;
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
    printf("get_parser_range: Error! parse markdown code failed!\n");
    free_parser(*parser, document);
    *parser = NULL;
    SET_ERROR();
    return NULL;
  }

  return document;
}

int check_text(cmark_node* root) {
  if (root == NULL) {
    SET_ERROR();
    return error;
  }

  size_t line_size;
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
        const size_t line_len = strlen(line_content);
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

const char* get_node_range(cmark_node* node, char** buffer) {
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

int check_child_range(cmark_node* root) {
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
            get_node_range(cur->parent, &p_range),
            get_node_range(cur, &c_range));
        free(p_range);
        free(c_range);
        return error;
      }
    }
  }

  cmark_iter_free(iter);
  return error;
}

int has_crossing_range(cmark_node* child, cmark_node* child_subline) {
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
//(2:1-2:25 2:26-2:29)
int check_subling_range(cmark_node* root) {
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
            get_node_range(child, &a_range), get_node_range(child->next, &b_range));
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

int verify_range() {
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

int valid_range() { return verify_range(); }

int main(void) { return valid_range(); }
