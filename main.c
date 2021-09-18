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
#include <libxml/uri.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include "SDL_render.h"
#include "main.h"

char* current_url = NULL;

CURL* curl_handle;
char* window_title = "";

int plotter_x = 0, plotter_y = 0;
int scroll_offset = 0;

SDL_Renderer* renderer;

SDL_Color black = {0, 0, 0, 255};
SDL_Color blue  = {0, 0, 255, 255};
SDL_Color gray  = {128, 128, 128, 255};
SDL_Color text_color;

TTF_Font* regular_font;
TTF_Font* bold_font;
TTF_Font* italic_font;
TTF_Font* current_font;

hlink hyperlinks[1024];
int num_hyperlinks = 0;

int is_on_screen(int y1, int y2)
{
  return y1 > scroll_offset || y2 < scroll_offset + 480;
}

void add_hyperlink(char* url, int x1, int y1, int x2, int y2)
{
  if (num_hyperlinks < NUM_HYPERLINKS)
  {
    hyperlinks[num_hyperlinks++] = (hlink) {.url=url, .x1=x1, .y1=y1, .x2=x2, .y2=y2};
  }
}

char* add_urls(char* url1, char* url2)
{
  char* ret;
  CURLU *h;
  CURLUcode uc;
  int flags = CURLU_GUESS_SCHEME;
  h = curl_url();
  uc = curl_url_set(h, CURLUPART_URL, url1, flags);
  uc = curl_url_set(h, CURLUPART_URL, url2, flags);
  uc = curl_url_get(h, CURLUPART_URL, &ret, flags);
  curl_url_cleanup(h);
  return ret;
}

void render_text(char* static_text, SDL_Renderer* renderer, TTF_Font* font, bool render)
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
    if (render)
    {
      SDL_Surface* surface = TTF_RenderText_Blended(font, text, text_color); // Render our line - only up to the null byte
      SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
      SDL_Rect rect = {plotter_x, plotter_y, char_width * len, char_height};
      SDL_RenderCopy(renderer, texture, NULL, &rect);
      SDL_DestroyTexture(texture);
      SDL_FreeSurface(surface);
    }
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
  char* arr[] = {"TR", "TD", "TH", "TITLE", "P", "A", "I", "B", "BR", "SCRIPT", "STYLE", "EM", "H1", "H2", "H3", "H4", "H5", "H6", "IMG"};
  for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); ++i)
  {
    printf("#define %s_TAG %u\n", arr[i], insensitive_hash(arr[i]));
  }
}

FILE* url_to_file(char* url)
{
  // Writes data from a URL to a file.
  FILE* out_file = tmpfile(); // Make a temporary file
  if (!out_file) throw_error("Cannot load URL %s", url);
  curl_easy_setopt(curl_handle, CURLOPT_URL, url);            // Set the URL
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, out_file); // Set output file
  int err = curl_easy_perform(curl_handle);
  if (err) throw_error((char*)curl_easy_strerror(err));
  rewind(out_file);
  return out_file;
}

htmlDocPtr parse_html_file(FILE* fp, char* url)
{
  xmlSubstituteEntitiesDefault(true);
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
#define TR_TAG 7609598
#define TD_TAG 7609584
#define TH_TAG 7609588
#define TITLE_TAG 368167992
#define P_TAG 112
#define A_TAG 97
#define I_TAG 105
#define B_TAG 98
#define BR_TAG 6428816
#define SCRIPT_TAG 646306219
#define STYLE_TAG 3561403121
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
            case TR_TAG:
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
              {
                char* href = (char*)xmlGetProp(ptr, (xmlChar*)"href");
                head = alloc_node(hyperlink, href,
                       simplify_html(ptr->last,
                       alloc_node(end_hyperlink, NULL, head)));
              }
              break;
            case IMG_TAG:
              {
                // TODO check whether on screen
                char* src = (char*)xmlGetProp(ptr, (xmlChar*)"src");
                char* full_url = add_urls(current_url, src);
                FILE* img = url_to_file(full_url);
                SDL_RWops* rw = SDL_RWFromFP(img, 0);
                SDL_Surface* surface = IMG_Load_RW(rw, 0);
                fclose(img);
                free(src);
                free(rw);
                free(full_url);
                if (!surface) throw_error((char*)IMG_GetError());
                head = alloc_node(image, surface,
                       simplify_html(ptr->last, head));
              }
              break;
          }
        }
        break;
      case XML_TEXT_NODE:
        {
          char* str = (char*)ptr->content;
          for (char* ptr = str; *ptr ; ptr++) // Delete all newlines
            if (*ptr == '\n') *ptr = ' ';
          if (str[strspn(str, " \r\t")] != '\0') // trick to remove whitespace lines.
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
  node* n = malloc(sizeof(*n));
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
      case image:
        printf("[IMAGE %p]\n", (void*)ptr->image);
      default:;
    }
  }
}

void render_simplified_html(node* ptr)
{
  bool is_seperated = false;
  int x1, y1, x2, y2; char* url; // hyperlink stuff
  for (; ptr ; ptr = ptr->next)
  {
    _Bool render = false;
    if (plotter_y > -480) render = true;
    if (plotter_y > 480) return;
    switch (ptr->type)
    {
      case text:
        render_text(ptr->text, renderer, current_font, render);
        break;
      case seperator:
        if (!is_seperated)
        {
          plotter_y += TTF_FontHeight(regular_font) * 1.5;
          plotter_x = 0;
          if (render)
          {
            SDL_SetRenderDrawColor(renderer, 192, 192, 192, SDL_ALPHA_OPAQUE);
            SDL_RenderDrawLine(renderer, 0, plotter_y, 640, plotter_y);
          }
          plotter_y += TTF_FontHeight(regular_font) * 0.5;
          is_seperated = true;
        }
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
        x1 = plotter_x;
        y1 = plotter_y;
        url = ptr->text;
        text_color = blue;
        break;
      case end_hyperlink:
        if (render)
        {
          x2 = plotter_x;
          y2 = plotter_y + TTF_FontHeight(current_font);
          add_hyperlink(url, x1, y1, x2, y2);
        }
        text_color = black;
        break;
      case image:
        {
          plotter_y += TTF_FontHeight(current_font);
          plotter_x = 0;
          int image_width = ptr->image->w;
          int image_height = ptr->image->h;
          if (render)
          {
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, ptr->image);
            if (image_width > 640)
            {
              image_height *= (640 / image_width);
              image_width = 640;
            }
            SDL_Rect rect = {0, plotter_y, image_width, image_height};
            SDL_RenderCopy(renderer, texture, NULL, &rect);
            SDL_DestroyTexture(texture);
          }
          plotter_y += image_height;
        }
        break;
      default:
        break;
    }
    if (ptr->type!=seperator) is_seperated = false;
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
  if (IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_TIF | IMG_INIT_WEBP) == 0) throw_error("Failed to init images");

  SDL_Window* window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, 0);
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
  regular_font = TTF_OpenFont("iosevka-term-regular.ttf", 15);
  bold_font    = TTF_OpenFont("iosevka-term-bold.ttf", 15);
  italic_font  = TTF_OpenFont("iosevka-term-italic.ttf", 15);
  current_font = regular_font;
  text_color = black;

  //current_url = "en.wikipedia.org/wiki/Web_browser";
  current_url = "news.ycombinator.com";

new_page:;

  printf("Downloading %s...\n", current_url);
  FILE* html      = url_to_file(current_url);
  printf("Parsing html...\n");
  htmlDocPtr doc  = parse_html_file(html, current_url);
  printf("Simplifying AST...\n");
  node* simple    = simplify_html(doc->last, NULL);
  printf("Done.\n");
  //print_simplified_html(simple);

  xmlCleanupParser();
  fclose(html);

  scroll_offset = 0;

  SDL_SetWindowTitle(window, window_title);

  SDL_Event e;
  do {
    num_hyperlinks = 0;
    SDL_SetRenderDrawColor(renderer, 242, 233, 234, 255);
    SDL_RenderClear(renderer);
    render_simplified_html(simple);
    SDL_RenderPresent(renderer);

    plotter_x = 0;
    plotter_y = scroll_offset;

    SDL_PollEvent(&e);
    //printf("Recieved event %i\n", e.type);
    switch (e.type)
    {
      case SDL_KEYDOWN:
        switch (e.key.keysym.sym)
        {
          case SDLK_PAGEDOWN:
            scroll_offset -= 10;
            break;
          case SDLK_PAGEUP:
            scroll_offset += 10;
            break;
        }
        //printf("Scroll is %i\n", scroll_offset);
        //SDL_FlushEvent(SDL_KEYDOWN);
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_LEFT)
        {
          int x = e.button.x;
          int y = e.button.y;
          for (int i = 0; i < num_hyperlinks; ++i)
          {
            hlink h = hyperlinks[i];
            if (h.x1 < x && h.x2 > x && h.y1 < y && h.y2 > y)
            {
              // Clicked!
              current_url = add_urls(current_url, h.url);
              dealloc_nodes(simple);
              xmlFreeDoc(doc);
              goto new_page;
            }
          }
        }
    }
  } while (e.type != SDL_QUIT);

  // Cleanup
  dealloc_nodes(simple);
  xmlFreeDoc(doc);
  TTF_CloseFont(regular_font);
  TTF_CloseFont(bold_font);
  TTF_CloseFont(italic_font);
  TTF_Quit();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  curl_easy_cleanup(curl_handle);
  print_tag_hashes();
  return EXIT_SUCCESS;
}

void dealloc_nodes(node* n)
{
  if (n)
  {
    dealloc_nodes(n->next);
    if (n->type == image) SDL_FreeSurface(n->image);
    else if (n->type == hyperlink) free(n->text);
    free(n);
  }
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

unsigned int insensitive_hash(const char *str)
{
  // http://www.cse.yorku.ca/~oz/hash.html
  unsigned int hash = 0;
  int c;
  while ((c = tolower(*str++)))
    hash = c + ((long)hash << 6) + ((long)hash << 16) - hash;
  return hash;
}
