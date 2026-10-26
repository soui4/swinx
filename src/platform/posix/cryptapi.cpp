#include <windows.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <mutex>
#include <set>

#include "debug.h"
#define kLogTag "crypt"

/* CryptoAPI 的 POSIX 实现。
 *
 * swinx 不实现密钥容器与证书库，只实现"取密码学质量随机数"这一条主力路径。
 * 提供者句柄因此只是一个登记在表里的令牌：CryptAcquireContext 发放、
 * CryptReleaseContext 回收、CryptGenRandom 校验。这样做而不是"永远返回同一个
 * 常量"，是为了让"用一个已经释放的句柄去取随机数"能像 Windows 一样失败，
 * 而不是静默地继续工作。
 */

static std::mutex s_providerMutex;
static std::set<HCRYPTPROV> s_providers;
static HCRYPTPROV s_nextProvider = 0x57494E58; // 'WINX'，避免与小整数撞车

static bool isProviderTypeSupported(DWORD provType)
{
    // 只有 PROV_RSA_FULL 被映射成"系统熵源"。其余类型要求真正的密钥算法
    // 实现（签名、信道加密），swinx 没有，交给平台 TLS 库去做。
    return provType == PROV_RSA_FULL || provType == PROV_RSA_SIG;
}

static BOOL acquireContext(HCRYPTPROV *phProv, bool hasContainer, DWORD dwProvType, DWORD dwFlags)
{
    if (!phProv)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!isProviderTypeSupported(dwProvType))
    {
        SetLastError(NTE_BAD_PROV_TYPE);
        return FALSE;
    }

    // CRYPT_VERIFYCONTEXT 的含义是"我不要密钥容器，只借提供者做运算"。
    // 没有它就意味着调用方要一个持久化的密钥集合，而 swinx 无从存放。
    if (hasContainer && !(dwFlags & CRYPT_VERIFYCONTEXT))
    {
        SetLastError(NTE_BAD_KEYSET);
        return FALSE;
    }

    if (dwFlags & CRYPT_DELETEKEYSET)
    {
        // 没有密钥集可删；Windows 上这是成功操作
        *phProv = 0;
        return TRUE;
    }

    std::lock_guard<std::mutex> lock(s_providerMutex);
    HCRYPTPROV h = s_nextProvider++;
    s_providers.insert(h);
    *phProv = h;
    return TRUE;
}

BOOL WINAPI CryptAcquireContextA(HCRYPTPROV *phProv, LPCSTR szContainer, LPCSTR szProvider, DWORD dwProvType, DWORD dwFlags)
{
    // szProvider 是"用哪个 CSP"，POSIX 上只有系统熵源一种，无需区分
    (void)szProvider;
    return acquireContext(phProv, szContainer != NULL, dwProvType, dwFlags);
}

BOOL WINAPI CryptAcquireContextW(HCRYPTPROV *phProv, LPCWSTR szContainer, LPCWSTR szProvider, DWORD dwProvType, DWORD dwFlags)
{
    (void)szProvider;
    return acquireContext(phProv, szContainer != NULL, dwProvType, dwFlags);
}

static bool fillFromSystemEntropy(BYTE *buffer, DWORD length)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
    {
        SLOG_FMTE("cannot open /dev/urandom: %d", errno);
        return false;
    }

    DWORD done = 0;
    while (done < length)
    {
        // read() 一次最多返回 32MB 上下，而且可能被信号打断，所以必须循环
        ssize_t n = read(fd, buffer + done, length - done);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            SLOG_FMTE("read(/dev/urandom) failed: %d", errno);
            close(fd);
            return false;
        }
        if (n == 0)
        {
            SLOG_FMTE("read(/dev/urandom) returned 0 bytes");
            close(fd);
            return false;
        }
        done += (DWORD)n;
    }

    close(fd);
    return true;
}

BOOL WINAPI CryptGenRandom(HCRYPTPROV hProv, DWORD dwLen, BYTE *pbBuffer)
{
    if (!pbBuffer && dwLen)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    {
        std::lock_guard<std::mutex> lock(s_providerMutex);
        if (s_providers.find(hProv) == s_providers.end())
        {
            SetLastError(NTE_BAD_KEYSET);
            return FALSE;
        }
    }

    // 空请求在 Windows 上是成功的空操作
    if (dwLen == 0)
        return TRUE;

    if (!fillFromSystemEntropy(pbBuffer, dwLen))
    {
        SetLastError(NTE_FAIL);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI CryptReleaseContext(HCRYPTPROV hProv, DWORD dwFlags)
{
    (void)dwFlags;
    std::lock_guard<std::mutex> lock(s_providerMutex);
    if (s_providers.erase(hProv) == 0)
    {
        SetLastError(NTE_BAD_KEYSET);
        return FALSE;
    }
    return TRUE;
}

/* ---- 证书上下文 ----
 * 只有 CertDuplicateCertificateContext 产出的指针才会被 CertFreeCertificateContext
 * 释放：其它来源（平台 TLS 库、调用方自建）的指针一律原样返回 TRUE 而不动它。
 * 宁可漏一次 free，也不能去 free 一块不是自己分配的内存。 */
static std::mutex s_certMutex;
static std::set<PCCERT_CONTEXT> s_ownedCertContexts;

PCCERT_CONTEXT WINAPI CertDuplicateCertificateContext(PCCERT_CONTEXT pCertContext)
{
    if (!pCertContext)
        return NULL;

    CERT_CONTEXT *copy = (CERT_CONTEXT *)malloc(sizeof(CERT_CONTEXT));
    if (!copy)
        return NULL;
    memcpy(copy, pCertContext, sizeof(CERT_CONTEXT));

    {
        std::lock_guard<std::mutex> lock(s_certMutex);
        s_ownedCertContexts.insert(copy);
    }
    return copy;
}

BOOL WINAPI CertFreeCertificateContext(PCCERT_CONTEXT pCertContext)
{
    // Windows：NULL 是合法入参，返回 TRUE
    if (!pCertContext)
        return TRUE;

    bool owned = false;
    {
        std::lock_guard<std::mutex> lock(s_certMutex);
        owned = s_ownedCertContexts.erase(pCertContext) != 0;
    }
    if (!owned)
    {
        SLOG_FMTW("CertFreeCertificateContext: ignoring a context swinx did not create");
        return TRUE;
    }

    free((void *)pCertContext);
    return TRUE;
}
