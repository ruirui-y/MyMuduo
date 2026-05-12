#ifndef SSL_CONTEXT_H
#define SSL_CONTEXT_H

#include "../noncopyable.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <string>

class SSLContext : noncopyable
{
public:
    SSLContext();
    ~SSLContext();

    // 服务端专用：加载公钥证书和私钥
    void LoadCertificate(const std::string& cert_path, const std::string& key_path);

    // 获取底层的 SSL_CTX 指针，给 TcpConnection 绑定 fd 用
    SSL_CTX* GetContext() const { return ctx_; }

private:
    SSL_CTX* ctx_;
};

#endif