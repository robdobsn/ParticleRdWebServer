// Web server resources
// Rob Dobson 2012-2017

#pragma once

class RdWebServerResourceDescr
{
public:
    RdWebServerResourceDescr(const char *pResId, const char *pMimeType,
            const char *pContentEncoding,
            const unsigned char *pData, int dataLen)
    {
        _pResId    = pResId;
        _pMimeType = pMimeType;
        _pContentEncoding = pContentEncoding;
        _pData     = pData;
        _dataLen   = dataLen;
    }
    const char          *_pResId;
    const char          *_pMimeType;
    const char          *_pContentEncoding;
    const unsigned char *_pData;
    int                 _dataLen;
};
