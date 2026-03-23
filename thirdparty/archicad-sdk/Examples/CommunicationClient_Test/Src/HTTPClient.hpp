#if !defined HTTPCLIENT_HPP
#define HTTPCLIENT_HPP

#pragma once

// from GSRoot
#include "ObjectState.hpp"

// from CommunicationClient
#include "Communication/Client/ClientConnectionFactory.hpp"
#include "Communication/Client/Http/HttpClientRequest.hpp"
#include "Communication/Client/Http/HttpClientResponse.hpp"


class HTTPClient
{
private:
    const GS::UniString&                               serverBaseUrl;
    Communication::Client::Http::HttpClientResponseRef response;
    Communication::Client::Http::HttpClientRequestRef  request;

    Communication::Client::ClientConnectionRef         clientConnection;
    GS::ObjectState                                    responseOs;

    GS::ObjectState                                    DecodeResponse () const;
    GS::ErrorStatus                                    GetExecutionStatus (const Communication::Http::StatusLine&);

public:
    HTTPClient (const GS::UniString&);

    GSErrCode                                          CloseConnection ();
    GSErrCode                                          Connect ();
    GSErrCode                                          Execute (const GS::UniString& requestMethod, const GS::UniString& requestMethodId, const GS::UInt32 requestTimeout = 5000);
    GS::ObjectState                                    GetResponse () const;
};

#endif
