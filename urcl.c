/* This file is part of urcl and distributed under the terms of the
 * MIT license. See COPYING.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hiredis/hiredis.h>

#include "urcl.h"

struct urcl {
    redisContext    *rc;
    char            *host;
    int             port;
};

static int urcl_reconnect( URCL * );
static int urcl_redirect( URCL *, char * );

    URCL *
urcl_connect( const char *host, int port )
{
    URCL *r;

    if (( r = calloc( 1, sizeof( struct urcl ))) == NULL ) {
        return( NULL );
    }

    if (( r->host = strdup( host )) == NULL ) {
        goto cleanup;
    }

    r->port = port;

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
        free( r->host );
        r->host = strdup( host );
        r->port = atoi( port + 1 );
        *port = ':';
        urcl_reconnect( r );
        /* If it's an ASK redirect we should send ASKING */
        return( 1 );
    }
    return( 0 );
}

    static int
urcl_reconnect( URCL *r )
{
    if ( r->rc != NULL ) {
        redisFree( r->rc );
    }

    r->rc = redisConnect( r->host, r->port );
    if ( r->rc == NULL ) {
        return( 1 );
    } else if ( r->rc->err ) {
        redisFree( r->rc );
        r->rc = NULL;
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
        if ( r->rc == NULL && ( urcl_reconnect( r ) != 0 )) {
            return( 1 );
        }

        if ( res ) {
            freeReplyObject( res );
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
        if ( r->rc == NULL && ( urcl_reconnect( r ) != 0 )) {
            return( 1 );
        }

        if ( res ) {
            freeReplyObject( res );
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
        if ( r->rc == NULL && ( urcl_reconnect( r ) != 0 )) {
            return( 1 );
        }

        if ( res ) {
            freeReplyObject( res );
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
        if ( r->rc == NULL && ( urcl_reconnect( r ) != 0 )) {
            return( 1 );
        }

        if ( res ) {
            freeReplyObject( res );
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
        if ( r->rc == NULL && ( urcl_reconnect( r ) != 0 )) {
            return( NULL );
        }

        if ( res ) {
            freeReplyObject( res );
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
        if ( r->rc == NULL && ( urcl_reconnect( r ) != 0 )) {
            return( NULL );
        }

        if ( res ) {
            freeReplyObject( res );
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
        if ( r->rc == NULL && ( urcl_reconnect( r ) != 0 )) {
            return( 1 );
        }

        if ( res ) {
            freeReplyObject( res );
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
