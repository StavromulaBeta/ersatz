#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <execinfo.h>
#include <ctype.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include "main.h"

CURL* curl_handle;

static void print_tag_hashes()
{
  char* arr[] = {"P", "A", "I", "B", "BR", "SCRIPT", "STYLE", "EM", "H1", "H2", "H3", "H4", "H5", "H6", "IMG"};
  for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); ++i)
  {
    printf("#define %s_TAG %i\n", arr[i], insensitive_hash(arr[i]));
  }
}

FILE* url_to_file(char* url)
{
  // Writes data from a URL to a file.
  FILE* out_file = tmpfile(); // Make a temporary file
  if (!out_file) throw_error("Cannot load URL %s", url);
  curl_easy_setopt(curl_handle, CURLOPT_URL, url);            // Set the URL
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, out_file); // Set output file
  curl_easy_perform(curl_handle);
  rewind(out_file);
  return out_file;
}

htmlDocPtr parse_html_file(FILE* fp, char* url)
{
  htmlDocPtr doc = htmlReadFd(fileno(fp), url, NULL, HTML_PARSE_NOBLANKS | HTML_PARSE_NONET);
  if (!doc) throw_error("Cannot parse file");
  return doc;
}

node* simplify_html(htmlNodePtr ptr, node* head)
{
  // ptr is the LAST node, and we iterate BACKWARDS, so our linked list is forwards
  for (; ptr ; ptr = ptr->prev)
  {
    switch (ptr->type)
    {
      case XML_ELEMENT_NODE:
        {
          size_t tag_hash = insensitive_hash((char*)ptr->name);
          switch(tag_hash)
          {
            #define P_TAG 112
            #define A_TAG 97
            #define I_TAG 105
            #define B_TAG 98
            #define BR_TAG 6428816
            #define SCRIPT_TAG 646306219
            #define STYLE_TAG -733564175
            #define EM_TAG 6625608
            #define H1_TAG 6822345
            #define H2_TAG 6822346
            #define H3_TAG 6822347
            #define H4_TAG 6822348
            #define H5_TAG 6822349
            #define H6_TAG 6822350
            #define IMG_TAG 874608419
            case SCRIPT_TAG:
            case STYLE_TAG:
              // Ignore child text
              break;
            default:
              // Ignore tag, but not text (default behavior for unknown tag)
              head = simplify_html(ptr->last, head);
              break;
            case B_TAG:
            case EM_TAG:
              // Bold text
              head = alloc_node(make_bold, NULL,
                     simplify_html(ptr->last,
                     alloc_node(remove_bold, NULL, head)));
              break;
            case I_TAG:
              // Italic text
              head = alloc_node(make_italic, NULL,
                     simplify_html(ptr->last,
                     alloc_node(remove_italic, NULL, head)));
              break;
            case H1_TAG:
            case H2_TAG:
            case H3_TAG:
            case H4_TAG:
            case H5_TAG:
            case H6_TAG:
              // Bold and spaced out
              head = alloc_node(seperator, NULL,
                     alloc_node(make_bold, NULL,
                     simplify_html(ptr->last,
                     alloc_node(remove_bold, NULL,
                     alloc_node(seperator, NULL, head)))));
              break;
            case P_TAG:
              // Just spaced out
              head = alloc_node(seperator, NULL,
                     simplify_html(ptr->last,
                     alloc_node(seperator, NULL, head)));
              break;
            case BR_TAG:
              // Newline
              head = alloc_node(text, "\n", simplify_html(ptr->last, head));
              break;
            case A_TAG:
              // Hyperlink
              // <a> tags cannot be nested, which is truly a blessing
              head = alloc_node(hyperlink, xmlGetProp(ptr, (xmlChar*)"href"), // FIXME could return NULL!
                     simplify_html(ptr->last,
                     alloc_node(end_hyperlink, NULL, head)));
              break;
            case IMG_TAG:
              head = alloc_node(image, NULL, // TODO Download image (relative urls?!)
                     simplify_html(ptr->last, head));
              break;
          }
        }
        break;
      case XML_TEXT_NODE:
        {
          char* str = (char*)ptr->content;
          if (str[strspn(str, " \r\n\t")] != '\0') // trick to remove whitespace lines.
            head = alloc_node(text, str, head);
        }
        break;
      default:;
    }
  }
  return head;
}

node* alloc_node(node_type type, void* data, node* next)
{
  node* n = malloc(sizeof(*n)); // TODO free
  *n = (node){.type = type, .data = data, .next = next};
  return n;
}


void print_simplified_html(node* ptr)
{
  for (; ptr ; ptr = ptr->next)
  {
    switch (ptr->type)
    {
      case text:
        printf("[TEXT]: %s\n", ptr->text);
        break;
      case seperator:
        printf("[SEPERATOR]\n");
        break;
      case make_bold:
        printf("[BEGIN BOLD]\n");
        break;
      case remove_bold:
        printf("[END BOLD]\n");
        break;
      case make_italic:
        printf("[BEGIN ITALIC]\n");
        break;
      case remove_italic:
        printf("[END ITALIC]\n");
        break;
      case hyperlink:
        printf("[HYPERLINK TO %s]\n", ptr->text);
        break;
      case end_hyperlink:
        printf("[END HYPERLINK]\n");
        break;
      default:;
    }
  }
}

int main()
{
  bind_error_signals();
  // Init libcURL
  curl_global_init(CURL_GLOBAL_ALL);
  curl_handle = curl_easy_init();
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:15.0) Gecko/20100101 Firefox/15.0.1");
  curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);     // Disable the progress bar
  curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects
  // Actual code
  char* url       = "en.wikipedia.org/wiki/Web_browser";
  FILE* html      = url_to_file(url);
  htmlDocPtr doc  = parse_html_file(html, url);
  node* simple    = simplify_html(doc->last, NULL);
  print_simplified_html(simple);
  // Cleanup
  xmlFreeDoc(doc);
  xmlCleanupParser();
  fclose(html);
  curl_easy_cleanup(curl_handle);
  return EXIT_SUCCESS;
}

__attribute__((format(printf, 1, 2))) void throw_error(char* fmt, ...)
{
  fputs("\033[31;1m", stderr);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fputc('\n', stderr);
  if (errno)
  {
    const char* str = strerror(errno);
    fprintf(stderr, "\033[0m(%c%s)\n", tolower(*str), str + 1);
  }
  fputs("\033[0;2m", stderr);
  void *trace_array[10];
  fputs("begin backtrace...\n", stderr);
  size_t size = backtrace(trace_array, 10);
  backtrace_symbols_fd(trace_array, size, STDERR_FILENO);
  exit(EXIT_FAILURE);
}

void bind_error_signals()
{
  char signals[] = { SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGABRT, SIGBUS, SIGFPE, SIGSEGV, SIGPIPE, SIGTERM, SIGCHLD };
  for (size_t i = 0; i < sizeof(signals); ++i) signal(signals[i], handle_error_signal);
}

_Noreturn void handle_error_signal(int sig)
{
  throw_error("Recieved signal %i (%s)", sig, strsignal(sig));
}

int insensitive_hash(const char *str)
{
  // http://www.cse.yorku.ca/~oz/hash.html
  int hash = 0;
  int c;
  while ((c = tolower(*str++)))
    hash = c + (hash << 6) + (hash << 16) - hash;
  return hash;
}
