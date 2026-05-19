#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmark-gfm-extension_api.h"
#include "cmark-gfm.h"
#include "cmark-gfm_config.h"
#include "node.h"
#include "parser.h"
#include "registry.h"
#include "syntax_extension.h"

#include <cmark-gfm-core-extensions.h>

#if defined(__OpenBSD__)
#include <sys/param.h>
#if OpenBSD >= 201605
#define USE_PLEDGE
#include <unistd.h>
#endif
#endif

#if defined(__OpenBSD__)
#include <sys/param.h>
#if OpenBSD >= 201605
#define USE_PLEDGE
#include <unistd.h>
#endif
#endif

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <fcntl.h>
#include <io.h>
#endif

typedef enum {
  FORMAT_NONE,
  FORMAT_HTML,
  FORMAT_XML,
  FORMAT_MAN,
  FORMAT_COMMONMARK,
  FORMAT_PLAINTEXT,
  FORMAT_LATEX
} writer_format;

/**
 * 直接在控制台打印 cmark_node* 的树状结构
 * @param root 要打印的根节点指针
 */
void cmark_node_print_tree(cmark_node *root) {
  if (!root) {
    printf("[Null Node]\n");
    return;
  }

  // 创建迭代器
  cmark_iter *iter = cmark_iter_new(root);
  if (!iter)
    return;

  cmark_event_type ev_type;
  int depth = 0;

  // 深度优先遍历树
  while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    cmark_node *node = cmark_iter_get_node(iter);
    if (!node)
      continue;

    // 获取节点类型名称 (例如 "document", "paragraph", "text")
    const char *type_str = cmark_node_get_type_string(node);

    if (ev_type == CMARK_EVENT_ENTER) {
      // 1. 打印缩进和树枝连线
      for (int i = 0; i < depth; i++) {
        if (i == depth - 1) {
          printf("├── ");
        } else {
          printf("│   ");
        }
      }

      // 2. 打印当前节点类型
      printf("%s", type_str);

      // 3. 根据节点类型，打印它包含的关键信息
      cmark_node_type type = cmark_node_get_type(node);

      // 如果节点带有文本字面量（比如文本内容、行内代码），直接打印出来
      const char *literal = cmark_node_get_literal(node);
      if (literal) {
        printf(": \"%s\"", literal);
      }

      // 如果是标题，额外打印它的层级 (如 H1, H2...)
      if (type == CMARK_NODE_HEADING) {
        printf(" (Level %d)", cmark_node_get_heading_level(node));
      }

      // 如果是代码块，额外打印它的编程语言标签
      if (type == CMARK_NODE_CODE_BLOCK) {
        const char *info = cmark_node_get_fence_info(node);
        if (info && strlen(info) > 0) {
          printf(" (Lang: %s)", info);
        }
      }

      // 换行，准备打印下一个节点
      printf("\n");

      // 4. 层级控制：如果该节点有子节点，深度 +1
      if (cmark_node_first_child(node) != NULL) {
        depth++;
      }

    } else if (ev_type == CMARK_EVENT_EXIT) {
      // 离开节点时：如果该节点有子节点，说明它的子树遍历完毕，深度 -1
      if (cmark_node_first_child(node) != NULL) {
        depth--;
      }
    }
  }

  // 释放迭代器
  cmark_iter_free(iter);
}

void print_source_code(cmark_node *node, const char *source_code,
                       int source_code_size) {
  if (node == NULL || source_code == NULL) {
    return;
  }

  int start_line = node->start_line;
  int start_column = node->start_column;
  int end_line = node->end_line;
  int end_column = node->end_column;

  char *segment_code = (char *)malloc(source_code_size);

  char c = *source_code;
  int current_line = 1;
  int current_pos = 1;
  int current_col = 1;
  int start_pos = 1;
  int end_pos = 1;

  while (current_pos <= source_code_size) {
    if (start_line == current_line && start_column == current_col) {
      start_pos = current_pos;
    }
    if (end_line == current_line && end_column == current_col) {
      end_pos = current_pos;
      break;
    }
    if (c == '\n' || c == '\r') {
      current_line += 1;
      current_col = 1;
    } else {
      current_col += 1;
    }
    current_pos += 1;
    c = source_code[current_pos - 1];
  }

  start_pos -= 1;
  strncpy(segment_code, source_code + start_pos, end_pos - start_pos);
  printf("%s\n", segment_code);
  free(segment_code);
}

cmark_node *get_edit_node(cmark_node *document, char **new_source_code) {
  if (document == NULL || new_source_code == NULL) {
    return NULL;
  }

  // suppose that our test case is:
  // 1. aaa
  //    1. bbb
  //        1. ccc
  //
  // and structure is:
  // document
  // ├── list
  // │   ├── item
  // │   │   ├── paragraph
  // │   │   │   ├── text: "aaa"
  // │   │   ├── list
  // │   │   │   ├── item
  // │   │   │   │   ├── paragraph
  // │   │   │   │   │   ├── text: "bbb"
  // │   │   │   │   ├── list
  // │   │   │   │   │   ├── item
  // │   │   │   │   │   │   ├── paragraph
  // │   │   │   │   │   │   │   ├── text: "ccc"

  cmark_node *aim_node = document->first_child->first_child->first_child->next
                             ->first_child->first_child;
  printf("[DEBUG]: aim node (%s) is [%d:%d, %d:%d] %s\n",
         cmark_node_get_type_string(aim_node), aim_node->start_line,
         aim_node->start_column, aim_node->end_line, aim_node->end_column,
         aim_node->as.literal.data);

  printf("[DEBUG]: we suppose to modify source code with '1.bbb\\n'\n");

  return aim_node;
}
void replace_test(cmark_node *document, const char *source_code,
                  int source_code_size) {
  char *new_code = NULL;
  cmark_node *aim_node = get_edit_node(document, &new_code);

  cmark_node_print_tree(document);
  print_source_code(aim_node, source_code, source_code_size);
}

void print_usage() {
  printf("Usage:   cmark-gfm [FILE*]\n");
  printf("Options:\n");
  printf("  --to, -t FORMAT   Specify output format (html, xml, man, "
         "commonmark, plaintext, latex)\n");
  printf("  --width WIDTH     Specify wrap width (default 0 = nowrap)\n");
  printf("  --sourcepos       Include source position attribute\n");
  printf("  --hardbreaks      Treat newlines as hard line breaks\n");
  printf("  --nobreaks        Render soft line breaks as spaces\n");
  printf("  --unsafe          Render raw HTML and dangerous URLs\n");
  printf("  --smart           Use smart punctuation\n");
  printf("  --validate-utf8   Replace UTF-8 invalid sequences with U+FFFD\n");
  printf("  --github-pre-lang Use GitHub-style <pre lang> for code blocks\n");
  printf(
      "  --extension, -e EXTENSION_NAME  Specify an extension name to use\n");
  printf(
      "  --list-extensions               List available extensions and quit\n");
  printf("  --strikethrough-double-tilde    Only parse strikethrough (if "
         "enabled)\n");
  printf("                                  with two tildes\n");
  printf("  --table-prefer-style-attributes Use style attributes to align "
         "table cells\n"
         "                                  instead of align attributes.\n");
  printf(
      "  --table-spans                   Enable parsing row- and column-span\n"
      "                                  in tables\n");
  printf("  --table-rowspan-ditto           Use a double-quote 'ditto mark' to "
         "indicate\n"
         "                                  row span in tables instead of a "
         "caret.\n");
  printf(
      "  --full-info-string              Include remainder of code block info\n"
      "                                  string in a separate attribute.\n");
  printf("  --help, -h       Print usage information\n");
  printf("  --version        Print version\n");
}

static bool print_document(cmark_node *document, writer_format writer,
                           int options, int width, cmark_parser *parser) {
  char *result;

  cmark_mem *mem = cmark_get_default_mem_allocator();

  switch (writer) {
  case FORMAT_HTML:
    result = cmark_render_html_with_mem(document, options,
                                        parser->syntax_extensions, mem);
    break;
  case FORMAT_XML:
    result = cmark_render_xml_with_mem(document, options, mem);
    break;
  case FORMAT_MAN:
    result = cmark_render_man_with_mem(document, options, width, mem);
    break;
  case FORMAT_COMMONMARK:
    result = cmark_render_commonmark_with_mem(document, options, width, mem);
    break;
  case FORMAT_PLAINTEXT:
    result = cmark_render_plaintext_with_mem(document, options, width, mem);
    break;
  case FORMAT_LATEX:
    result = cmark_render_latex_with_mem(document, options, width, mem);
    break;
  default:
    fprintf(stderr, "Unknown format %d\n", writer);
    return false;
  }
  printf("%s", result);
  mem->free(result);

  return true;
}

static void print_extensions(void) {
  cmark_llist *syntax_extensions;
  cmark_llist *tmp;

  printf("Available extensions:\nfootnotes\n");

  cmark_mem *mem = cmark_get_default_mem_allocator();
  syntax_extensions = cmark_list_syntax_extensions(mem);
  for (tmp = syntax_extensions; tmp; tmp = tmp->next) {
    cmark_syntax_extension *ext = (cmark_syntax_extension *)tmp->data;
    printf("%s\n", ext->name);
  }

  cmark_llist_free(mem, syntax_extensions);
}

int main(int argc, char *argv[]) {
  int i, numfps = 0;
  int *files;
  char buffer[4096];
  cmark_parser *parser = NULL;
  size_t bytes;
  cmark_node *document = NULL;
  int width = 0;
  char *unparsed;
  writer_format writer = FORMAT_HTML;
  int options = CMARK_OPT_DEFAULT;
  int res = 1;

#ifdef USE_PLEDGE
  if (pledge("stdio rpath", NULL) != 0) {
    perror("pledge");
    return 1;
  }
#endif

  cmark_gfm_core_extensions_ensure_registered();

#ifdef USE_PLEDGE
  if (pledge("stdio rpath", NULL) != 0) {
    perror("pledge");
    return 1;
  }
#endif

#if defined(_WIN32) && !defined(__CYGWIN__)
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  files = (int *)calloc(argc, sizeof(*files));

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--version") == 0) {
      printf("cmark-gfm %s", CMARK_GFM_VERSION_STRING);
      printf(" - CommonMark with GitHub Flavored Markdown converter\n(C) "
             "2014-2016 John MacFarlane\n");
      goto success;
    } else if (strcmp(argv[i], "--list-extensions") == 0) {
      print_extensions();
      goto success;
    } else if (strcmp(argv[i], "--full-info-string") == 0) {
      options |= CMARK_OPT_FULL_INFO_STRING;
    } else if (strcmp(argv[i], "--table-prefer-style-attributes") == 0) {
      options |= CMARK_OPT_TABLE_PREFER_STYLE_ATTRIBUTES;
    } else if (strcmp(argv[i], "--table-spans") == 0) {
      options |= CMARK_OPT_TABLE_SPANS;
    } else if (strcmp(argv[i], "--table-rowspan-ditto") == 0) {
      options |= CMARK_OPT_TABLE_ROWSPAN_DITTO;
    } else if (strcmp(argv[i], "--strikethrough-double-tilde") == 0) {
      options |= CMARK_OPT_STRIKETHROUGH_DOUBLE_TILDE;
    } else if (strcmp(argv[i], "--sourcepos") == 0) {
      options |= CMARK_OPT_SOURCEPOS;
    } else if (strcmp(argv[i], "--hardbreaks") == 0) {
      options |= CMARK_OPT_HARDBREAKS;
    } else if (strcmp(argv[i], "--nobreaks") == 0) {
      options |= CMARK_OPT_NOBREAKS;
    } else if (strcmp(argv[i], "--smart") == 0) {
      options |= CMARK_OPT_SMART;
    } else if (strcmp(argv[i], "--github-pre-lang") == 0) {
      options |= CMARK_OPT_GITHUB_PRE_LANG;
    } else if (strcmp(argv[i], "--unsafe") == 0) {
      options |= CMARK_OPT_UNSAFE;
    } else if (strcmp(argv[i], "--validate-utf8") == 0) {
      options |= CMARK_OPT_VALIDATE_UTF8;
    } else if (strcmp(argv[i], "--liberal-html-tag") == 0) {
      options |= CMARK_OPT_LIBERAL_HTML_TAG;
    } else if ((strcmp(argv[i], "--help") == 0) ||
               (strcmp(argv[i], "-h") == 0)) {
      print_usage();
      goto success;
    } else if (strcmp(argv[i], "--width") == 0) {
      i += 1;
      if (i < argc) {
        width = (int)strtol(argv[i], &unparsed, 10);
        if (unparsed && strlen(unparsed) > 0) {
          fprintf(stderr, "failed parsing width '%s' at '%s'\n", argv[i],
                  unparsed);
          goto failure;
        }
      } else {
        fprintf(stderr, "--width requires an argument\n");
        goto failure;
      }
    } else if ((strcmp(argv[i], "-t") == 0) || (strcmp(argv[i], "--to") == 0)) {
      i += 1;
      if (i < argc) {
        if (strcmp(argv[i], "man") == 0) {
          writer = FORMAT_MAN;
        } else if (strcmp(argv[i], "html") == 0) {
          writer = FORMAT_HTML;
        } else if (strcmp(argv[i], "xml") == 0) {
          writer = FORMAT_XML;
        } else if (strcmp(argv[i], "commonmark") == 0) {
          writer = FORMAT_COMMONMARK;
        } else if (strcmp(argv[i], "plaintext") == 0) {
          writer = FORMAT_PLAINTEXT;
        } else if (strcmp(argv[i], "latex") == 0) {
          writer = FORMAT_LATEX;
        } else {
          fprintf(stderr, "Unknown format %s\n", argv[i]);
          goto failure;
        }
      } else {
        fprintf(stderr, "No argument provided for %s\n", argv[i - 1]);
        goto failure;
      }
    } else if ((strcmp(argv[i], "-e") == 0) ||
               (strcmp(argv[i], "--extension") == 0)) {
      i += 1; // Simpler to handle extensions in a second pass, as we can
              // directly register them with the parser.

      if (i < argc && strcmp(argv[i], "footnotes") == 0) {
        options |= CMARK_OPT_FOOTNOTES;
      }
    } else if (*argv[i] == '-') {
      print_usage();
      goto failure;
    } else { // treat as file argument
      files[numfps++] = i;
    }
  }

#if DEBUG
  parser = cmark_parser_new(options);
#else
  parser = cmark_parser_new_with_mem(options, cmark_get_arena_mem_allocator());
#endif

  for (i = 1; i < argc; i++) {
    if ((strcmp(argv[i], "-e") == 0) || (strcmp(argv[i], "--extension") == 0)) {
      i += 1;
      if (i < argc) {
        if (strcmp(argv[i], "footnotes") == 0) {
          continue;
        }
        cmark_syntax_extension *syntax_extension =
            cmark_find_syntax_extension(argv[i]);
        if (!syntax_extension) {
          fprintf(stderr, "Unknown extension %s\n", argv[i]);
          goto failure;
        }
        cmark_parser_attach_syntax_extension(parser, syntax_extension);
      } else {
        fprintf(stderr, "No argument provided for %s\n", argv[i - 1]);
        goto failure;
      }
    }
  }

  char *source_code = NULL;
  int source_code_size = 0;

  for (i = 0; i < numfps; i++) {
    FILE *fp = fopen(argv[files[i]], "rb");
    if (fp == NULL) {
      fprintf(stderr, "Error opening file %s: %s\n", argv[files[i]],
              strerror(errno));
      goto failure;
    }

    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
      if (source_code == NULL) {
        source_code = (char *)malloc(bytes);
      } else {
        source_code = (char *)realloc(source_code, source_code_size + bytes);
      }
      strncpy(source_code + source_code_size, buffer, bytes);
      source_code_size += bytes;

      cmark_parser_feed(parser, buffer, bytes);
      if (bytes < sizeof(buffer)) {
        break;
      }
    }

    fclose(fp);
  }

  if (numfps == 0) {
    while ((bytes = fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
      cmark_parser_feed(parser, buffer, bytes);
      if (bytes < sizeof(buffer)) {
        break;
      }
    }
  }

#ifdef USE_PLEDGE
  if (pledge("stdio", NULL) != 0) {
    perror("pledge");
    return 1;
  }
#endif

  document = cmark_parser_finish(parser);

  // replace_test(document, source_code, source_code_size);

  if (!document || !print_document(document, writer, options, width, parser))
    goto failure;

success:
  res = 0;

failure:

#if DEBUG
  if (parser)
    cmark_parser_free(parser);

  if (document)
    cmark_node_free(document);
#else
  cmark_arena_reset();
#endif

  cmark_release_plugins();

  free(files);

  return res;
}
