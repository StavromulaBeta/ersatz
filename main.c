#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <execinfo.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "SDL_render.h"
#include "main.h"

CURL* curl_handle;
char* window_title = "";

int plotter_x = 0, plotter_y = 0;
int scroll_offset = 0;

TTF_Font* small_font;
TTF_Font* large_font;

SDL_Renderer* renderer;

SDL_Color black = {0, 0, 0, 255};
SDL_Color blue  = {0, 0, 255, 255};
SDL_Color text_color;

TTF_Font* regular_font;
TTF_Font* bold_font;
TTF_Font* italic_font;
TTF_Font* current_font;

void render_text(char* static_text, SDL_Renderer* renderer, TTF_Font* font)
{
  // This algorithm writes wrapped text to the window and updates the plotter variables accordingly.
  // It assumes that the provided font is monospaced, for simplicity and performance reasons.
  int window_width = 640;
  size_t bytes = strlen(static_text) + 2;
  char* text = malloc(bytes); // We allocate a buffer for our own string...
  char* start = text;
  memcpy(text + 1, static_text, bytes - 1); // ...and move the string there
  text[0] = ' ';
  int char_width, char_height;
  TTF_SizeUTF8(font, "a", &char_width, &char_height); // Calculate the width and height of one character
  int linebreak_pos = -1;
  bool more_lines;
  bool first_iteration = true;
  do {
    more_lines = false;
    int wrap_chars = (window_width - plotter_x) / char_width; // Calculate how many pixels we have to work with
    int len = 0;
    for (; text[len]; ++len) // Iterate over the string until we find place for a linebreak
    {
      if (linebreak_pos >= 0 && len > wrap_chars) break;
      if (isspace(text[len])) linebreak_pos = len; // Whitespace is a valid position for an inserted linebreak
      if (text[len] == '\n') goto linebreak; // Newlines are always linebreaks
    }
    len = strlen(text);
    if (wrap_chars < len && linebreak_pos >= 0)
    {
linebreak:
      text[linebreak_pos] = '\0'; // Null-terminate our string where we want the line to end
      len = linebreak_pos;
      more_lines = true;
    }
    text += first_iteration;
    len -= first_iteration;
    linebreak_pos -= first_iteration;
    SDL_Surface* surface = TTF_RenderText_Blended(font, text, text_color); // Render our line - only up to the null byte
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_Rect rect = {plotter_x, plotter_y, char_width * len, char_height};
    SDL_RenderCopy(renderer, texture, NULL, &rect);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
    if (more_lines) // Update the plotter variables...
    {
      plotter_y += char_height; // ...for the next iteration...
      plotter_x = 0;
      text += linebreak_pos + 1;
    }
    else plotter_x += char_width * len; // ...or not
    first_iteration = false;
  } while (more_lines);
  free(start);
}

static void print_tag_hashes()
{
  char* arr[] = {"TITLE", "P", "A", "I", "B", "BR", "SCRIPT", "STYLE", "EM", "H1", "H2", "H3", "H4", "H5", "H6", "IMG"};
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
            #define TITLE_TAG 368167992
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
            case TITLE_TAG:
              window_title = (char*)ptr->children->content;
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

void render_simplified_html(node* ptr)
{
  for (; ptr ; ptr = ptr->next)
  {
    switch (ptr->type)
    {
      case text:
        render_text(ptr->text, renderer, current_font);
        break;
      case seperator:
        plotter_y += 20;
        plotter_x = 0;
        break;
      case make_bold:
        current_font = bold_font;
        break;
      case make_italic:
        current_font = italic_font;
        break;
      case remove_italic:
      case remove_bold:
        current_font = regular_font;
        break;
      case hyperlink:
        text_color = blue;
        break;
      case end_hyperlink:
        text_color = black;
        break;
      default:
        break;
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
  // Init SDL2
  if (SDL_Init(SDL_INIT_EVERYTHING)) throw_error("Failed to initialise the SDL window");
  if (TTF_Init()) throw_error("Failed to initialise SDL_TTF");

  SDL_Window* window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, 0);
  renderer = SDL_CreateRenderer(window, -1, 0);
  regular_font = TTF_OpenFont("iosevka-term-regular.ttf", 15);
  bold_font    = TTF_OpenFont("iosevka-term-bold.ttf", 15);
  italic_font  = TTF_OpenFont("iosevka-term-italic.ttf", 15);
  current_font = regular_font;
  SDL_SetRenderDrawColor(renderer, 242, 233, 234, 255);
  text_color = black;

  //render_text("Did you ever hear the tragedy of Darth Plagueis The Wise? I thought not. It's not a story the Jedi would tell you. It's a Sith legend. Darth Plagueis was a Dark Lord of the Sith, so powerful and so wise he could use the Force to influence the midichlorians to create life... He had such a knowledge of the dark side that he could even keep the ones he cared about from dying. The dark side of the Force is a pathway to many abilities some consider to be unnatural. He became so powerful... the only thing he was afraid of was losing his power, which eventually, of course, he did. Unfortunately, he taught his apprentice everything he knew, then his apprentice killed him in his sleep. Ironic. He could save others from death, but not himself.", renderer, small_font);
  //render_text("Hello, world!", renderer, large_font);
  //render_text("Did you ever hear the tragedy of Darth Plagueis The Wise? I thought not. It's not a story the Jedi would tell you. It's a Sith legend. Darth Plagueis was a Dark Lord of the Sith, so powerful and so wise he could use the Force to influence the midichlorians to create life... He had such a knowledge of the dark side that he could even keep the ones he cared about from dying. The dark side of the Force is a pathway to many abilities some consider to be unnatural. He became so powerful... the only thing he was afraid of was losing his power, which eventually, of course, he did. Unfortunately, he taught his apprentice everything he knew, then his apprentice killed him in his sleep. Ironic. He could save others from death, but not himself.", renderer, small_font);

  char* url       = "en.wikipedia.org/wiki/Web_browser";
  FILE* html      = url_to_file(url);
  htmlDocPtr doc  = parse_html_file(html, url);
  node* simple    = simplify_html(doc->last, NULL);

  SDL_SetWindowTitle(window, window_title);

  SDL_Event e;
  do {

    SDL_RenderClear(renderer);
    render_simplified_html(simple);
    SDL_RenderPresent(renderer);

    plotter_x = 0;
    plotter_y = scroll_offset;

    SDL_WaitEvent(&e);
    printf("Recieved event %i\n", e.type);
    if (e.type == SDL_KEYDOWN)
    {
      switch (e.key.keysym.sym)
      {
        case SDLK_PAGEDOWN:
          scroll_offset -= 15;
          break;
        case SDLK_PAGEUP:
          scroll_offset += 15;
          break;
      }
    }
  } while (e.type != SDL_QUIT);

  // Cleanup
  TTF_CloseFont(small_font);
  TTF_CloseFont(large_font);
  TTF_Quit();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
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
  fputs("\n\033[0;2m", stderr);
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
