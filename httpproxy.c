#include "mongoose.h"
#include <curl/curl.h>
#include <stdio.h>

size_t mg_write_data(void *data, size_t size, size_t nmemb, void *userp) {
  mg_send(userp, data, size * nmemb);
  return size * nmemb;
}
static void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *message = (struct mg_http_message *)ev_data;

    char target[message->uri.len];
    for (uint i = 0; i < message->uri.len; i++) {
      target[i] = message->uri.buf[i + 1];
    }
    target[message->uri.len - 1] = 0; // grab everything but the first '/'
    printf("Got request for %s\n", target);

    CURL *handle = curl_easy_init();
    curl_easy_setopt(handle, CURLOPT_URL, target);
    curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, mg_write_data);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, c);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, mg_write_data);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, c);

    int success = curl_easy_perform(handle);
    if (success != CURLE_OK) {
      mg_http_reply(c, 400, "", "Request failed\n");
      return;
    }

    printf("Done\n");
  }
}
int main(void) {
  struct mg_mgr manager;
  mg_mgr_init(&manager);
  mg_http_listen(&manager, "http://0.0.0.0:8000", ev_handler, NULL);
  for (;;) {
    mg_mgr_poll(&manager, 1000);
  }
  return 0;
}
