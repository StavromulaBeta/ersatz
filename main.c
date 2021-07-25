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
#include "main.h"

CURL* curl_handle;

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

int main()
{
  // Bind error signal handlinjg functions.
  char signals[] = { SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGABRT, SIGBUS, SIGFPE, SIGSEGV, SIGPIPE, SIGTERM, SIGCHLD };
  for (size_t i = 0; i < sizeof(signals); ++i) signal(signals[i], handle_error_signal);
  // Init libcURL
  curl_global_init(CURL_GLOBAL_ALL);
  curl_handle = curl_easy_init();
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:15.0) Gecko/20100101 Firefox/15.0.1");
  curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);     // Disable the progress bar
  curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects
  // Actual code
  FILE* test = url_to_file("www.wikipedia.com");
  char c;
  while ((c = fgetc(test)) != EOF)
  {
    putc(c, stdout);
  }
  fclose(test);
  // Cleanup
  curl_easy_cleanup(curl_handle);
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
  puts("begin backtrace...");
  size_t size = backtrace(trace_array, 10);
  backtrace_symbols_fd(trace_array, size, STDERR_FILENO);
  exit(EXIT_FAILURE);
}

_Noreturn void handle_error_signal(int sig)
{
  throw_error("Recieved signal %i (%s)", sig, strsignal(sig));
}
