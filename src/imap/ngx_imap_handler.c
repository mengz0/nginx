
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_imap.h>
#include <nginx.h>


static void ngx_imap_init_session(ngx_event_t *rev);
static void ngx_pop3_auth_state(ngx_event_t *rev);
static ngx_int_t ngx_pop3_read_command(ngx_imap_session_t *s);


static u_char pop3_greeting[] = "+OK " NGINX_VER " ready" CRLF;
static u_char imap_greeting[] = "* OK " NGINX_VER " ready" CRLF;

static u_char pop3_ok[] = "+OK" CRLF;
static u_char pop3_invalid_command[] = "-ERR invalid command" CRLF;


void ngx_imap_init_connection(ngx_connection_t *c)
{
    u_char   *greeting;
    ssize_t   size;

    ngx_log_debug0(NGX_LOG_DEBUG_IMAP, c->log, 0,
                   "imap init connection");

    c->log_error = NGX_ERROR_INFO;

    greeting = pop3_greeting;
    size = sizeof(pop3_greeting) - 1;

    if (ngx_send(c, greeting, size) < size) {
        /*
         * we treat the incomplete sending as NGX_ERROR
         * because it is very strange here
         */
        ngx_imap_close_connection(c);
        return;
    }

    c->read->event_handler = ngx_imap_init_session;

    ngx_add_timer(c->read, /* STUB */ 60000);

    if (ngx_handle_read_event(c->read, 0) == NGX_ERROR) {
        ngx_imap_close_connection(c);
    }
}


static void ngx_imap_init_session(ngx_event_t *rev)
{
    size_t               size;
    ngx_connection_t    *c;
    ngx_imap_session_t  *s;

    c = rev->data;

    /* TODO: timeout */

    if (!(s = ngx_pcalloc(c->pool, sizeof(ngx_imap_session_t)))) {
        ngx_imap_close_connection(c);
        return;
    }

    c->data = s;
    s->connection = c;

    if (ngx_array_init(&s->args, c->pool, 2, sizeof(ngx_str_t)) == NGX_ERROR) {
        ngx_imap_close_connection(s->connection);
        return;
    }

    size = /* STUB: pop3: 128, imap: configurable 4K default */ 128;

    s->buffer = ngx_create_temp_buf(s->connection->pool, size);
    if (s->buffer == NULL) {
        ngx_imap_close_connection(s->connection);
        return;
    }

    ngx_pop3_auth_state(rev);
}


static void ngx_pop3_auth_state(ngx_event_t *rev)
{
    u_char              *text;
    ssize_t              size;
    ngx_int_t            rc;
    ngx_connection_t    *c;
    ngx_imap_session_t  *s;

    c = rev->data;
    s = c->data;

    /* TODO: timeout */

    rc = ngx_pop3_read_command(s);

    if (rc == NGX_AGAIN || rc == NGX_ERROR) {
        return;
    }

    s->state = 0;

    if (rc == NGX_IMAP_PARSE_INVALID_COMMAND) {
        text = pop3_invalid_command;
        size = sizeof(pop3_invalid_command) - 1;

    } else {
        text = pop3_ok;
        size = sizeof(pop3_ok) - 1;
    }

    if (ngx_send(c, text, size) < size) {
        /*
         * we treat the incomplete sending as NGX_ERROR
         * because it is very strange here
         */
        ngx_imap_close_connection(c);
        return;
    }
}


static ngx_int_t ngx_pop3_read_command(ngx_imap_session_t *s)
{
    ssize_t    n;
    ngx_int_t  rc;

    n = ngx_recv(s->connection, s->buffer->last,
                 s->buffer->end - s->buffer->last);

    if (n == NGX_ERROR || n == 0) {
        ngx_imap_close_connection(s->connection);
        return NGX_ERROR;
    }

    if (n > 0) {
        s->buffer->last += n;
    }

    if (n == NGX_AGAIN) {
        if (ngx_handle_read_event(s->connection->read, 0) == NGX_ERROR) {
            ngx_imap_close_connection(s->connection);
            return NGX_ERROR;
        }

        return NGX_AGAIN;
    }

    rc = ngx_pop3_parse_command(s);

    if (rc == NGX_AGAIN || rc == NGX_IMAP_PARSE_INVALID_COMMAND) {
        return rc;
    }

    if (rc == NGX_ERROR) {
        ngx_imap_close_connection(s->connection);
        return NGX_ERROR;
    }

    return NGX_OK;
}


#if 0

void ngx_imap_close_session(ngx_imap_session_t *s)
{
    ngx_log_debug0(NGX_LOG_DEBUG_IMAP, s->connection->log, 0,
                   "close imap session");

    ngx_imap_close_connection(s->connection);
}

#endif


void ngx_imap_close_connection(ngx_connection_t *c)
{
    ngx_log_debug1(NGX_LOG_DEBUG_IMAP, c->log, 0,
                   "close imap connection: %d", c->fd);

    ngx_close_connection(c);
}
