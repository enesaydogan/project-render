#include "HTTPClient.hpp"

// from GSRoot
#include "System.hpp"

// from JSONConversion
#include "ObjectStateJSONConversion.hpp"

// from DG
#include "DG.h"


GSErrCode HTTPClient::Connect ()
{
	if (clientConnection != nullptr)
		return Error;

	try {
		clientConnection = Communication::Client::ClientConnectionFactory::CreateConnection (serverBaseUrl);
	} catch (Communication::Client::ClientException&) { return ExceptionError; }

	return NoError;
}


GSErrCode HTTPClient::CloseConnection ()
{
	if (clientConnection != nullptr) {
		try {
			if (clientConnection->IsClosed () == false) {
				clientConnection->Close ();
			}
		} catch (Communication::Client::ClientException&) { return ExceptionError; }

		try {
			clientConnection = nullptr;
		} catch (Communication::Client::ClientException&) { return ExceptionError; }
	}

	return NoError;
}


GS::ErrorStatus HTTPClient::GetExecutionStatus (const Communication::Http::StatusLine& statusLine)
{
	if (statusLine.GetStatusCode () >= 200 && statusLine.GetStatusCode () < 300)
		return NoError;

	return GS::ErrorStatus (Error, statusLine.ToString ());
}


GS::ObjectState HTTPClient::DecodeResponse () const
{
	GS::ObjectState decodedResponse;
	const bool hasContentTypeHeader = response->GetHeaders ("Content-Type") != nullptr;

	if (hasContentTypeHeader && response->GetHeaders ("Content-Type")->GetValue ().Contains ("json")) {
		if (JSON::ConvertToObjectState (*response->GetContentChannel (), decodedResponse) != NoError) {
			DBBREAK ();
			static GS::ObjectState invalidJSONFormat;
			invalidJSONFormat.Add ("errorMessage", "Unable to decode JSON content");
			return invalidJSONFormat;
		}
	} else {
		static GS::ObjectState nonJSONFormat;
		nonJSONFormat.Clear ();
		nonJSONFormat.Add ("message", "Succesfully downloaded non-JSON content");
		nonJSONFormat.Add ("contentType", (hasContentTypeHeader ? response->GetHeaders ("Content-Type")->GetValue () : "unknown"));
		return nonJSONFormat;
	}

	return decodedResponse;
}


HTTPClient::HTTPClient (const GS::UniString& serverUrl) :
	serverBaseUrl (serverUrl)
{
}


GSErrCode HTTPClient::Execute (const GS::UniString& requestMethod, const GS::UniString& requestMethodId, const GS::UInt32 requestTimeout)
{
	if (!requestMethod.BeginsWith ("/"))
		return ErrParam;

	request = GS::StaticCast<Communication::Client::Http::HttpClientRequest> (clientConnection->CreateRequest ());
	response = GS::StaticCast<Communication::Client::Http::HttpClientResponse> (clientConnection->CreateResponse ());

	DBASSERT (request != nullptr);
	DBASSERT (response != nullptr);

	request->SetRequestLine (Communication::Http::RequestLine (request->GetRequestLine ().GetProtocolVersion (), "GET", request->GetRequestLine ().GetUri () + requestMethod));
	request->SetId (requestMethodId);

	try {
		clientConnection->SetTimeout (requestTimeout);
		clientConnection->Send (request);
		clientConnection->Receive (response);
	} catch (Communication::Client::ClientException&) {
		return ExceptionError;
	}

	GS::ErrorStatus executionStatus = GetExecutionStatus (response->GetStatusLine ());
	responseOs = DecodeResponse ();

	if (executionStatus.IsError ()) {
		return ExceptionError;
	}

	return NoError;
}


GS::ObjectState HTTPClient::GetResponse () const
{
	return responseOs;
}
