/* This file is part of urcl and distributed under the terms of the
 * MIT license. See COPYING.
 */

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <hiredis/hiredis.h>

#include "urcl.h"

struct urcl_host {
    char                *h_ip;
    int                 h_port;
    redisContext        *h_rc;
    struct urcl_host    *h_next;
    struct urcl_host    *h_prev;
};

struct urcl {
    char                *hostname;
    int                 hostcount;
    struct urcl_host    *host;
};

static int urcl_host_insert( URCL *, const char *, int );
static int urcl_checkconnection( URCL * );
static int urcl_reconnect( URCL * );
static int urcl_redirect( URCL *, char * );
static void urcl_asking( URCL *r );

    URCL *
urcl_connect( const char *host, int port )
{
    URCL                *r;
    struct addrinfo     hints;
    struct addrinfo     *air;
    struct addrinfo     *ai;
    char                hbuf[ NI_MAXHOST ];
    int                 rc;
    int                 i;

    if (( r = calloc( 1, sizeof( struct urcl ))) == NULL ) {
        return( NULL );
    }

    if (( r->hostname = strdup( host )) == NULL ) {
        goto cleanup;
    }

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
            urcl_host_insert( r, hbuf, port );
        }
    }

    freeaddrinfo( air );

    if ( r->hostcount > 1 ) {
        /* Select a somewhat random initial host, since getaddrinfo() defaults
         * to sorting the returned IPs and we don't want to concentrate initial
         * connections on a single host. Permuting the list would improve
         * more situations, but is also more work.
         */
        for ( i = ( getpid( ) % r->hostcount ); i > 0; i-- ) {
            r->host = r->host->h_next;
        }
    }

    if ( urcl_reconnect( r ) != 0 ) {
        goto cleanup;
    }

    return( r );

cleanup:
    free( r->host );
    free( r );
    return( NULL );
}

    void
urcl_free( URCL *r )
{
    struct urcl_host    *h, *next_h;

    if ( r ) {
        if ( r->host ) {
            /* Break the circular list */
            r->host->h_prev->h_next = NULL;
        }
        for ( h = r->host; h != NULL; ) {
            next_h = h->h_next;
            if ( h->h_rc ) {
                redisFree( h->h_rc );
            }
            if ( h->h_ip ) {
                free( h->h_ip );
            }
            free( h );
            h = next_h;
        }
        if ( r->hostname ) {
            free( r->hostname );
        }
        free( r );
    }
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
    r->hostcount++;
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
        if ( strncmp( err, "ASK ", 4 ) == 0 ) {
            urcl_asking( r );
        }
        return( 1 );
    }
    return( 0 );
}

    static int
urcl_reconnect( URCL *r )
{
    struct urcl_host    *host;

    host = r->host;
    do {
        if ( host->h_rc == NULL ) {
            host->h_rc = redisConnect( host->h_ip, host->h_port );
        }
        if ( host->h_rc ) {
            if ( host->h_rc->err ) {
                redisFree( host->h_rc );
                host->h_rc = NULL;
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
    if ( r->host->h_rc == NULL && ( urcl_reconnect( r ) != 0 )) {
        return( 1 );
    }
    return( 0 );
}

    static void
urcl_asking( URCL *r )
{
    redisReply  *res = NULL;

    if (( res = redisCommand( r->host->h_rc, "ASKING" )) == NULL ) {
        redisFree( r->host->h_rc );
        r->host->h_rc = NULL;
    } else {
        freeReplyObject( res );
    }
}

     int
urcl_readonly( URCL *r )
{
    int         ret = 1;
    redisReply  *res = NULL;

    if ( urcl_checkconnection( r )) {
        return( 1 );
    }

    if (( res = redisCommand( r->host->h_rc, "READONLY" )) == NULL ) {
        redisFree( r->host->h_rc );
        r->host->h_rc = NULL;
        return( 1 );
    }

    if (( res->type == REDIS_REPLY_STRING ) && ( res->len == 2 ) &&
            ( memcmp( res->str, "OK", 2 ) == 0 )) {
        ret = 0;
    }

    freeReplyObject( res );
    return( ret );
}

    int
urcl_readwrite( URCL *r )
{
    int         ret = 1;
    redisReply  *res = NULL;

    if ( urcl_checkconnection( r )) {
        return( 1 );
    }

    if (( res = redisCommand( r->host->h_rc, "READWRITE" )) == NULL ) {
        redisFree( r->host->h_rc );
        r->host->h_rc = NULL;
        return( 1 );
    }

    if (( res->type == REDIS_REPLY_STRING ) && ( res->len == 2 ) &&
            ( memcmp( res->str, "OK", 2 ) == 0 )) {
        ret = 0;
    }

    freeReplyObject( res );
    return( ret );
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

        if (( res = redisCommand( r->host->h_rc, "SET %s %s", key, value )) == NULL ) {
            redisFree( r->host->h_rc );
            r->host->h_rc = NULL;
            return( 1 );
        }
    } while (( res->type == REDIS_REPLY_ERROR ) &&
            urcl_redirect( r, res->str ));

    if (( res->type == REDIS_REPLY_STATUS ) && ( res->len == 2 ) &&
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

        if (( res = redisCommand( r->host->h_rc, "HSET %s %s %s",
                key, field, value )) == NULL ) {
            redisFree( r->host->h_rc );
            r->host->h_rc = NULL;
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

        if (( res = redisCommand( r->host->h_rc, "EXPIRE %s %s", key, buf )) == NULL ) {
            redisFree( r->host->h_rc );
            r->host->h_rc = NULL;
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

        if (( res = redisCommand( r->host->h_rc, "INCRBY %s %s", key, buf )) == NULL ) {
            redisFree( r->host->h_rc );
            r->host->h_rc = NULL;
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

        if (( res = redisCommand( r->host->h_rc, "GET %s", key )) == NULL ) {
            redisFree( r->host->h_rc );
            r->host->h_rc = NULL;
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

        if (( res = redisCommand( r->host->h_rc, "HGET %s %s", key, field )) == NULL ) {
            redisFree( r->host->h_rc );
            r->host->h_rc = NULL;
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

        if (( res = redisCommand( r->host->h_rc, "DEL %s", key )) == NULL ) {
            redisFree( r->host->h_rc );
            r->host->h_rc = NULL;
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
