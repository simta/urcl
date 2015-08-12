/* This file is part of urcl and distributed under the terms of the
 * MIT license. See COPYING.
 */

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <hiredis/hiredis.h>

#include "urcl.h"

struct urcl_host {
    char                *h_ip;
    int                 h_port;
    struct urcl_host    *h_next;
    struct urcl_host    *h_prev;
};

struct urcl {
    redisContext        *rc;
    char                *hostname;
    int                 port;
    struct urcl_host    *host;
};

static int urcl_host_insert( URCL *, const char *, int );
static int urcl_checkconnection( URCL * );
static int urcl_reconnect( URCL * );
static int urcl_redirect( URCL *, char * );

    URCL *
urcl_connect( const char *host, int port )
{
    URCL                *r;
    struct addrinfo     hints;
    struct addrinfo     *air;
    struct addrinfo     *ai;
    char                hbuf[ NI_MAXHOST ];
    int                 rc;

    if (( r = calloc( 1, sizeof( struct urcl ))) == NULL ) {
        return( NULL );
    }

    if (( r->hostname = strdup( host )) == NULL ) {
        goto cleanup;
    }

    r->port = port;

    memset( &hints, 0, sizeof( struct addrinfo ));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;

    if (( rc = getaddrinfo( r->hostname, NULL, &hints, &air ))) {
        goto cleanup;
    }

    for ( ai = air; ai != NULL; ai = ai->ai_next ) {
        if (( rc = getnameinfo( ai->ai_addr, ai->ai_addrlen,
                hbuf, sizeof( hbuf ), NULL, 0, NI_NUMERICHOST )) == 0 ) {
            urcl_host_insert( r, hbuf, r->port );
        }
    }

    freeaddrinfo( air );

    if ( urcl_reconnect( r ) != 0 ) {
        goto cleanup;
    }

    return( r );

cleanup:
    free( r->host );
    free( r );
    return( NULL );
}

    static int
urcl_host_insert( URCL *r, const char *ip, int port )
{
    struct urcl_host        *he;

    if ( r->host == NULL ) {
        r->host = calloc( 1, sizeof( struct urcl_host ));
        r->host->h_next = r->host;
        r->host->h_prev = r->host;
        he = r->host;
    } else {
        /* Check for an existing entry */
        he = r->host;
        do {
            if (( strcmp( he->h_ip, ip ) == 0 ) &&
                    ( he->h_port == port )) {
                r->host = he;
                return( 0 );
            }
            he = he->h_next;
        } while ( he != r->host );

        /* Insert a new entry */
        he = calloc( 1, sizeof( struct urcl_host ));
        he->h_next = r->host->h_next;
        he->h_prev = r->host;
        he->h_next->h_prev = he;
        he->h_prev->h_next = he;
    }

    he->h_ip = strdup( ip );
    he->h_port = port;
    r->host = he;
    return( 0 );
}

    static int
urcl_redirect( URCL *r, char *err )
{
    char    *host, *port;

    if (( strncmp( err, "MOVED ", 6 ) == 0 ) ||
            ( strncmp( err, "ASK ", 4 ) == 0 )) {
        /* redis cluster, need to connect to a different node */
        host = strchr( err, ' ' ) + 1;
        host = strchr( host, ' ' ) + 1;
        port = strchr( host, ':' );
        *port = '\0';
        urcl_host_insert( r, host, atoi( port + 1 ));
        *port = ':';
        urcl_reconnect( r );
        /* FIXME: If it's an ASK redirect we should send ASKING */
        return( 1 );
    }
    return( 0 );
}

    static int
urcl_reconnect( URCL *r )
{
    struct urcl_host    *host;

    if ( r->rc != NULL ) {
        redisFree( r->rc );
    }

    host = r->host;
    do {
        r->rc = redisConnect( host->h_ip, host->h_port );
        if ( r->rc ) {
            if ( r->rc->err ) {
                redisFree( r->rc );
                r->rc = NULL;
            } else {
                r->host = host;
                return( 0 );
            }
        }
        host = host->h_next;
    } while ( host != r->host );

    return( 1 );
}

    static int
urcl_checkconnection( URCL *r )
{
    if ( r->rc == NULL && ( urcl_reconnect( r ) != 0 )) {
        return( 1 );
    }
    return( 0 );
}

    int
urcl_set( URCL *r, const char *key, const char *value )
{
    int         ret = 1;
    redisReply  *res = NULL;

    do {
        freeReplyObject( res );

        if ( urcl_checkconnection( r )) {
            return( 1 );
        }

        if (( res = redisCommand( r->rc, "SET %s %s", key, value )) == NULL ) {
            redisFree( r->rc );
            r->rc = NULL;
            return( 1 );
        }
    } while (( res->type == REDIS_REPLY_ERROR ) &&
            urcl_redirect( r, res->str ));

    if (( res->type == REDIS_REPLY_STRING ) && ( res->len == 2 ) &&
            ( memcmp( res->str, "OK", 2 ) == 0 )) {
        ret = 0;
    }

    freeReplyObject( res );
    return( ret );
}

    int
urcl_hset( URCL *r, const char *key, const char *field, const char *value )
{
    int         ret = 1;
    redisReply  *res = NULL;

    do {
        freeReplyObject( res );

        if ( urcl_checkconnection( r )) {
            return( 1 );
        }

        if (( res = redisCommand( r->rc, "HSET %s %s %s",
                key, field, value )) == NULL ) {
            redisFree( r->rc );
            r->rc = NULL;
            return( 1 );
        }
    } while (( res->type == REDIS_REPLY_ERROR ) &&
            urcl_redirect( r, res->str ));

    if (( res->type == REDIS_REPLY_INTEGER ) && ( res->integer == 0 )) {
        ret = 0;
    }

    freeReplyObject( res );
    return( ret );
}

    int
urcl_expire( URCL *r, const char *key, long long expiration )
{
    int         ret = 1;
    char        buf[ 256 ];
    redisReply  *res = NULL;

    snprintf( buf, 256, "%lld", expiration );

    do {
        freeReplyObject( res );

        if ( urcl_checkconnection( r )) {
            return( 1 );
        }

        if (( res = redisCommand( r->rc, "EXPIRE %s %s", key, buf )) == NULL ) {
            redisFree( r->rc );
            r->rc = NULL;
            return( 1 );
        }
    } while (( res->type == REDIS_REPLY_ERROR ) &&
            urcl_redirect( r, res->str ));

    if (( res->type == REDIS_REPLY_INTEGER ) && ( res->integer == 0 )) {
        ret = 0;
    }

    freeReplyObject( res );
    return( ret );
}

    long long
urcl_incrby( URCL *r, const char *key, long long incr )
{
    long long   ret = -1;
    char        buf[ 256 ];
    redisReply  *res = NULL;

    snprintf( buf, 256, "%lld", incr );

    do {
        freeReplyObject( res );

        if ( urcl_checkconnection( r )) {
            return( 1 );
        }

        if (( res = redisCommand( r->rc, "INCRBY %s %s", key, buf )) == NULL ) {
            redisFree( r->rc );
            r->rc = NULL;
            return( 1 );
        }
    } while (( res->type == REDIS_REPLY_ERROR ) &&
            urcl_redirect( r, res->str ));

    if ( res->type == REDIS_REPLY_INTEGER ) {
        ret = res->integer;
    }

    freeReplyObject( res );
    return( ret );
}

    char *
urcl_get( URCL *r, const char *key )
{
    char        *ret = NULL;
    redisReply  *res = NULL;

    do {
        freeReplyObject( res );

        if ( urcl_checkconnection( r )) {
            return( NULL );
        }

        if (( res = redisCommand( r->rc, "GET %s", key )) == NULL ) {
            redisFree( r->rc );
            r->rc = NULL;
            return( NULL );
        }

    } while (( res->type == REDIS_REPLY_ERROR ) &&
            urcl_redirect( r, res->str ));

    if ( res->type == REDIS_REPLY_STRING ) {
        ret = strdup( res->str );
    }

    freeReplyObject( res );
    return( ret );
}

    char *
urcl_hget( URCL *r, const char *key, const char *field )
{
    char        *ret = NULL;
    redisReply  *res = NULL;

    do {
        freeReplyObject( res );

        if ( urcl_checkconnection( r )) {
            return( NULL );
        }

        if (( res = redisCommand( r->rc, "HGET %s %s", key, field )) == NULL ) {
            redisFree( r->rc );
            r->rc = NULL;
            return( NULL );
        }
    } while (( res->type == REDIS_REPLY_ERROR ) &&
            urcl_redirect( r, res->str ));

    if ( res->type == REDIS_REPLY_STRING ) {
        ret = strdup( res->str );
    }

    freeReplyObject( res );
    return( ret );
}

    int
urcl_del( URCL *r, const char *key )
{
    int         ret = 1;
    redisReply  *res = NULL;

    do {
        freeReplyObject( res );

        if ( urcl_checkconnection( r )) {
            return( 1 );
        }

        if (( res = redisCommand( r->rc, "DEL %s", key )) == NULL ) {
            redisFree( r->rc );
            r->rc = NULL;
            return( 1 );
        }
    } while (( res->type == REDIS_REPLY_ERROR ) &&
            urcl_redirect( r, res->str ));

    if ( res->type == REDIS_REPLY_INTEGER ) {
        ret = 0;
    }

    freeReplyObject( res );
    return( ret );
}
