// Web Server (target Particle devices e.g. RedBear Duo)
// Rob Dobson 2012-2017

#include "application.h"
#include "RdWebServer.h"
#include "RdWebServerUtils.h"

RdWebClient::RdWebClient()
{
    _webClientState        = WEB_CLIENT_NONE;
    _webClientStateEntryMs = 0;
    _pResourceToSend       = NULL;
    _resourceSendIdx       = 0;
    _resourceSendBlkCount  = 0;
    _resourceSendMillis    = 0;
    _pHttpReqPayload       = NULL;
    _httpReqPayloadLen     = 0;
    _curHttpPayloadRxPos   = 0;
}


RdWebClient::~RdWebClient()
{
    cleanUp();
}


void RdWebClient::cleanUp()
{
    delete _pHttpReqPayload;
}


void RdWebClient::setState(WebClientState newState)
{
    _webClientState        = newState;
    _webClientStateEntryMs = millis();
    if ((newState != WEB_CLIENT_SEND_RESOURCE) && (newState != WEB_CLIENT_SEND_RESOURCE_WAIT))
    {
        Log.trace("%09ld WebClient %d State: %s entryMs %ld", micros(), _clientIdx, connStateStr(), _webClientStateEntryMs);
    }
}


const char *RdWebClient::connStateStr()
{
    switch (_webClientState)
    {
    case WEB_CLIENT_NONE:
        return "None";

    case WEB_CLIENT_ACCEPTED:
        return "Accepted";

    case WEB_CLIENT_SEND_RESOURCE_WAIT:
        return "Wait";

    case WEB_CLIENT_SEND_RESOURCE:
        return "Send";
    }
    return "Unknown";
}


//////////////////////////////
// Handle read from TCP client
void RdWebClient::handleTCPReadData(int numToRead)
{
    // Read from TCP client
    // Note: reads to a position a few chars ahead of the start of the buffer - this is
    // to permit chars from the end of a previous buffer to be added for end of header testing
    // allowing that the header might be split across two frames
    const char *headerEndSequence = "\r\n\r\n";
    const int  headerEndSeqLen    = strlen(headerEndSequence);
    uint8_t    *pTCPReadBuf       = new uint8_t[numToRead + headerEndSeqLen + 1];

    // Fill initial chars with spaces
    memset(pTCPReadBuf, ' ', headerEndSeqLen);
    // Pointer to where data actually read to
    uint8_t *pTCPReadPos = pTCPReadBuf + headerEndSeqLen;
    int     numRead      = _TCPClient.read(pTCPReadPos, numToRead);
    if (numRead <= 0)
    {
        delete [] pTCPReadBuf;
        return;
    }
    // Terminate buffer
    pTCPReadPos[numRead] = '\0';

    // Check if header already complete
    if (!_httpHeaderComplete)
    {
        // See if the end of header might be split across frames
        if ((pTCPReadPos[0] == '\r') || (pTCPReadPos[0] == '\n'))
        {
            // Copy at most headerEndSeqLen chars to the bit before the data we read
            int        reqLen   = _httpReqStr.length();
            const char *pReqStr = _httpReqStr.c_str();
            for (int i = 0; (i < headerEndSeqLen) && (i < reqLen); i++)
            {
                pTCPReadBuf[headerEndSeqLen - i - 1] = pReqStr[reqLen - i - 1];
            }
        }
        // Location of end of header in read buffer
        uint8_t *pEndOfHeaderInReadBuf = NULL;
        // Check if the composite buffer contains the end of header string
        uint8_t *pEOLEOL = (uint8_t *)strstr((const char *)pTCPReadBuf, headerEndSequence);
        if (pEOLEOL != NULL)
        {
            // End of the string to be added to the request header
            pEndOfHeaderInReadBuf = pEOLEOL + headerEndSeqLen;
            // Header now complete
            _httpHeaderComplete = true;
        }
        // Copy the header portion to the request header string
        uint8_t charReplacedForTerminator = 0;
        if (pEndOfHeaderInReadBuf != NULL)
        {
            charReplacedForTerminator = *pEndOfHeaderInReadBuf;
            // Terminate string temporarily
            *pEndOfHeaderInReadBuf = 0;
        }
        if ((_httpReqStr.length() + strlen((const char*)pTCPReadPos) < HTTPD_MAX_REQ_LENGTH))
            _httpReqStr.concat((char *)pTCPReadPos);
        // Put back the char which we replaced with terminator
        if (pEndOfHeaderInReadBuf != NULL)
            *pEndOfHeaderInReadBuf = charReplacedForTerminator;
        // Get the length of the payload
        if (_httpHeaderComplete)
        {
            int payloadLen = getContentLengthFromHeader(_httpReqStr);
            _curHttpPayloadRxPos = 0;
            Log.trace("%09ld WebClient Payload length %d", micros(), payloadLen);
            // We have to ignore payloads that are too big for our memory
            if (payloadLen > HTTP_MAX_PAYLOAD_LENGTH)
            {
                payloadLen = 0;
            }
            if ((payloadLen != 0) && (payloadLen != _httpReqPayloadLen))
            {
                delete [] _pHttpReqPayload;
                // Add space for null terminator
                _pHttpReqPayload    = new unsigned char[payloadLen + 1];
                _pHttpReqPayload[0] = 0;
            }
            _httpReqPayloadLen = payloadLen;
        }
        // Copy payload data to the payload buffer (if any)
        if ((_httpReqPayloadLen > 0) && (pEndOfHeaderInReadBuf != NULL))
        {
            int toCopy = numRead - (pEndOfHeaderInReadBuf - pTCPReadPos);
            if (toCopy > _httpReqPayloadLen)
            {
                toCopy = _httpReqPayloadLen;
            }
            memcpy(_pHttpReqPayload, pEndOfHeaderInReadBuf, toCopy);
            _curHttpPayloadRxPos = toCopy;
            *(_pHttpReqPayload + _curHttpPayloadRxPos) = 0;
        }
    }
    else
    {
        // Copy data to payload
        int spaceLeft = _httpReqPayloadLen - _curHttpPayloadRxPos;
        if (spaceLeft > 0)
        {
            int toCopy = numRead;
            if (toCopy > spaceLeft)
            {
                toCopy = spaceLeft;
            }
            memcpy(_pHttpReqPayload + _curHttpPayloadRxPos, pTCPReadPos, toCopy);
            _curHttpPayloadRxPos += toCopy;
            *(_pHttpReqPayload + _curHttpPayloadRxPos) = 0;
        }
    }

    // Clean up temp buffer
    delete [] pTCPReadBuf;
}

////////////////////////////////////////////
// Clean up resources used for TCP reception
void RdWebClient::cleanupTCPRxResources()
{
    // Deallocate memory and reset payload length etc
    _httpReqPayloadLen  = 0;
    _httpHeaderComplete = false;
    delete [] _pHttpReqPayload;
    _pHttpReqPayload = NULL;
    _httpReqStr      = "";
}


//////////////////////////////////
// Handle the client state machine
void RdWebClient::service(RdWebServer *pWebServer)
{
    // Handle different states
    switch (_webClientState)
    {
    case WEB_CLIENT_NONE:
        // See if a connection is ready to be accepted
        _TCPClient = pWebServer->available();
        if (_TCPClient)
        {
            // Now connected
            cleanupTCPRxResources();
            setState(WEB_CLIENT_ACCEPTED);
            // Info
            IPAddress ip    = _TCPClient.remoteIP();
            String    ipStr = ip;
            Log.trace("%09ld WebClient IP %s", micros(), ipStr.c_str());
        }
        break;

    case WEB_CLIENT_ACCEPTED:
       {
           // Check if client is still connected
           if (!_TCPClient.connected())
           {
               Log.trace("%09ld WebClient disconnected", micros());
               _TCPClient.stop();
               cleanupTCPRxResources();
               setState(WEB_CLIENT_NONE);
               break;
           }
           // Check for having been in this state for too long
           if (RdWebServerUtils::isTimeout(millis(), _webClientStateEntryMs, MAX_MS_IN_CLIENT_STATE_WITHOUT_DATA))
           {
               Log.trace("%09ld WebClient no-data timeout", micros());
               _TCPClient.stop();
               cleanupTCPRxResources();
               setState(WEB_CLIENT_NONE);
           }
           // Anything available?
           int numBytesAvailable = _TCPClient.available();
           int numToRead         = numBytesAvailable;
           if (numToRead > 0)
           {
               // Make sure we don't overflow buffers
               if (numToRead > MAX_CHS_IN_SERVICE_LOOP)
                   numToRead = MAX_CHS_IN_SERVICE_LOOP;
               if (_httpReqStr.length() + numToRead > HTTPD_MAX_REQ_LENGTH)
                   numToRead = HTTPD_MAX_REQ_LENGTH - _httpReqStr.length();
           }

           // Check if we want to read
           if (numToRead <= 0)
               return;

           // Handle read from TCP client
           handleTCPReadData(numToRead);

           // Check for completion
           if (_httpHeaderComplete && (_httpReqPayloadLen == _curHttpPayloadRxPos))
           {
               Log.trace("%09ld WebClient received %d", micros(), _httpReqStr.length());
               bool handledOk = false;
               _pResourceToSend = handleReceivedHttp(handledOk, pWebServer);
               // clean the received resources
               cleanupTCPRxResources();
               // Get ready to send the response (in sections as needed)
               _resourceSendIdx      = 0;
               _resourceSendBlkCount = 0;
               _resourceSendMillis   = millis();
               // Wait until response complete
               setState(WEB_CLIENT_SEND_RESOURCE_WAIT);
               if (!handledOk)
               {
                   Log.trace("%09ld WebClient couldn't handle request", micros());
               }
           }
           else
           {
               // Restart state timer to ensure timeout only happens when there is no data
               _webClientStateEntryMs = millis();
           }
           break;
       }

    case WEB_CLIENT_SEND_RESOURCE_WAIT:
       {
           // Check how long to wait
           unsigned long msToWait = MS_WAIT_AFTER_LAST_TCP_FRAME;
           if ((_pResourceToSend != NULL) && (_resourceSendIdx < _pResourceToSend->_dataLen))
           {
               msToWait = MS_WAIT_BETWEEN_TCP_FRAMES;
           }

           // Check for timeout on resource send
           if (RdWebServerUtils::isTimeout(millis(), _resourceSendMillis, msToWait))
           {
               setState(WEB_CLIENT_SEND_RESOURCE);
           }
           break;
       }

    case WEB_CLIENT_SEND_RESOURCE:
       {
           if (!_pResourceToSend)
           {
               // Close connection and finish
               _TCPClient.stop();
               setState(WEB_CLIENT_NONE);
               Log.trace("%09ld WebClient resp complete", micros());
               break;
           }
           // Send data in chunks based on limited buffer sizes in TCP stack
           if (_resourceSendIdx >= _pResourceToSend->_dataLen)
           {
               // Completed - close client
               _TCPClient.stop();
               setState(WEB_CLIENT_NONE);
               Log.trace("%09ld WebClient Sent %s, %d bytes total, %d blocks", micros(),
                        _pResourceToSend->_pResId, _pResourceToSend->_dataLen, _resourceSendBlkCount);
               break;
           }

           // Get point and length of next chunk
           const unsigned char *pMem       = _pResourceToSend->_pData + _resourceSendIdx;
           int                 toSendBytes = _pResourceToSend->_dataLen - _resourceSendIdx;
           if (toSendBytes > HTTPD_MAX_RESP_CHUNK_SIZE)
           {
               toSendBytes = HTTPD_MAX_RESP_CHUNK_SIZE;
           }

           // Send next chunk
//    Log.trace("WebClient Writing %d bytes (%d) = %02x %02x %02x %02x ... %02x %02x %02x", toSendBytes, _resourceSendIdx,
//             pMem[0], pMem[1], pMem[2], pMem[3], pMem[toSendBytes - 3], pMem[toSendBytes - 2], pMem[toSendBytes - 1]);

           _TCPClient.write(pMem, toSendBytes);
           _TCPClient.flush();
           _resourceSendIdx += toSendBytes;
           _resourceSendBlkCount++;
           _resourceSendMillis = millis();
           setState(WEB_CLIENT_SEND_RESOURCE_WAIT);
           break;
       }
    }
}


//////////////////////////////////////
// Handle an HTTP request
RdWebServerResourceDescr *RdWebClient::handleReceivedHttp(bool& handledOk, RdWebServer *pWebServer)
{
    handledOk = false;
    RdWebServerResourceDescr *pResourceToRespondWith = NULL;

    // Request string
    const char *pHttpReq = _httpReqStr.c_str();

    // Get HTTP method
    int httpMethod = METHOD_OTHER;
    if (strncmp(pHttpReq, "GET ", 4) == 0)
    {
        httpMethod = METHOD_GET;
    }
    else if (strncmp(pHttpReq, "POST", 4) == 0)
    {
        httpMethod = METHOD_POST;
    }
    else if (strncmp(pHttpReq, "OPTIONS", 7) == 0)
    {
        httpMethod = METHOD_OPTIONS;
    }

    // See if there is a valid HTTP command
    String endpointStr, argStr;
    if (extractEndpointArgs(pHttpReq + 3, endpointStr, argStr))
    {
        // Received cmd and arguments
        Log.trace("%09ld WebClient handleHTTP EndPtStr %s ArgStr %s", micros(),
                        endpointStr.c_str(), argStr.c_str());

        // Handle REST API commands
        RestAPIEndpointDef *pEndpoint = pWebServer->getEndpoint(endpointStr);
        if (pEndpoint)
        {
            Log.trace("%09ld WebClient FoundEndpoint <%s> Type %d", micros(),
                        endpointStr.c_str(), pEndpoint->_endpointType);
            if (pEndpoint->_endpointType == RestAPIEndpointDef::ENDPOINT_CALLBACK)
            {
                String             retStr;
                RestAPIEndpointMsg apiMsg(httpMethod, endpointStr.c_str(), argStr.c_str(), pHttpReq);
                apiMsg._pMsgContent   = _pHttpReqPayload;
                apiMsg._msgContentLen = _httpReqPayloadLen;
                (pEndpoint->_callback)(apiMsg, retStr);
                Log.trace("%09ld WebClient api response len %d", micros(), retStr.length());
                if (strlen(pEndpoint->_pContentType) == 0)
                {
                    formHTTPResponse(_httpRespStr, "200 OK", "application/json", retStr.c_str(), -1);
                }
                else
                {
                    formHTTPResponse(_httpRespStr, "200 OK", pEndpoint->_pContentType, retStr.c_str(), -1);
                }
                Log.trace("%09ld WebClient http response len %d", micros(), _httpRespStr.length());
                // These delays arrived at by experimentation - 15ms seems ok, 10ms is not
                delay(20);
                _TCPClient.write((uint8_t *)_httpRespStr.c_str(), _httpRespStr.length());
                Log.trace("%09ld WebClient write to tcp clientIdx %d len %d", micros(), _clientIdx, _httpRespStr.length());
                // See comment above
                delay(20);
                _TCPClient.flush();
                handledOk = true;
            }
        }

        // Look for the command in the static resources
        if (!handledOk)
        {
            for (int wsResIdx = 0; wsResIdx < pWebServer->getNumResources(); wsResIdx++)
            {
                RdWebServerResourceDescr *pRes = pWebServer->getResource(wsResIdx);
                if ((strcasecmp(pRes->_pResId, endpointStr) == 0) ||
                    ((strlen(endpointStr) == 0) && (strcasecmp(pRes->_pResId, "index.html") == 0)))
                {
                    if (pRes->_pData != NULL)
                    {
                        Log.trace("%09ld WebClient sending resource %s, %d bytes, %s",
                                  micros(), pRes->_pResId, pRes->_dataLen, pRes->_pMimeType);
                        // Form header
                        formHTTPResponse(_httpRespStr, "200 OK", pRes->_pMimeType, "", pRes->_dataLen);
                        _TCPClient.write((uint8_t *)_httpRespStr.c_str(), _httpRespStr.length());

                        /*const char *pBuff   = _httpRespStr.c_str();
                         * const int  bufffLen = strlen(pBuff);
                         * Log.trace("Header %d = %02x %02x %02x ... %02x %02x %02x", _httpRespStr.length(),
                         *        pBuff[0], pBuff[1], pBuff[2], pBuff[bufffLen - 3], pBuff[bufffLen - 2], pBuff[bufffLen - 1]);*/
                        _TCPClient.flush();
                        // Respond with static resource
                        pResourceToRespondWith = pRes;
                        handledOk = true;
                    }
                    break;
                }
            }
        }

        // If not handled ok
        if (!handledOk)
        {
            Log.trace("%09ld WebClient Endpoint %s not found or invalid", micros(), endpointStr.c_str());
        }
    }
    else
    {
        Log.trace("%09ld WebClient Cannot find command or args", micros());
    }

    // Handle situations where the command wasn't handled ok
    if (!handledOk)
    {
        Log.trace("%09ld WebClient Returning 404 Not found", micros());
        formHTTPResponse(_httpRespStr, "404 Not Found", "text/plain", "404 Not Found", -1);
        _TCPClient.write((uint8_t *)_httpRespStr.c_str(), _httpRespStr.length());
    }
    return pResourceToRespondWith;
}


//////////////////////////////////////
// Extract arguments from rest api string
bool RdWebClient::extractEndpointArgs(const char *buf, String& endpointStr, String& argStr)
{
    if (buf == NULL)
    {
        return false;
    }

    // Check for first slash
    char *pSlash1 = strchr(buf, '/');
    if (pSlash1 == NULL)
    {
        return false;
    }
    pSlash1++;
    // Extract command
    while (*pSlash1)
    {
        if ((*pSlash1 == '/') || (*pSlash1 == ' ') || (*pSlash1 == '\n') ||
            (*pSlash1 == '?') || (*pSlash1 == '&'))
        {
            break;
        }
        endpointStr.concat(*pSlash1++);
    }
    if ((*pSlash1 == '\0') || (*pSlash1 == ' ') || (*pSlash1 == '\n'))
    {
        return true;
    }
    // Now args
    pSlash1++;
    while (*pSlash1)
    {
        if ((*pSlash1 == ' ') || (*pSlash1 == '\n'))
        {
            break;
        }
        argStr.concat(*pSlash1++);
    }
    // Convert escaped characters
    RestAPIEndpoints::unencodeHTTPChars(endpointStr);
    RestAPIEndpoints::unencodeHTTPChars(argStr);
    return true;
}


int RdWebClient::getContentLengthFromHeader(const char *msgBuf)
{
    char *ptr = strstr(msgBuf, "Content-Length:");

    if (ptr)
    {
        ptr += 15;
        int contentLen = atoi(ptr);
        if (contentLen >= 0)
        {
            return contentLen;
        }
    }
    return 0;
}


// Form a header to respond
void RdWebClient::formHTTPResponse(String& respStr, const char *rsltCode,
                                   const char *contentType, const char *respBody, int contentLen)
{
    if (contentLen == -1)
    {
        contentLen = strlen(respBody);
    }
    respStr = String::format("HTTP/1.1 %s\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: %s\r\nConnection: close\r\nContent-Length: %d\r\n\r\n%s", rsltCode, contentType, contentLen, respBody);
}


RdWebServer::RdWebServer()
{
    _pTCPServer            = NULL;
    _pRestAPIEndpoints     = NULL;
    _pWebServerResources   = NULL;
    _numWebServerResources = 0;
    _TCPPort                     = 80;
    _webServerState              = WEB_SERVER_STOPPED;
    _webServerStateEntryMs       = 0;
    _numWebServerResources       = 0;
    _webServerActiveLastUnixTime = 0;
    // Configure each client
    for (int clientIdx = 0; clientIdx < MAX_WEB_CLIENTS; clientIdx++)
    {
        _webClients[clientIdx].setClientIdx(clientIdx);
    }
}


// Destructor
RdWebServer::~RdWebServer()
{
}


const char *RdWebServer::connStateStr()
{
    switch (_webServerState)
    {
    case WEB_SERVER_STOPPED:
        return "Stopped";

    case WEB_SERVER_WAIT_CONN:
        return "WaitConn";

    case WEB_SERVER_BEGUN:
        return "Begun";
    }
    return "Unknown";
}


char RdWebServer::connStateChar()
{
    switch (_webServerState)
    {
    case WEB_SERVER_STOPPED:
        return '0';

    case WEB_SERVER_WAIT_CONN:
        return 'W';

    case WEB_SERVER_BEGUN:
        return 'B';
    }
    return 'K';
}


void RdWebServer::setState(WebServerState newState)
{
    _webServerState        = newState;
    _webServerStateEntryMs = millis();
    Log.trace("%09ld WebServerState: %s entryMs %ld", micros(), connStateStr(), _webServerStateEntryMs);
}


void RdWebServer::start(int port)
{
    Log.info("WebServer: Start");
    _TCPPort = port;
    setState(WEB_SERVER_WAIT_CONN);
}


void RdWebServer::restart(int port)
{
    Log.info("WebServer: Restart");
    // Check if already started
    if (_pTCPServer)
    {
        stop();
    }
    // Create server and begin
    _TCPPort    = port;
    _pTCPServer = new TCPServer(_TCPPort);
    setState(WEB_SERVER_WAIT_CONN);
}


void RdWebServer::stop()
{
    Log.info("WebServer: Stop");
    if (_pTCPServer)
    {
        _pTCPServer->stop();
        // Delete previous server
        delete _pTCPServer;
        _pTCPServer = NULL;
    }
    setState(WEB_SERVER_STOPPED);
}


TCPClient RdWebServer::available()
{
    if (_pTCPServer)
    {
        return _pTCPServer->available();
    }
    return -1;
}


//////////////////////////////////////
// Handle the connection state machine
void RdWebServer::service()
{
    // Handle different states
    switch (_webServerState)
    {
    case WEB_SERVER_STOPPED:
        break;

    case WEB_SERVER_WAIT_CONN:
        if (WiFi.ready())
        {
            restart(_TCPPort);
            if (_pTCPServer)
            {
                Log.info("WebServer TCPServer Begin");
                _pTCPServer->begin();
                setState(WEB_SERVER_BEGUN);
            }
        }
        break;

    case WEB_SERVER_BEGUN:
        // Service the clients
        for (int clientIdx = 0; clientIdx < MAX_WEB_CLIENTS; clientIdx++)
        {
            _webClients[clientIdx].service(this);

            if (_webClients[clientIdx].clientIsActive())
            {
                _webServerActiveLastUnixTime = Time.now();
            }
        }
        break;
    }
}


//////////////////////////////////////
// Add resources to the web server
void RdWebServer::addStaticResources(RdWebServerResourceDescr *pResources, int numResources)
{
    _pWebServerResources   = pResources;
    _numWebServerResources = numResources;
}


// Add endpoints to the web server
void RdWebServer::addRestAPIEndpoints(RestAPIEndpoints *pRestAPIEndpoints)
{
    _pRestAPIEndpoints = pRestAPIEndpoints;
}
