#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <curl/curl.h>

CURL* curl_handle;

static size_t curl_write_fn(void *start, size_t sz, size_t bytes, void *file)
{
  // Used by curl to write to a file.
  return fwrite(start, sz, bytes, file);
}

void url_to_file(char* url, char* filename)
{
  // Writes data from a URL to a file.
  FILE* out_file = fopen(filename, "wb");
  if (!out_file) { fprintf(stderr, "Cannot load URL to file!\n"); exit(EXIT_FAILURE); }
  curl_easy_setopt(curl_handle, CURLOPT_URL, url);                     // Set the URL
  curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);               // Disable the progress bar
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, curl_write_fn); // Use my write function
  curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);           // Follow redirects
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, out_file);          // Set output file
  curl_easy_perform(curl_handle);
  fclose(out_file);
}

int main()
{
  curl_global_init(CURL_GLOBAL_ALL);
  curl_handle = curl_easy_init();
  url_to_file("www.wikipedia.com", "wikipedia.out");
  curl_easy_cleanup(curl_handle);
}
