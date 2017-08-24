/* This file is part of urcl and distributed under the terms of the
 * MIT license. See COPYING.
 */

#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <hiredis.h>

#include "urcl.h"

struct urcl_host {
    char                *h_ip;
    int                 h_port;
    redisContext        *h_rc;
    struct urcl_host    *h_next;
    struct urcl_host    *h_prev;
};

struct urcl {
    char                    *hostname;
    int                     hostcount;
    struct urcl_host        *host;
    struct urcl_host        *host_map[ 16384 ];
};

static int urcl_host_insert( urclHandle *, const char *, int );
static int urcl_checkconnection( urclHandle * );
static uint16_t urcl_hashslot( const char * );
static int urcl_reconnect( urclHandle * );
static int urcl_redirect( urclHandle *, char * );

static const uint16_t crc16_table[ 256 ] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7, 0x8108,
    0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef, 0x1231, 0x0210,
    0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6, 0x9339, 0x8318, 0xb37b,
    0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de, 0x2462, 0x3443, 0x0420, 0x1401,
    0x64e6, 0x74c7, 0x44a4, 0x5485, 0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee,
    0xf5cf, 0xc5ac, 0xd58d, 0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6,
    0x5695, 0x46b4, 0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d,
    0xc7bc, 0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b, 0x5af5,
    0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12, 0xdbfd, 0xcbdc,
    0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a, 0x6ca6, 0x7c87, 0x4ce4,
    0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41, 0xedae, 0xfd8f, 0xcdec, 0xddcd,
    0xad2a, 0xbd0b, 0x8d68, 0x9d49, 0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13,
    0x2e32, 0x1e51, 0x0e70, 0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a,
    0x9f59, 0x8f78, 0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e,
    0xe16f, 0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e, 0x02b1,
    0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256, 0xb5ea, 0xa5cb,
    0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d, 0x34e2, 0x24c3, 0x14a0,
    0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xa7db, 0xb7fa, 0x8799, 0x97b8,
    0xe75f, 0xf77e, 0xc71d, 0xd73c, 0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657,
    0x7676, 0x4615, 0x5634, 0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9,
    0xb98a, 0xa9ab, 0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882,
    0x28a3, 0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92, 0xfd2e,
    0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 0x7c26, 0x6c07,
    0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1, 0xef1f, 0xff3e, 0xcf5d,
    0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8, 0x6e17, 0x7e36, 0x4e55, 0x5e74,
    0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};


    urclHandle *
urcl_connect( const char *host, int port )
{
    urclHandle          *r;
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
urcl_free( urclHandle *r )
{
    struct urcl_host    *h, *next_h;

    if ( r ) {
        if ( r->host ) {
            /* Break the circular list */
            r->host->h_prev->h_next = NULL;
        }
        for ( h = r->host; h != NULL; h = next_h ) {
            next_h = h->h_next;
            if ( h->h_rc ) {
                redisFree( h->h_rc );
            }
            if ( h->h_ip ) {
                free( h->h_ip );
            }
            free( h );
        }
        if ( r->hostname ) {
            free( r->hostname );
        }
        free( r );
    }
}

    void
urcl_free_result( urclResult *r )
{
    freeReplyObject( r );
}

    static int
urcl_host_insert( urclHandle *r, const char *ip, int port )
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
urcl_redirect( urclHandle *r, char *err )
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
            urcl_command( r, NULL, "ASKING" );
        }
        return( 1 );
    }
    return( 0 );
}

    static int
urcl_reconnect( urclHandle *r )
{
    struct urcl_host    *host;
    struct timeval      tv_timeout = { 10, 0 };

    host = r->host;
    do {
        if ( host->h_rc == NULL ) {
            host->h_rc = redisConnectWithTimeout( host->h_ip, host->h_port,
                    tv_timeout );
        }
        if ( host->h_rc ) {
            if ( host->h_rc->err ) {
                redisFree( host->h_rc );
                host->h_rc = NULL;
            } else {
                redisSetTimeout( host->h_rc, tv_timeout );
                r->host = host;
                return( 0 );
            }
        }
        host = host->h_next;
    } while ( host != r->host );

    return( 1 );
}

    static int
urcl_checkconnection( urclHandle *r )
{
    if ( r->host->h_rc == NULL && ( urcl_reconnect( r ) != 0 )) {
        return( 1 );
    }
    return( 0 );
}

    static uint16_t
urcl_hashslot( const char *key )
{
    const char      *left;
    const char      *right;
    int             i;
    size_t          len = 0;
    uint16_t        slot = 0;

    if (( left = strchr( key, '{' )) && ( right = strchr( left, '}' ))) {
        left++;
        len = right - left;
    }

    if ( len == 0 ) {
        left = key;
        len = strlen( key );
    }

    for ( i = 0; i < len; i++ ) {
        slot = ( slot << 8 ) ^
                crc16_table[ (( slot >> 8 ) ^ left[ i ] ) & 0xff ];
    }

    return( slot & 0x3fff );
}

    void *
urcl_command( urclHandle *r, const char *key, const char *format, ... )
{
    va_list     ap;
    uint16_t    slot = 0;
    redisReply  *res = NULL;

    if ( key ) {
        slot = urcl_hashslot( key );
        if ( r->host_map[ slot ] ) {
            r->host = r->host_map[ slot ];
        }
    }

    do {
        freeReplyObject( res );

        if ( urcl_checkconnection( r ) == 0 ) {
            va_start( ap, format );
            res = redisvCommand( r->host->h_rc, format, ap );
            va_end( ap );
        }
    } while ( res && ( res->type == REDIS_REPLY_ERROR ) &&
            urcl_redirect( r, res->str ));

    if ( res == NULL ) {
        redisFree( r->host->h_rc );
        r->host->h_rc = NULL;
    } else if ( key ) {
        r->host_map[ slot ] = r->host;
    }

    return( res );
}

