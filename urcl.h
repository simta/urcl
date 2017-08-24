#ifndef URCL_H
#define URCL_H

#include <stdarg.h>
#include <hiredis.h>

typedef struct urcl urclHandle;
typedef struct redisReply urclResult;

urclHandle  *urcl_connect( const char *host, int port );
void        urcl_free( urclHandle * );
void        urcl_free_result( urclResult * );
void        *urcl_command( urclHandle *, const char *key, const char *format, ... );

#endif /* URCL_H */
