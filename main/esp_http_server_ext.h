/*
 * esp_http_server_ext.h
 *
 * A more efficient alternatives of some esp_http_server APIs.
 * Memory copies are replaced by returning references where possible.
 */

#ifndef MAIN_ESP_HTTP_SERVER_EXT_H_
#define MAIN_ESP_HTTP_SERVER_EXT_H_

#include <http_parser.h>
#include <esp_http_server.h>

// The offset of "struct http_parser_url url_parse_res;" in "struct httpd_req_aux", in bytes.
// See esp_httpd_priv.h
#define AUX_OFFSET 1060

static inline struct http_parser_url* httpd_url_from_req(httpd_req_t *req) {
    return (struct http_parser_url*) (((char*) req->aux) + AUX_OFFSET);
}

// Return the url string, the returned string should not be modified.
// str_len outputs the length of the url, including the null terminator.
const char* httpd_req_get_url_query_str_byref(httpd_req_t *r, size_t* str_len);
// Return the value (CAREFUL: not null terminated), the returned string should not be modified.
// str_len outputs the length of the value string, excluding the null terminator.
const char* httpd_query_key_value_byref(const char *qry_str, const char *key, size_t* strlen_out);
// Recover the non-printable chars in the raw request, null-terminated output will be placed in a decoded.
// rawlen is the length of the raw string, excluding the null terminator.
// decoded should be at least rawlen+1.
size_t httpd_query_value_decode(const char* raw, size_t rawlen, char* decoded);

#endif /* MAIN_ESP_HTTP_SERVER_EXT_H_ */
