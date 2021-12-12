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

SDL_Rect bar;
SDL_Rect back;
SDL_Rect url;
SDL_Rect backtext;
SDL_Rect urltext;

#define BAR_HEIGHT 50

typedef struct _url_list
{
  char* full_url;
  struct _url_list *next;
} url_list;

_Bool should_rerender_bar = 1;

url_list* history;

char* current_url = NULL;

CURL* curl_handle;
char* window_title = "";

int plotter_x = 20, plotter_y = BAR_HEIGHT;
int scroll_offset = 0;

SDL_Renderer* renderer;

SDL_Color black = {0, 0, 0, 255};
SDL_Color blue  = {0, 0, 255, 255};
SDL_Color gray  = {128, 128, 128, 255};
SDL_Color other_gray = {96, 96, 96, 255};
SDL_Color text_color;

TTF_Font* regular_font;
TTF_Font* menu_font;
TTF_Font* bold_font;
TTF_Font* italic_font;
TTF_Font* current_font;

hlink hyperlinks[1024];
int num_hyperlinks = 0;

int window_width = 640;
int window_height = 480;

int is_on_screen(int y1, int y2)
{
  return y1 > scroll_offset || y2 < scroll_offset + window_height;
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
    int wrap_chars = (window_width - plotter_x - 20) / char_width; // Calculate how many pixels we have to work with
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
      plotter_x = 20;
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
  printf("Downloading %s... ", url);
  FILE* out_file = tmpfile(); // Make a temporary file
  if (!out_file) throw_error("Cannot load URL %s", url);
  curl_easy_setopt(curl_handle, CURLOPT_URL, url);            // Set the URL
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, out_file); // Set output file
  int err = curl_easy_perform(curl_handle);
  if (err) throw_error((char*)curl_easy_strerror(err));
  rewind(out_file);
  puts("Done.");
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
                if (!surface) head = simplify_html(ptr->last, head);
                else
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
    if (plotter_y > -window_height) render = true;
    if (plotter_y > window_height) return;
    switch (ptr->type)
    {
      case text:
        render_text(ptr->text, renderer, current_font, render);
        break;
      case seperator:
        if (!is_seperated)
        {
          plotter_y += TTF_FontHeight(regular_font) * 1.5;
          plotter_x = 20;
          if (render)
          {
            SDL_SetRenderDrawColor(renderer, 192, 192, 192, SDL_ALPHA_OPAQUE);
            SDL_RenderDrawLine(renderer, 10, plotter_y, window_width - 20, plotter_y);
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
          int h = TTF_FontHeight(current_font);
          x2 = plotter_x;
          y2 = y1 + h;
          if (plotter_y > y1)
          {
            add_hyperlink(url, x1, y1, window_width - 10, y2);
            add_hyperlink(url, 10, plotter_y, x2, plotter_y + h);
          }
          else add_hyperlink(url, x1, y1, x2, y2);
        }
        text_color = black;
        break;
      case image:
        {
          plotter_y += TTF_FontHeight(current_font);
          plotter_x = 20;
          int image_width = ptr->image->w;
          int image_height = ptr->image->h;
          if (image_width > (window_width - 40))
          {
            image_height *= (window_width - 40);
            image_height /= image_width;
            image_width = window_width - 40;
          }
          if (render)
          {
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, ptr->image);
            SDL_Rect rect = {plotter_x, plotter_y, image_width, image_height};
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

int main(int argc, char** argv)
{
  bind_error_signals();
  // Init libcURL
  curl_global_init(CURL_GLOBAL_ALL);
  curl_handle = curl_easy_init();
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "Ersatz/0.0.1");
  curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);     // Disable the progress bar
  curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects
  // Init SDL2
  if (SDL_Init(SDL_INIT_EVERYTHING)) throw_error("Failed to initialise the SDL window");
  if (TTF_Init()) throw_error("Failed to initialise SDL_TTF");
  if (IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_TIF | IMG_INIT_WEBP) == 0) throw_error("Failed to init images");

  SDL_Window* window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height, SDL_WINDOW_RESIZABLE);
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
  regular_font = TTF_OpenFont("iosevka-term-regular.ttf", 15);
  menu_font    = TTF_OpenFont("iosevka-term-regular.ttf", 22);
  bold_font    = TTF_OpenFont("iosevka-term-bold.ttf", 15);
  italic_font  = TTF_OpenFont("iosevka-term-italic.ttf", 15);
  current_font = regular_font;
  text_color = black;

enter_url:
  current_url = text_input("Enter a URL:");
new_page:;
  FILE* html      = url_to_file(current_url);
  htmlDocPtr doc  = parse_html_file(html, current_url);
  node* simple    = simplify_html(doc->last, NULL);
  //print_simplified_html(simple);

  xmlCleanupParser();
  fclose(html);

  scroll_offset = 0;

  SDL_SetWindowTitle(window, window_title);

  url_list* l = malloc(sizeof *l);
  l->full_url = strdup(current_url);
  l->next = history;
  history = l;

  should_rerender_bar = 1;

  SDL_Event e;
  do {
    num_hyperlinks = 0;
    SDL_SetRenderDrawColor(renderer, 242, 233, 234, 255);
    SDL_RenderClear(renderer);
    render_simplified_html(simple);
    draw_bar();
    SDL_RenderPresent(renderer);

    plotter_x = 20;
    plotter_y = scroll_offset + BAR_HEIGHT;

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
          case SDLK_BACKSPACE:
go_back:
            if (history->next)
            {
              current_url = history->next->full_url;
              url_list* n = history->next->next;
              free(history->full_url);
              free(history->next);
              free(history);
              history = n;
              dealloc_nodes(simple);
              xmlFreeDoc(doc);
              goto new_page;
            }
        }
        //printf("Scroll is %i\n", scroll_offset);
        //SDL_FlushEvent(SDL_KEYDOWN);
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_LEFT)
        {
          int x = e.button.x;
          int y = e.button.y;
          if (back.x < x && back.x + back.w > x && back.y < y && back.y + back.h > y)
            goto go_back;
          if (url.x < x && url.x + url.w > x && url.y < y && url.y + url.h > y)
          {
            dealloc_nodes(simple);
            xmlFreeDoc(doc);
            goto enter_url;
          }
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
        break;
      case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_RESIZED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        {
          window_width = e.window.data1;
          window_height = e.window.data2;
          should_rerender_bar = 1;
        }
        break;
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
  //print_tag_hashes();
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

void draw_bar()
{
  static int url_width;
  static SDL_Texture* t1 = NULL;
  static SDL_Texture* t2 = NULL;
  if (should_rerender_bar)
  {
    should_rerender_bar = false;
    int back_text_width;
    int text_height;
    TTF_SizeUTF8(menu_font, " back ", &back_text_width, &text_height);
    TTF_SizeUTF8(menu_font, current_url, &url_width, &text_height);
    bar = (SDL_Rect)  {.x = 0, .y = 0, .w = window_width, .h = BAR_HEIGHT};
    back = (SDL_Rect) {.x = window_width - 90, .y = 10, .w = 80, .h = BAR_HEIGHT - 20};
    url = (SDL_Rect)  {.x = 10, .y = 10, .w = window_width - 110, .h = BAR_HEIGHT - 20};
    backtext = (SDL_Rect) {.x = window_width - 90, .y = 10, .w = back_text_width, .h = text_height};
    if (url_width > window_width - 120) url_width = window_width - 120;
    urltext = (SDL_Rect) {.x = 15, .y = 10, .w = url_width, .h = text_height};
    SDL_DestroyTexture(t1);
    SDL_DestroyTexture(t2);
    SDL_Surface* s1;
    SDL_Surface* s2;
    s1 = TTF_RenderText_Blended(menu_font, " back ", black);
    t1 = SDL_CreateTextureFromSurface(renderer, s1);
    s2 = TTF_RenderText_Blended(menu_font, current_url, black);
    t2 = SDL_CreateTextureFromSurface(renderer, s2);
    SDL_FreeSurface(s1);
    SDL_FreeSurface(s2);
  }
  SDL_SetRenderDrawColor(renderer, 242, 233, 234, 255);
  SDL_RenderFillRect(renderer, &bar);
  SDL_SetRenderDrawColor(renderer, 192, 192, 192, 255);
  SDL_RenderDrawRect(renderer, &bar);
  SDL_RenderDrawRect(renderer, &back);
  SDL_RenderDrawRect(renderer, &url);
  SDL_RenderCopy(renderer, t1, NULL, &backtext);
  SDL_RenderCopy(renderer, t2, NULL, &urltext);
}

char* text_input(char* prompt)
{
  SDL_Window* input_window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 600, 60, 0);
  SDL_Renderer* input_renderer = SDL_CreateRenderer(input_window, -1, SDL_RENDERER_PRESENTVSYNC);
  int prompt_width;
  int prompt_height;
  TTF_SizeUTF8(regular_font, prompt, &prompt_width, &prompt_height);
  SDL_Rect prompt_rect = (SDL_Rect) {.x = 10, .y = 10, .w = prompt_width, .h = prompt_height};
  SDL_Surface* prompt_surface = TTF_RenderText_Blended(regular_font, prompt, black);
  SDL_Texture* prompt_texture = SDL_CreateTextureFromSurface(input_renderer, prompt_surface);
  SDL_SetTextInputRect(&prompt_rect);
  SDL_StartTextInput();
  SDL_Event e;
  char text[1024] ="\0";
  int len = 0;
  for (;;)
  {
    int input_width;
    int input_height;
    TTF_SizeUTF8(regular_font, text, &input_width, &input_height);
    SDL_Rect input_rect = (SDL_Rect) {.x = 10, .y = 10 + prompt_height, .w = input_width, .h = input_height};
    SDL_SetRenderDrawColor(input_renderer, 242, 233, 234, 255);
    SDL_RenderClear(input_renderer);
    SDL_Surface* input_surface = TTF_RenderText_Blended(regular_font, text, black);
    SDL_Texture* input_texture = SDL_CreateTextureFromSurface(input_renderer, input_surface);
    SDL_RenderCopy(input_renderer, prompt_texture, NULL, &prompt_rect);
    SDL_RenderCopy(input_renderer, input_texture, NULL, &input_rect);
    SDL_RenderPresent(input_renderer);
    SDL_DestroyTexture(input_texture);
    SDL_FreeSurface(input_surface);
    SDL_WaitEvent(&e);
    switch (e.type)
    {
      case SDL_TEXTINPUT:
      {
        if (len >= 1022) continue;
        text[len++] = e.text.text[0];
        text[len] = '\0';
        break;
      }
      case SDL_KEYDOWN:
      if (e.key.keysym.sym == SDLK_RETURN)
      {
        SDL_FreeSurface(prompt_surface);
        SDL_DestroyTexture(prompt_texture);
        SDL_DestroyWindow(input_window);
        SDL_DestroyRenderer(input_renderer);
        return strdup(text);
      }
      if (e.key.keysym.sym == SDLK_BACKSPACE && len) text[--len] = '\0';
    }
  }
}
