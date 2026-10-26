#include <sfr_config.h>
#include <windows.h>
#ifndef _WINCRYPT_H_
#define _WINCRYPT_H_

/* wincrypt.h —— CryptoAPI 的最小可用子集。
 *
 * Windows 的 <windows.h> 会无条件包含本头（SDK 里就是这么做的），所以这里
 * 必须能被 windows.h 安全地拉进来：上面两行 include 是 swinx 各头文件的统一
 * 写法，靠 include guard 保证不会递归。
 *
 * swinx 没有密钥容器、证书库这些系统设施，能提供的是"密码学质量的随机数"
 * 这一件事 —— 它是 WebKit 里唯一真正被用到的能力（生成临时文件名、Guid）。
 * 需要密钥容器（szContainer 非 NULL 且未带 CRYPT_VERIFYCONTEXT）的调用会被
 * 明确拒绝，而不是返回一个假句柄让调用方以为拿到了密钥存储。
 */

typedef ULONG_PTR HCRYPTPROV;
typedef ULONG_PTR HCRYPTKEY;
typedef ULONG_PTR HCRYPTHASH;

/* 提供者类型。只有 PROV_RSA_FULL 在 swinx 上有意义 —— 它对应"任意来源的
 * 强随机数"，其余类型都涉及真实的密钥算法实现。 */
#define PROV_RSA_FULL      1
#define PROV_RSA_SIG       2
#define PROV_DSS           3
#define PROV_DSS_DH        13
#define PROV_RSA_SCHANNEL  12
#define PROV_DH_SCHANNEL   18
#define PROV_EC_ECDSA_SIG  14
#define PROV_EC_ECNRA_SIG  15

/* CryptAcquireContext 的 dwFlags */
#define CRYPT_VERIFYCONTEXT  0xF0000000
#define CRYPT_NEWKEYSET      0x00000008
#define CRYPT_DELETEKEYSET   0x00000010
#define CRYPT_MACHINE_KEYSET 0x00000020
#define CRYPT_SILENT         0x00000040

#define MS_DEF_PROV_A "Microsoft Base Cryptographic Provider v1.0"
#define MS_DEF_PROV_W L"Microsoft Base Cryptographic Provider v1.0"
#ifdef UNICODE
#define MS_DEF_PROV MS_DEF_PROV_W
#else
#define MS_DEF_PROV MS_DEF_PROV_A
#endif

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

    BOOL WINAPI CryptAcquireContextA(HCRYPTPROV *phProv, LPCSTR szContainer, LPCSTR szProvider, DWORD dwProvType, DWORD dwFlags);
    BOOL WINAPI CryptAcquireContextW(HCRYPTPROV *phProv, LPCWSTR szContainer, LPCWSTR szProvider, DWORD dwProvType, DWORD dwFlags);

    BOOL WINAPI CryptGenRandom(HCRYPTPROV hProv, DWORD dwLen, BYTE *pbBuffer);

    BOOL WINAPI CryptReleaseContext(HCRYPTPROV hProv, DWORD dwFlags);

    /* ---- 证书上下文 ----
     * swinx 没有证书库，也就不存在证书上下文的来源；这里只提供类型与
     * "复制 / 释放"这对所有权函数，让 WebCore 的 CF 桥接代码能编译并通过
     * 所有权检查。真正的证书由平台 TLS 库（SecureTransport / OpenSSL）持有，
     * 不经过这一层。 */
    typedef void *HCERTSTORE;
    typedef void *PCERT_INFO;

    typedef struct _CERT_CONTEXT
    {
        DWORD dwCertEncodingType;
        BYTE *pbCertEncoded;
        DWORD cbCertEncoded;
        PCERT_INFO pCertInfo;
        HCERTSTORE hCertStore;
    } CERT_CONTEXT, *PCERT_CONTEXT;

    typedef const CERT_CONTEXT *PCCERT_CONTEXT;

    PCCERT_CONTEXT WINAPI CertDuplicateCertificateContext(PCCERT_CONTEXT pCertContext);
    BOOL WINAPI CertFreeCertificateContext(PCCERT_CONTEXT pCertContext);

#ifdef __cplusplus
}
#endif //__cplusplus

#ifdef UNICODE
#define CryptAcquireContext CryptAcquireContextW
#else
#define CryptAcquireContext CryptAcquireContextA
#endif

#endif //_WINCRYPT_H_
