#include <wintrust.h>
#include <wincrypt.h>
#ifndef WSS_GET_SECONDARY_SIG_COUNT
typedef struct {
    DWORD cbStruct;
    DWORD dwIndex;
    DWORD dwFlags;
    DWORD cSecondarySigs;
    DWORD dwVerifiedSigIndex;
    PCERT_STRONG_SIGN_PARA pCryptoPolicy;
} WINTRUST_SIGNATURE_SETTINGS;
struct AfgWinTrustData : WINTRUST_DATA {
    WINTRUST_SIGNATURE_SETTINGS *pSignatureSettings;
};
#define WSS_VERIFY_SPECIFIC 1
#define WSS_GET_SECONDARY_SIG_COUNT 2
#define WINTRUST_DATA AfgWinTrustData
#include "sl_security.h"
#undef WINTRUST_DATA
#else
#include "sl_security.h"
#endif
