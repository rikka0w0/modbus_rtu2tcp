#include <stdlib.h>
#include <strings.h>
#include <sys/param.h>

#include "esp_err.h"
#include "esp_log.h"

#include "esp_http_server_ext.h"

const char* httpd_req_get_url_query_str_byref(httpd_req_t *r, size_t* str_len) {
    if (r == NULL) {
        return NULL;
    }

    struct http_parser_url *res = httpd_url_from_req(r);

    /* Check if query field is present in the URL */
    if (res->field_set & (1 << UF_QUERY)) {
        const char *qry = r->uri + res->field_data[UF_QUERY].off;

        /* Minimum required buffer len for keeping
         * null terminated query string */
        size_t min_buf_len = res->field_data[UF_QUERY].len + 1;

        *str_len = min_buf_len;
        return qry;
    }

    return NULL;
}

/* Helper function to get a URL query tag from a query string of the type param1=val1&param2=val2 */
const char* httpd_query_key_value_byref(const char *qry_str, const char *key, size_t* strlen_out) {
    if (qry_str == NULL || key == NULL) {
        return NULL;
    }

    const char   *qry_ptr = qry_str;

    while (strlen(qry_ptr)) {
        /* Search for the '=' character. Else, it would mean
         * that the parameter is invalid */
        const char *val_ptr = strchr(qry_ptr, '=');
        if (!val_ptr) {
            break;
        }
        size_t offset = val_ptr - qry_ptr;

        /* If the key, does not match, continue searching.
         * Compare lengths first as key from url is not
         * null terminated (has '=' in the end) */
        if ((offset != strlen(key)) ||
            (strncasecmp(qry_ptr, key, offset))) {
            /* Get the name=val string. Multiple name=value pairs
             * are separated by '&' */
            qry_ptr = strchr(val_ptr, '&');
            if (!qry_ptr) {
                break;
            }
            qry_ptr++;
            continue;
        }

        /* Locate start of next query */
        qry_ptr = strchr(++val_ptr, '&');
        /* Or this could be the last query, in which
         * case get to the end of query string */
        if (!qry_ptr) {
            qry_ptr = val_ptr + strlen(val_ptr);
        }

        // Output the result
        // Exclude the NULL terminator
        *strlen_out = qry_ptr - val_ptr;
        return val_ptr;
    }

    return NULL;
}

static uint8_t hexstr_to_num(char c) {
    if(c>='0' && c<='9'){
        return c - '0';
    } else if(c>='a' && c<='f'){
        return c - 'a' + 10;
    } else if(c>='A' && c<='F'){
        return c - 'A' + 10;
    } else {
        return 255;
    }
}

// rawlen is the size of the raw input, also the size of decoded buffer.
// The result (decoded) should be shorter than the raw input.
// This function returns the actual length of the decoded string, including \0.
size_t httpd_query_value_decode(const char* raw, size_t rawlen, char* decoded) {
    size_t rawpos = 0;
    size_t outlen = 0;

    while (rawpos < rawlen) {
        if (raw[rawpos] == '%') {
            rawpos++;
            decoded[outlen] = hexstr_to_num(raw[rawpos++]) << 4;
            decoded[outlen] |= hexstr_to_num(raw[rawpos++]);
        } else {
            decoded[outlen] = raw[rawpos++];
        }
        outlen++;
    }

    // Append terminator
    decoded[outlen] = '\0';
    return outlen + 1;
}
