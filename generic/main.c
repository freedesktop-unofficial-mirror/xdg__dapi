#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>

#include "commands.h"
#include <dapi/comm.h>

static void debug( const char* fmt, ... )
#ifdef __GNUC__
    __attribute (( format( printf, 1, 2 )))
#endif
    ;
    
static void debug( const char* fmt, ... )
    {
    va_list va;
    va_start( va, fmt );
    vfprintf( stderr, fmt, va );
    va_end( va );
    fprintf( stderr, "\n" );
    }

DapiConnection** connections = NULL;
int num_connections = 0;

Display* dpy = NULL;

static void closeConnection( DapiConnection* conn )
    {
    int i;
    debug( "Closing %d", dapi_socket( conn ));
    for( i = 0;
         i < num_connections;
         ++i )
        {
        if( connections[ i ] == conn )
            {
            connections[ i ] = NULL;
            break;
            }
        }
    dapi_close( conn );
    }

static void processCommandInit( DapiConnection* conn, int seq )
    {
    if( !dapi_readCommandInit( conn ))
        {
        closeConnection( conn );
        return;
        }
    debug( "Init %d", dapi_socket( conn ));
    dapi_writeReplyInit( conn, seq, 1 );
    }

/* TODO */
static int caps[] =
    {    
    DAPI_COMMAND_INIT,
    DAPI_COMMAND_CAPABILITIES,
    DAPI_COMMAND_OPENURL,
/*    DAPI_COMMAND_EXECUTEURL,*/
    DAPI_COMMAND_BUTTONORDER,
/*    DAPI_COMMAND_RUNASUSER,*/
    DAPI_COMMAND_SUSPENDSCREENSAVING,
/*    DAPI_COMMAND_MAILTO,*/
    DAPI_COMMAND_LOCALFILE,
    DAPI_COMMAND_UPLOADFILE,
    DAPI_COMMAND_REMOVETEMPORARYLOCALFILE
    };

static void processCommandCapabilities( DapiConnection* conn, int seq )
    {
    intarr capabilities;
    if( !dapi_readCommandInit( conn ))
        {
        closeConnection( conn );
        return;
        }
    debug( "Capabilities %d", dapi_socket( conn ));
    capabilities.count = sizeof( caps ) / sizeof( caps[ 0 ] );
    capabilities.data = caps;
    dapi_writeReplyCapabilities( conn, seq, capabilities, 1 );
    }
    
static void processCommandOpenUrl( DapiConnection* conn, int seq )
    {
    int ok;
    char* url;
    DapiWindowInfo winfo;
    if( !dapi_readCommandOpenUrl( conn, &url, &winfo ))
        {
        closeConnection( conn );
        return;
        }
    debug( "Open url %d: %s", dapi_socket( conn ), url );
    /* winfo ? */
    ok = openUrl( url );
    dapi_writeReplyOpenUrl( conn, seq, ok ? 1 : 0 );
    free( url );
    dapi_freeWindowInfo( winfo );
    }

static void processCommandButtonOrder( DapiConnection* conn, int seq )
    {
    if( !dapi_readCommandButtonOrder( conn ))
        {
        closeConnection( conn );
        return;
        }
    debug( "Button order %d", dapi_socket( conn ));
    dapi_writeReplyButtonOrder( conn, seq, 1 );
    }

static void processCommandExecuteUrl( DapiConnection* conn, int seq )
    {
    char* url;
    DapiWindowInfo winfo;
    if( !dapi_readCommandExecuteUrl( conn, &url, &winfo ))
        {
        closeConnection( conn );
        return;
        }
    debug( "Execute url %d: %s", dapi_socket( conn ), url );
    dapi_writeReplyExecuteUrl( conn, seq, 0 ); /* TODO failure ??? */
    free( url );
    dapi_freeWindowInfo( winfo );
    }

static void processCommandRunAsUser( DapiConnection* conn, int seq )
    {
    char* user;
    char* command;
    DapiWindowInfo winfo;
    if( !dapi_readCommandRunAsUser( conn, &user, &command, &winfo ))
        {
        closeConnection( conn );
        return;
        }
    debug( "Run as user %d: %s %s", dapi_socket( conn ), user, command );
    dapi_writeReplyRunAsUser( conn, seq, 0 ); /* TODO failure ??? */
    free( user );
    free( command );
    dapi_freeWindowInfo( winfo );
    }

static void processCommandSuspendScreensaving( DapiConnection* conn, int seq )
    {
    int suspend;
    int ok;
    static int suspend_count = 0;
    if( !dapi_readCommandSuspendScreensaving( conn, &suspend ))
        {
        closeConnection( conn );
        return;
        }
    debug( "Suspend screensaving %d: %d", dapi_socket( conn ), suspend );
    /* TODO remember setting per-connection and
       - reset on disconnect
       - ignore repeated set
    */
    suspend_count += suspend ? 1 : -1;
    if( suspend_count < 0 )
        suspend_count = 0;
    ok = suspendScreensaving( dpy, suspend_count > 0 );
    dapi_writeReplySuspendScreensaving( conn, seq, ok ? 1 : 0 );
    }

static void processCommandMailTo( DapiConnection* conn, int seq )
    {
    int ok;
    char* subject;
    char* body;
    char* to;
    char* cc;
    char* bcc;
    stringarr attachments;
    DapiWindowInfo winfo;
    if( !dapi_readCommandMailTo( conn, &subject, &body, &to, &cc, &bcc, &attachments, &winfo ))
        {
        closeConnection( conn );
        return;
        }
    debug( "Mail to %d: %s", dapi_socket( conn ), subject );
    /* winfo ? */
    ok = mailTo( subject, body, to, cc, bcc, ( const char** ) attachments.data, attachments.count );
    dapi_writeReplyMailTo( conn, seq, ok ? 1 : 0 );
    free( subject );
    free( body );
    free( to );
    free( cc );
    free( bcc );
    dapi_freestringarr( attachments );
    dapi_freeWindowInfo( winfo );
    }

static void processCommandLocalFile( DapiConnection* conn, int seq )
    {
    char* file;
    char* local;
    int allow_download;
    const char* result = NULL;
    DapiWindowInfo winfo;
    if( !dapi_readCommandLocalFile( conn, &file, &local, &allow_download, &winfo ))
        {
        closeConnection( conn );
        return;
        }
    debug( "Local file %d: %s %s %d", dapi_socket( conn ), file, local, allow_download );
    if( file[ 0 ] == '/' )
        result = file;  /* is it already local */
    else if( strncmp( file, "file:///", strlen( "file:///" )) == 0 )
        result = file + strlen( "file://" ); /* local url */
    else
        result = NULL;
    /* local is unused, no real downloading */
    dapi_writeReplyLocalFile( conn, seq, result );
    free( file );
    dapi_freeWindowInfo( winfo );
    }

static void processCommandUploadFile( DapiConnection* conn, int seq )
    {
    char* local;
    char* file;
    int remove_local;
    int ok;
    DapiWindowInfo winfo;
    if( !dapi_readCommandUploadFile( conn, &local, &file, &remove_local, &winfo ))
        {
        closeConnection( conn );
        return;
        }
    debug( "Upload file %d: %s %s %d", dapi_socket( conn ), local, file, remove_local );
    if( file[ 0 ] == '/' && strcmp( local, file ) == 0 )
        ok = 1;
    else if( strncmp( file, "file:///", strlen( "file:///" )) == 0
        && strcmp( file + strlen( "file://" ), local ) == 0 )
        ok = 1;
    else
        ok = 0;
    /* remove_local is unused, no real uploading */
    dapi_writeReplyUploadFile( conn, seq, ok );
    free( local );
    free( file );
    dapi_freeWindowInfo( winfo );
    }

static void processCommandRemoveTemporaryLocalFile( DapiConnection* conn, int seq )
    {
    char* file;
    if( !dapi_readCommandRemoveTemporaryLocalFile( conn, &file ))
        {
        closeConnection( conn );
        return;
        }
    debug( "Remove temporary %d: %s", dapi_socket( conn ), file );
    dapi_writeReplyRemoveTemporaryLocalFile( conn, seq, 1 );
    free( file );
    }


static void processCommand( DapiConnection* conn )
    {
    int command;
    int seq;
    if( !dapi_readCommand( conn, &command, &seq ))
        {
        closeConnection( conn );
        return;
        }
    debug( "Command %d: %d (%d)", dapi_socket( conn ), command, seq );
    switch( command )
        {
        case DAPI_COMMAND_INIT:
            processCommandInit( conn, seq );
            return;
        case DAPI_COMMAND_CAPABILITIES:
            processCommandCapabilities( conn, seq );
            return;
        case DAPI_COMMAND_OPENURL:
            processCommandOpenUrl( conn, seq );
            return;
        case DAPI_COMMAND_EXECUTEURL:
            processCommandExecuteUrl( conn, seq );
            return;
        case DAPI_COMMAND_BUTTONORDER:
            processCommandButtonOrder( conn, seq );
            return;
        case DAPI_COMMAND_RUNASUSER:
            processCommandRunAsUser( conn, seq );
            return;
        case DAPI_COMMAND_SUSPENDSCREENSAVING:
            processCommandSuspendScreensaving( conn, seq );
            return;
        case DAPI_COMMAND_MAILTO:
            processCommandMailTo( conn, seq );
            return;
        case DAPI_COMMAND_LOCALFILE:
            processCommandLocalFile( conn, seq );
            return;
        case DAPI_COMMAND_UPLOADFILE:
            processCommandUploadFile( conn, seq );
            return;
        case DAPI_COMMAND_REMOVETEMPORARYLOCALFILE:
            processCommandRemoveTemporaryLocalFile( conn, seq );
            return;
        default:
            debug( "Unknown command %d: %d", dapi_socket( conn ), command );
            return;
        }
    }

int main( int argc, char* argv[] )
    {
    int i;
    int mainsock;
    dpy = XOpenDisplay( NULL );
    if( dpy == NULL )
        {
        fprintf( stderr, "Cannot open X connection!\n" );
        return 1;
        }
    mainsock = dapi_bindSocket();
    if( mainsock < 0 )
        return 2;
    for(;;)
        {
        fd_set in;
        FD_ZERO( &in );
        FD_SET( mainsock, &in );
        int maxsock = mainsock;
        for( i = 0;
             i < num_connections;
             ++i )
            if( connections[ i ] != NULL )
                {
                int sock = dapi_socket( connections[ i ] );
                FD_SET( sock, &in );
                if( sock > maxsock )
                    maxsock = sock;
                }
        FD_SET( XConnectionNumber( dpy ), &in );
        if( XConnectionNumber( dpy ) > maxsock )
            maxsock = XConnectionNumber( dpy );
        select( maxsock + 1, &in, NULL, NULL, NULL );
        if( FD_ISSET( XConnectionNumber( dpy ), &in ))
            {
            while( XPending( dpy ))
                {
                XEvent ev;
                XNextEvent( dpy, &ev );
                }
            }
        for( i = 0;
             i < num_connections;
             ++i )
            {
            if( connections[ i ] != NULL && FD_ISSET( dapi_socket( connections[ i ] ), &in ))
                processCommand( connections[ i ] );
            }
        if( FD_ISSET( mainsock, &in ))
            {
            DapiConnection* conn = dapi_acceptSocket( mainsock );
            if( conn != NULL )
                {
                int pos;
                debug( "New connection: %d", dapi_socket( conn ));
                for( pos = 0;
                     pos < num_connections;
                     ++pos )
                    {
                    if( connections[ pos ] == NULL )
                        break;
                    }
                if( pos == num_connections )
                    {
                    ++num_connections;
                    connections = realloc( connections, sizeof( DapiConnection* ) * num_connections );
                    }
                connections[ pos ] = conn;
                }
            }
        }
    }
