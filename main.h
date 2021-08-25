#pragma once
#include <stdio.h>
#include <libxml/HTMLparser.h>
#include <SDL2/SDL_image.h>

typedef enum { text, image, make_bold, remove_bold, make_italic, remove_italic, seperator, hyperlink, end_hyperlink } node_type;

typedef struct node
{
  node_type type;
  union
  {
    char* text;
    SDL_Surface* image;
    void* data;
  };
  struct node* next;
} node;

FILE* url_to_file(char*);
_Noreturn void throw_error(char*, ...);
_Noreturn void handle_error_signal(int);
void bind_error_signals();
node* simplify_html(htmlNodePtr, node*);
int insensitive_hash(const char*);
node* alloc_node(node_type, void*, node*);
void print_simplified_html(node*);
void render_simplified_html(node*);
