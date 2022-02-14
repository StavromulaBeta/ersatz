// TODO ADD COMMENTS

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

#define BAR_HEIGHT 50 // The height of the URL bar

typedef enum { get, post } method_t; // A type of HTTP request

typedef struct _form // An HTML input form
{
  const char* name;
  const char* action;
  method_t method;
} form;

typedef struct _form_list // A linked list of input forms
{
  const struct _form_list* next;
  const form* form;
  SDL_Rect box;
} form_list;

typedef enum // A tag specifying what a node does
{
  text,
  image,
  make_bold,
  remove_bold,
  make_italic,
  remove_italic,
  seperator,
  hyperlink,
  end_hyperlink,
  input,
} node_type;

typedef struct _node // A node is a linked list of instructions to render a page
{
  const struct _node* next;
  node_type type;
  union
  {
    const char* text;
    SDL_Surface* image;
    const form* form;
    const void* data;
  };
} node;

typedef struct _hlink // A hyperlink with a url and a position
{
  SDL_Rect box;
  const char* url;
} hlink;

typedef struct _hlink_list // A list of hyperlinks
{
  struct _hlink_list* next;
  hlink link;
} hlink_list;

typedef struct _url_list // A list of URLs
{
  const struct _url_list *next;
  char* full_url;
} url_list;

static const char* text_input(const char*);
static void draw_bar(void);
static FILE* url_to_file(const char*);
static void dealloc_nodes(const node*);
static void dealloc_forms(const form_list*);
static _Noreturn void throw_error(const char*, ...);
static _Noreturn void handle_error_signal(int);
static void bind_error_signals(void);
static const node* simplify_html(htmlNodePtr, const node*);
static unsigned insensitive_hash(const char*);
static const node* alloc_node(node_type, const void*, const node*);
static void print_simplified_html(const node*);
static void render_simplified_html(const node*);

// The size and position of the back button.
#define BACK_RECT      ((SDL_Rect) {.x = window_width - 90, .y = 10, .w = 80, .h = BAR_HEIGHT - 20})

// The size and position of the url bar.
#define URL_RECT       ((SDL_Rect) {.x = 10, .y = 10, .w = window_width - 110, .h = BAR_HEIGHT - 20})

// If this is one, the url bar will be rerendered next frame
static _Bool should_rerender_bar = 1;

// Linked list of previous urls
static const url_list* history;

// Current url as string
static const char* current_url = NULL;

static CURL* curl_handle;

// Title for window manager
static const char* window_title = "";

// Position to render next element
static int plotter_x = 20;
static int plotter_y = BAR_HEIGHT;

// Offset to plotter_y by scrolling
static int scroll_offset = 0;

static SDL_Renderer* renderer;
static SDL_Window*   window;

// Some colours TODO
#define BLACK    ((SDL_Color) {0, 0, 0, 255})
#define BLUE     ((SDL_Color) {0, 0, 255, 255})
#define GRAY     ((SDL_Color) {128, 128, 128, 255})
#define DARKGRAY ((SDL_Color) {96, 96, 96, 255})

// Colour to render text in
static SDL_Color text_color;
// Font to render text in
static TTF_Font* current_font;

// Possible values for current_font.
static TTF_Font* regular_font;
static TTF_Font* menu_font;
static TTF_Font* bold_font;
static TTF_Font* italic_font;

// All hyperlinks on current page
static hlink_list* hyperlinks = NULL;

// Window dimensions
static int window_width = 640;
static int window_height = 480;

// Width of margins and content between them
#define MARGIN_WIDTH (window_width/8)
#define CONTENT_WIDTH (window_width*6/8)

// All input forms on a page
static const form_list* forms = NULL;

// Possible cursors to set
static SDL_Cursor* default_cursor;
static SDL_Cursor* loading_cursor;

/*
  dealloc_forms() takes a list of forms and deallocates them all
*/
static void dealloc_forms(const form_list* l)
{
  if (l)
  {
    dealloc_forms(l->next);
    if (l->form)
    {
      // TODO
      //if (l->form->action) free((void*)l->form->action);
      //if (l->form->method) free((void*)l->form->method);
      //free((void*)l->form);
    }
    free((void*)l);
  }
}

/*
  dealloc_links() takes a list of hyperlinks and deallocates them all
*/
static void dealloc_links(const hlink_list* l)
{
  if (!l) return;
  dealloc_links(l->next);
  free((void*)l);
}

/*
  start_loading() sets the cursor to the spinny boi
*/
static void start_loading(void)
{
  SDL_SetCursor(loading_cursor);
}

/*
  stop_loading() sets the cursor to the pointy one
*/
static void stop_loading(void)
{
  SDL_SetCursor(default_cursor);
}

/*
  does_intersect_with() returns true when x,y is inside rect r, false otherwise
*/
static _Bool does_intersect_rect(int x, int y, SDL_Rect r)
{
  return (x >= r.x && y >= r.y && x <= r.x + r.w && y <= r.y + r.h);
}

/*
  is_on_screen() returns true if x,y is on the screen, false otherwise
*/
static int is_on_screen(int y1, int y2)
{
  return y1 > scroll_offset || y2 < scroll_offset + window_height;
}

/*
  add_hyperlink() adds a hyperlink with x, y, and url to the global hyperlink list
*/
static void add_hyperlink(const char* url, int x, int y, int w, int h)
{
  hlink_list* l = malloc(sizeof *l);
  l->link = (hlink) {.url=url, .box=(SDL_Rect){.x=x,.y=y,.w=w,.h=h}};
  l->next = hyperlinks;
  hyperlinks = l;
}

/*
  add_urls() appends two relative or nonrelative urls
*/
static char* add_urls(const char* url1, const char* url2)
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

/*
  render_text() draws text to the screen in a specified font. 'render' specifies whether to actually draw or simulate drawing.
  This implements a home-grown organic text wrapping algorithm.
*/
static void render_text(const char* static_text, SDL_Renderer* renderer, TTF_Font* font, bool render)
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
    int wrap_chars = (window_width - plotter_x - MARGIN_WIDTH) / char_width; // Calculate how many pixels we have to work with
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
      plotter_x = MARGIN_WIDTH;
      text += linebreak_pos + 1;
    }
    else plotter_x += char_width * len; // ...or not
    first_iteration = false;
  } while (more_lines);
  free(start);
}

/*
  print_tag_hashes() is a debugging function to print the hashes for the html tags so i can put them in a switch.
*/
static void print_tag_hashes(void)
{
  char* arr[] = {"TR", "TD", "TH", "TITLE", "P", "A", "I", "B", "BR", "SCRIPT", "STYLE", "EM", "H1", "H2", "H3", "H4", "H5", "H6", "IMG", "FORM", "INPUT"};
  for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); ++i)
  {
    printf("#define %s_TAG %u\n", arr[i], insensitive_hash(arr[i]));
  }
}

/*
  url_to_file() downloads to url to a temporary file, returning a file handle.
  There is no caching.
  There are no parallel downloads.
*/
static FILE* url_to_file(const char* url)
{
  // Writes data from a URL to a file.
  fprintf(stderr, "Downloading %s... ", url);
  FILE* out_file = tmpfile(); // Make a temporary file
  if (!out_file) throw_error("Cannot load URL %s", url);
  curl_easy_setopt(curl_handle, CURLOPT_URL, url);            // Set the URL
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, out_file); // Set output file
  int err = 0, retries = 0;
  do {
    err = curl_easy_perform(curl_handle);
    if (retries++ == 5) throw_error((char*)curl_easy_strerror(err));
  } while (err);
  rewind(out_file);
  fputs("Done.\n", stderr);
  return out_file;
}

/*
  parse_html_file() reads a file pointer and parses it with libxml
*/
static htmlDocPtr parse_html_file(FILE* fp, const char* url)
{
  xmlSubstituteEntitiesDefault(true);
  htmlDocPtr doc = htmlReadFd(fileno(fp), url, NULL, HTML_PARSE_NOBLANKS | HTML_PARSE_NONET);
  if (!doc) throw_error("Cannot parse file");
  return doc;
}

/*
  simplify_html() converts libxml's ast into my own. The second argument specifies the next node.
*/
static const node* simplify_html(htmlNodePtr ptr, const node* head)
{
  // ptr is the LAST node, and we iterate BACKWARDS, so our linked list is forwards
  static const char* form_action;
  static method_t form_method;
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
#define FORM_TAG 3234988420
#define INPUT_TAG 293375786
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
                char* src = (char*)xmlGetProp(ptr, (xmlChar*)"src");
                if (!src) goto err;
                char* full_url = add_urls(current_url, src);
                FILE* img = url_to_file(full_url);
                SDL_RWops* rw = SDL_RWFromFP(img, 0);
                SDL_Surface* surface = IMG_Load_RW(rw, 0);
                fclose(img);
                free(src);
                free(rw);
                free(full_url);
                if (!surface) goto err;
                head = alloc_node(image, surface,
                       simplify_html(ptr->last, head));
              }
              break;
            case FORM_TAG:
            {
              form_action = (char*)xmlGetProp(ptr, (xmlChar*)"action");
              char* met = (char*)xmlGetProp(ptr, (xmlChar*)"method");
              form_method = !met || met[0] == 'g' ? get : post;
              head = simplify_html(ptr->last, head);
            }
            break;
            case INPUT_TAG:
            {
              char* name = (char*)xmlGetProp(ptr, (xmlChar*)"name");
              char* type = (char*)xmlGetProp(ptr, (xmlChar*)"type");
              if (type && strcmp(type, "text") && strcmp(type, "search")) goto err;
              // Assume type is text because nobody actually uses checkboxes.
              form* f = malloc(sizeof *f);
              f->action = form_action;
              f->name = name;
              f->method = form_method;
              head = alloc_node(input, f,
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
      default: err: head = simplify_html(ptr->last, head); break;
    }
  }
  return head;
}

/*
  alloc_node() simply allocates a node in memory, with arguments for the struct fields
*/
const node* alloc_node(node_type type, const void* data, const node* next)
{
  node* n = malloc(sizeof(*n));
  *n = (node){.type = type, .data = data, .next = next};
  return n;
}

/*
  print_simplified_html() is a debugging function to print a simplified html data structure.
*/
void print_simplified_html(const node* ptr)
{
  for (; ptr ; ptr = ptr->next)
  {
    switch (ptr->type)
    {
      case text          : puts(ptr->text); break;
      case seperator     : puts("[SEPERATOR]"); break;
      case make_bold     : puts("[BEGIN BOLD]"); break;
      case remove_bold   : puts("[END BOLD]"); break;
      case make_italic   : puts("[BEGIN ITALIC]"); break;
      case remove_italic : puts("[END ITALIC]"); break;
      case hyperlink     : printf("[HYPERLINK TO %s]\n", ptr->text); break;
      case end_hyperlink : puts("[END HYPERLINK]"); break;
      case image         : printf("[IMAGE AT %p]\n", (void*)ptr->image); break;
      default            : break;
    }
  }
}

/*
  render_simplified_html() renders the simplified html data structure to the screen, including images.
  It adds hyperlinks and forms to their global lists respectively.
  It adjusts for scrolling.
*/
void render_simplified_html(const node* ptr)
{
  bool is_seperated = false;
  int x, y, w, h;
  const char* url; // hyperlink stuff
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
          plotter_y += TTF_FontHeight(regular_font) + 25;
          plotter_x = MARGIN_WIDTH;
          if (render)
          {
            SDL_SetRenderDrawColor(renderer, 192, 192, 192, SDL_ALPHA_OPAQUE);
            SDL_RenderDrawLine(renderer, MARGIN_WIDTH/2, plotter_y, window_width - MARGIN_WIDTH/2, plotter_y);
          }
          plotter_y += 25;
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
        x = plotter_x;
        y = plotter_y;
        url = ptr->text;
        text_color = BLUE;
        break;
      case end_hyperlink:
        if (render)
        {
          h = TTF_FontHeight(current_font);
          w = plotter_x - x;
          if (plotter_y > y)
          {
            // Multiline hyperlink
            add_hyperlink(url, x, y, window_width - MARGIN_WIDTH - x, h);
            if (plotter_y - h - y > 0) add_hyperlink(url, MARGIN_WIDTH, y + h, window_width - MARGIN_WIDTH, plotter_y - h - y);
            add_hyperlink(url, MARGIN_WIDTH, plotter_y, plotter_x - MARGIN_WIDTH, h);
          }
          else add_hyperlink(url, x, y, w, h);
        }
        text_color = BLACK;
        break;
      case image:
        {
          if (plotter_x > MARGIN_WIDTH) plotter_y += TTF_FontHeight(current_font) + 10;
          plotter_x = MARGIN_WIDTH;
          int image_width = ptr->image->w;
          int image_height = ptr->image->h;
          if (image_width > (window_width - MARGIN_WIDTH*2))
          {
            image_height *= (window_width - MARGIN_WIDTH*2);
            image_height /= image_width;
            image_width = window_width - MARGIN_WIDTH*2;
          }
          if (render)
          {
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, ptr->image);
            SDL_Rect rect = {MARGIN_WIDTH, plotter_y, image_width, image_height};
            SDL_RenderCopy(renderer, texture, NULL, &rect);
            SDL_DestroyTexture(texture);
          }
          plotter_y += image_height + 10;
        }
        break;
      case input:
      {
        int height = TTF_FontHeight(regular_font);
        const form* f = ptr->form;
        form_list* fl = malloc(sizeof *fl);
        if (plotter_x > MARGIN_WIDTH) plotter_y += height;
        plotter_x = MARGIN_WIDTH;
        fl->form = f;
        fl->next = forms;
        fl->box.x = MARGIN_WIDTH;
        fl->box.y = plotter_y + height/2;
        fl->box.w = CONTENT_WIDTH;
        fl->box.h = height;
        plotter_y += height * 2;
        forms = fl;
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(renderer, &fl->box);
      }
      break;
      default:
      break;
    }
    if (ptr->type == seperator) is_seperated = true;
    else is_seperated = false;
  }
}

/*
  init_curl() inits curl innit.
*/
static void init_curl(void)
{
  curl_global_init(CURL_GLOBAL_ALL);
  curl_handle = curl_easy_init();
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "Ersatz/0.0.1");
  curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);     // Disable the progress bar
  curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects
}

/*
  init_sdl() inits sdl innit.
*/
static void init_sdl(void)
{
  if (SDL_Init(SDL_INIT_EVERYTHING)) throw_error("Failed to initialise the SDL window");
  if (TTF_Init()) throw_error("Failed to initialise SDL_TTF");
  if (IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_TIF | IMG_INIT_WEBP) == 0) throw_error("Failed to init images");
  window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height, SDL_WINDOW_RESIZABLE);
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
}

/*
  init_fonts() inits fonts innit.
*/
static void init_fonts(void)
{
  regular_font = TTF_OpenFont("iosevka-term-regular.ttf", 15);
  menu_font    = TTF_OpenFont("iosevka-term-regular.ttf", 22);
  bold_font    = TTF_OpenFont("iosevka-term-bold.ttf", 15);
  italic_font  = TTF_OpenFont("iosevka-term-italic.ttf", 15);
  current_font = regular_font;
  text_color = BLACK;
}

/*
  init_cursors() inits cursors innit.
*/
static void init_cursors(void)
{
  default_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
  loading_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
}

/*
  The main function calls all the other functions.
  It includes the main event loop.
  It has internal control flow using goto statements.
  This was a mistake.
*/
int main(int argc, char** argv)
{
  bind_error_signals();
  init_curl();
  init_sdl();
  init_fonts();
  init_cursors();

  if (argc > 1) current_url = argv[1];
  else
    enter_url: current_url = text_input("Enter a URL:");

new_page:;
  start_loading();

  dealloc_forms(forms);
  forms = NULL;

  dealloc_links(hyperlinks);
  hyperlinks = NULL;

  FILE* html = url_to_file(current_url);

  {
    char* buf;
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &buf);
    current_url = strdup(buf);
  }

  htmlDocPtr doc  = parse_html_file(html, current_url);

  fclose(html);
  xmlCleanupParser();

  const node* simple = simplify_html(doc->last, NULL);

  //print_simplified_html(simple);

  scroll_offset = 0;

  SDL_SetWindowTitle(window, window_title);

  {
    url_list* l = malloc(sizeof *l);
    l->full_url = strdup(current_url);
    l->next = history;
    history = l;
  }

  should_rerender_bar = 1;

  stop_loading();

  SDL_Event e;
  do {
    SDL_SetRenderDrawColor(renderer, 242, 233, 234, 255);
    SDL_RenderClear(renderer);
    render_simplified_html(simple);
    draw_bar();
    SDL_RenderPresent(renderer);

    plotter_x = MARGIN_WIDTH;
    plotter_y = scroll_offset + BAR_HEIGHT;

    SDL_PollEvent(&e);
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
              const url_list* n = history->next->next;
              free((void*)history->full_url);
              free((void*)history->next);
              free((void*)history);
              history = n;
              dealloc_nodes(simple);
              xmlFreeDoc(doc);
              goto new_page;
            }
        }
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_LEFT)
        {
          int x = e.button.x;
          int y = e.button.y;
          if (does_intersect_rect(x, y, BACK_RECT))
            goto go_back;
          if (does_intersect_rect(x, y, URL_RECT))
          {
            dealloc_nodes(simple);
            xmlFreeDoc(doc);
            goto enter_url;
          }
          for (const form_list* l = forms; l; l = l->next)
          {
            if (does_intersect_rect(x, y, l->box))
            {
              // Time to make a request.
              static char buf[1024] = "";
              const form* f = l->form;
              const char* url = add_urls(current_url, f->action);
              const char* inp = text_input(url);
              char* inp_esc = curl_easy_escape(curl_handle, inp, strlen(inp));
              if (f->method == post)
              {
                sprintf(buf, "%s=%s", f->name, inp_esc);
                curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, buf);
              }
              else
              {
                sprintf(buf, "%s?%s=%s", url, f->name, inp_esc);
                url = buf;
              }
              current_url = url;
              free((void*)inp);
              curl_free(inp_esc);
              dealloc_nodes(simple);
              xmlFreeDoc(doc);
              goto new_page;
            }
          }
          for (hlink_list* l = hyperlinks; l; l=l->next)
          {
            hlink h = l->link;
            if (does_intersect_rect(x, y, h.box))
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
  SDL_FreeCursor(default_cursor);
  SDL_FreeCursor(loading_cursor);
  SDL_Quit();
  curl_easy_cleanup(curl_handle);
  //print_tag_hashes();
  return EXIT_SUCCESS;
}

/*
  dealloc_nodes() deallocates a list of nodes.
  It also tries to deallocate their contents.
*/
void dealloc_nodes(const node* n)
{
  if (n)
  {
    dealloc_nodes(n->next);
    if (n->type == image) SDL_FreeSurface(n->image);
    else if (n->type == hyperlink) free((void*)n->text);
    free((void*)n);
  }
}

/*
  throw_error() prints a pretty error message.
*/
__attribute__((format(printf, 1, 2))) void throw_error(const char* fmt, ...)
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

/*
  bind_signals() binds each signal to the signal handler function.
*/
void bind_error_signals()
{
  char signals[] = { SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGABRT, SIGBUS, SIGFPE, SIGSEGV, SIGPIPE, SIGTERM, SIGCHLD };
  for (size_t i = 0; i < sizeof(signals); ++i) signal(signals[i], handle_error_signal);
}

/*
  handle_error_signal() is a signal handler that just prints an error.
*/
_Noreturn void handle_error_signal(int sig)
{
  throw_error("Recieved signal %i (%s)", sig, strsignal(sig));
}

/*
  insensitive_hash() generates a unique integer from a string so we can switch on html tags
*/
unsigned int insensitive_hash(const char *str)
{
  // http://www.cse.yorku.ca/~oz/hash.html
  unsigned int hash = 0;
  int c;
  while ((c = tolower(*str++)))
    hash = c + ((long)hash << 6) + ((long)hash << 16) - hash;
  return hash;
}

/*
  draw_bar() draws the bar at the top of the screen, with the current url and a back button.
  It uses the should_rerender_bar variable - which is set if the entire bar needs to be redrawn
*/
void draw_bar()
{
  static int url_width;
  static int back_text_width;
  static int back_text_height;
  static SDL_Texture* t1 = NULL;
  static SDL_Texture* t2 = NULL;
  const char* back_button_text = " back ";
  if (should_rerender_bar)
  {
    should_rerender_bar = false;
    if (url_width > window_width - 120) url_width = window_width - 120;
    TTF_SizeUTF8(menu_font, back_button_text, &back_text_width, &back_text_height);
    SDL_DestroyTexture(t1);
    SDL_DestroyTexture(t2);
    SDL_Surface* s1;
    SDL_Surface* s2;
    s1 = TTF_RenderText_Blended(menu_font, back_button_text, BLACK);
    t1 = SDL_CreateTextureFromSurface(renderer, s1);
    s2 = TTF_RenderText_Blended(menu_font, current_url, BLACK);
    t2 = SDL_CreateTextureFromSurface(renderer, s2);
    SDL_FreeSurface(s1);
    SDL_FreeSurface(s2);
  }

  const SDL_Rect back_rect = BACK_RECT;
  const SDL_Rect url_rect  = URL_RECT;

  const SDL_Rect bar_rect       = ((SDL_Rect){.x = 0, .y = 0, .w = window_width, .h = 50});
  const SDL_Rect back_text_rect = (SDL_Rect){.x = window_width - 90, .y = 10, .w = back_text_width, .h = back_text_height};
  const SDL_Rect url_text_rect  = ((SDL_Rect){.x = 15, .y = 10, .w = url_width, .h = 22});

  SDL_SetRenderDrawColor(renderer, 242, 233, 234, 255);
  SDL_RenderFillRect(renderer, &bar_rect);
  SDL_SetRenderDrawColor(renderer, 192, 192, 192, 255);
  SDL_RenderDrawRect(renderer, &bar_rect);
  SDL_RenderDrawRect(renderer, &back_rect);
  SDL_RenderDrawRect(renderer, &url_rect);
  SDL_RenderCopy(renderer, t1, NULL, &back_text_rect);
  SDL_RenderCopy(renderer, t2, NULL, &url_text_rect);
}

/*
  text_input() opens a prompt, and the user will input text then press enter.
  The entered text is returned as a string.
*/
const char* text_input(const char* prompt)
{
  SDL_Window* input_window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 600, 60, 0);
  SDL_Renderer* input_renderer = SDL_CreateRenderer(input_window, -1, SDL_RENDERER_PRESENTVSYNC);
  int prompt_width;
  int prompt_height;
  TTF_SizeUTF8(regular_font, prompt, &prompt_width, &prompt_height);
  SDL_Rect prompt_rect = (SDL_Rect) {.x = 10, .y = 10, .w = prompt_width, .h = prompt_height};
  SDL_Surface* prompt_surface = TTF_RenderText_Blended(regular_font, prompt, BLACK);
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
    SDL_Surface* input_surface = TTF_RenderText_Blended(regular_font, text, BLACK);
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
