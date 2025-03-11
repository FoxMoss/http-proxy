#include "mongoose.h"
#include <curl/curl.h>
#include <curl/easy.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ConnectionWrapper {
  struct mg_connection *connection;
  enum Type { TYPE_HEADER, TYPE_BODY } type;
  bool chunked;
  size_t size;
};

size_t mg_write_data(void *data, size_t size, size_t nmemb, void *userp) {
  struct ConnectionWrapper *wrapper = (struct ConnectionWrapper *)userp;
  if (wrapper->type == TYPE_HEADER &&
      strstr(data, "Transfer-Encoding: chunked") != NULL) {

    mg_send(wrapper->connection, data, size * nmemb);
    wrapper->chunked = true;
  } else if (wrapper->type == TYPE_BODY) {
    char hex[256];
    int hex_size = snprintf(hex, 256, "%zx\r\n", size * nmemb);
    mg_send(wrapper->connection, hex, hex_size);
    mg_send(wrapper->connection, data, size * nmemb);
    mg_send(wrapper->connection, "\r\n", 2);

  } else {
    mg_send(wrapper->connection, data, size * nmemb);
  }

  wrapper->size += size * nmemb;
  return size * nmemb;
}

struct str_ptr {
  char *str;
  size_t length;
  size_t offset;
};
size_t mg_read_data(void *returnptr, size_t size, size_t nmemb, void *userp) {
  struct str_ptr *str = (struct str_ptr *)userp;

  size_t read_size = size * nmemb;
  if (str->offset + read_size > str->length) {
    read_size = str->length - str->offset;
  }
  memcpy(returnptr, str->str + str->offset, read_size);
  str->offset += read_size;
  return read_size;
}

static void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *message = (struct mg_http_message *)ev_data;

    size_t len = message->uri.len + 1 + message->query.len;
    if (message->query.len == 0) {
      len = message->uri.len;
    }
    char *target = calloc(len, 1);
    memcpy(target, message->uri.buf + 1, message->uri.len - 1);
    if (message->query.len != 0) {
      target[message->uri.len - 1] = '?';
      memcpy(target + message->uri.len, message->query.buf, message->query.len);
    }

    target[len - 1] = 0;

    CURL *handle = curl_easy_init();

    struct str_ptr str = {message->body.buf, message->body.len, 0};

    size_t i, max = sizeof(message->headers) / sizeof(message->headers[0]);

    struct curl_slist *list = NULL;
    for (i = 0; i < max && message->headers[i].name.len > 0; i++) {
      struct mg_str key = message->headers[i].name;
      struct mg_str value = message->headers[i].value;
      if (mg_strcmp(key, mg_str("Content-type")) == 0) {
        value = mg_str("application/json");
      }

      char *buffer = calloc(key.len + value.len + 3, 1);
      strncat(buffer, key.buf, key.len);
      strcat(buffer, ": ");
      strncat(buffer, value.buf, value.len);
      list = curl_slist_append(list, buffer);
      free(buffer);
    }

    if (mg_strcmp(message->method, mg_str("POST")) == 0) {
      list = curl_slist_append(list, "Content-type: application/json");

      curl_easy_setopt(handle, CURLOPT_READFUNCTION, mg_read_data);
      curl_easy_setopt(handle, CURLOPT_READDATA, &str);

      curl_easy_setopt(handle, CURLOPT_POST, 1);
    }
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, list);

    curl_easy_setopt(handle, CURLOPT_URL, target);
    curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    struct ConnectionWrapper header_wrapper = {c, TYPE_HEADER, false, 0};
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, mg_write_data);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, &header_wrapper);

    struct ConnectionWrapper body_wrapper = {c, TYPE_BODY, false, 0};
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, mg_write_data);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &body_wrapper);

    int success = curl_easy_perform(handle);
    free(target);
    if (success != CURLE_OK) {
      mg_http_reply(c, 400, "", "Request failed\n");
      return;
    }
    curl_easy_cleanup(handle);

    if (header_wrapper.chunked) {
      mg_send(c, "0\r\n\r\n", 5);
    }
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
