#ifndef URCL_H
#define URCL_H

typedef struct urcl URCL;

URCL        *urcl_connect( const char *host, int port );
int         urcl_set( URCL *, const char *key, const char *value );
int         urcl_hset( URCL *, const char *key, const char *field, const char *value );
int         urcl_expire( URCL *, const char *key, long long );
long long   urcl_incrby( URCL *, const char *key, long long );
char        *urcl_get( URCL *, const char *key );
char        *urcl_hget( URCL *, const char *key, const char *field );
int         urcl_del( URCL *, const char *key );
int         urcl_readonly( URCL * );
int         urcl_readwrite( URCL * );

#endif /* URCL_H */
