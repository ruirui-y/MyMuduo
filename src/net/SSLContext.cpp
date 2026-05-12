#include "net/SSLContext.h"
#include "Log/Logger.h"
#include <mutex>

// 保证 OpenSSL 算法库在多线程环境下只初始化一次
static void InitOpenSSL() {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
}

SSLContext::SSLContext()
{
    static std::once_flag flag;
    std::call_once(flag, InitOpenSSL);

    // 使用现代的通用 TLS_method()，同时兼容 Client 和 Server
    const SSL_METHOD* method = TLS_method();
    ctx_ = SSL_CTX_new(method);
    if (!ctx_)
    {
        LOG_FATAL << "SSLContext 创建失败！";
        ERR_print_errors_fp(stderr);
    }

    // 1. SSL_MODE_ENABLE_PARTIAL_WRITE: 允许写多少算多少（返回部分写入的字节数）
    // 2. SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER: 允许重试时，使用不同地址的指针（因为数据被转移到了 Buffer 里）
    SSL_CTX_set_mode(ctx_, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    // 默认关闭证书真伪验证（主要为了作为客户端连币安时快速调通，后续可提供 API 开启）
    SSL_CTX_set_verify(ctx_, SSL_VERIFY_NONE, nullptr);

    LOG_INFO << "SSLContext 初始化成功，加密引擎已就绪。";
}

SSLContext::~SSLContext()
{
    if (ctx_) {
        SSL_CTX_free(ctx_);
    }
}

void SSLContext::LoadCertificate(const std::string& cert_path, const std::string& key_path)
{
    // 1. 加载公钥证书
    if (SSL_CTX_use_certificate_file(ctx_, cert_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
        LOG_FATAL << "加载证书失败: " << cert_path;
    }

    // 2. 加载私钥
    if (SSL_CTX_use_PrivateKey_file(ctx_, key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
        LOG_FATAL << "加载私钥失败: " << key_path;
    }

    // 3. 验证私钥和证书是否配对
    if (!SSL_CTX_check_private_key(ctx_)) {
        LOG_FATAL << "致命错误：私钥与证书不匹配！";
    }

    LOG_INFO << "SSL 证书与私钥加载、校验成功！";
}