
/* Copyright (c) 2014, Human Brain Project
 *                     Daniel Nachbaur <daniel.nachbaur@epfl.ch>
 *                     Stefan.Eilemann@epfl.ch
 */

#include "publisher.h"
#include "event.h"
#include "detail/broker.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <lunchbox/log.h>
#include <lunchbox/servus.h>
#include <map>
#include <zmq.h>

// for NI_MAXHOST
#ifdef _WIN32
#  include <Ws2tcpip.h>
#else
#  include <netdb.h>
#endif

namespace zeq
{
namespace detail
{

class Publisher
{
public:
    Publisher( const lunchbox::URI& uri )
        : _context( zmq_ctx_new( ))
        , _publisher( zmq_socket( _context, ZMQ_PUB ))
        , _service( std::string( "_" ) + uri.getScheme() + "._tcp" )
    {
        if( zmq_bind( _publisher, buildZmqURI( uri ).c_str( )) == -1 )
        {
            zmq_close( _publisher );
            zmq_ctx_destroy( _context );
            _publisher = 0;

            LBTHROW( std::runtime_error(
                         std::string( "Cannot bind publisher socket, got " ) +
                                      zmq_strerror( zmq_errno( ))));
        }

        _initService( uri.getHost(), uri.getPort( ));
    }

    ~Publisher()
    {
        zmq_close( _publisher );
        zmq_ctx_destroy( _context );
    }

    bool publish( const zeq::Event& event )
    {
        const uint64_t type = event.getType();
        zmq_msg_t msgHeader;
        zmq_msg_init_size( &msgHeader, sizeof(type));
        memcpy( zmq_msg_data(&msgHeader), &type, sizeof(type));

        zmq_msg_t msg;
        zmq_msg_init_size( &msg, event.getSize( ));
        memcpy( zmq_msg_data(&msg), event.getData(), event.getSize( ));

        if( zmq_msg_send( &msgHeader, _publisher, ZMQ_SNDMORE ) == -1 ||
            zmq_msg_send( &msg, _publisher, 0 ) == -1 )
        {
            zmq_msg_close( &msgHeader );
            zmq_msg_close( &msg );
            LBWARN << "Cannot publish, got " << zmq_strerror( zmq_errno( ))
                   << std::endl;
            return false;
        }
        zmq_msg_close( &msgHeader );
        zmq_msg_close( &msg );
        return true;
    }

private:
    void _initService( std::string host, uint16_t port )
    {
        if( !_publisher )
            return;

        if( host == "*" )
            host.clear();

        if( host.empty() || port == 0 )
            _resolveHostAndPort( host, port );

        _service.withdraw(); // go silent during k/v update
        _service.set( SERVICE_HOST, host );
        _service.set( SERVICE_PORT, boost::lexical_cast< std::string >( port ));
        _service.announce( port, host );
    }

    void _resolveHostAndPort( std::string& host, uint16_t& port )
    {
        char endPoint[1024];
        size_t size = sizeof(endPoint);
        if( zmq_getsockopt( _publisher, ZMQ_LAST_ENDPOINT, &endPoint,
                            &size ) == -1 )
        {
            LBTHROW( std::runtime_error(
                         "Cannot determine port of publisher" ));
        }

        const std::string endPointStr( endPoint );

        if( port == 0 )
        {
            const std::string portStr =
                  endPointStr.substr( endPointStr.find_last_of( ":" ) + 1 );
            port = boost::lexical_cast< uint16_t >( portStr );
        }

        if( host.empty( ))
        {
            host = endPointStr.substr( endPointStr.find_last_of( "/" ) + 1 );
            host = host.substr( 0, host.size() - host.find_last_of( ":" ) + 1 );
            if( host == "0.0.0.0" )
            {
                char hostname[NI_MAXHOST+1] = {0};
                gethostname( hostname, NI_MAXHOST );
                hostname[NI_MAXHOST] = '\0';
                host = hostname;
            }
        }
    }

    void* _context;
    void* _publisher;
    lunchbox::Servus _service;
};
}

Publisher::Publisher( const lunchbox::URI& uri )
    : _impl( new detail::Publisher( uri ))
{
}

Publisher::~Publisher()
{
    delete _impl;
}

bool Publisher::publish( const Event& event )
{
    return _impl->publish( event );
}

}
