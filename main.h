#pragma once
#include <stdio.h>
#include <libxml/HTMLparser.h>
#include <SDL2/SDL_image.h>

#define NUM_HYPERLINKS 1024

typedef enum { text, image, make_bold, remove_bold, make_italic, remove_italic, seperator, hyperlink, end_hyperlink } node_type;

typedef struct _node
{
  node_type type;
  union
  {
    char* text;
    SDL_Surface* image;
    void* data;
  };
  struct _node* next;
} node;

typedef struct _hlink
{
  int x1;
  int y1;
  int x2;
  int y2;
  char* url;
} hlink;

void draw_bar();
FILE* url_to_file(char*);
void dealloc_nodes(node*);
_Noreturn void throw_error(char*, ...);
_Noreturn void handle_error_signal(int);
void bind_error_signals();
node* simplify_html(htmlNodePtr, node*);
unsigned insensitive_hash(const char*);
node* alloc_node(node_type, void*, node*);
void print_simplified_html(node*);
void render_simplified_html(node*);
