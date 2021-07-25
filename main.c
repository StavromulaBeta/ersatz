#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <curl/curl.h>

CURL* curl_handle;

void url_to_file(char* url, char* filename)
{
  // Writes data from a URL to a file.
  FILE* out_file = fopen(filename, "wb");
  if (!out_file) { fprintf(stderr, "Cannot load URL to file!\n"); exit(EXIT_FAILURE); }
  curl_easy_setopt(curl_handle, CURLOPT_URL, url);            // Set the URL
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, out_file); // Set output file
  curl_easy_perform(curl_handle);
  fclose(out_file);
}

int main()
{
  curl_global_init(CURL_GLOBAL_ALL);
  curl_handle = curl_easy_init();
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:15.0) Gecko/20100101 Firefox/15.0.1");
  curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);     // Disable the progress bar
  curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects
  url_to_file("upload.wikimedia.org/wikipedia/commons/6/68/Barnard_33.jpg", "horsehead.jpg");
  curl_easy_cleanup(curl_handle);
}
