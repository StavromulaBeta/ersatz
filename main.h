#pragma once

FILE* url_to_file(char*);
_Noreturn void throw_error(char*, ...);
_Noreturn void handle_error_signal(int);
