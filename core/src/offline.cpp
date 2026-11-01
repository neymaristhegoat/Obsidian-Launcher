#include "launcher/offline.h"
#include <openssl/evp.h>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace launcher::offline {

bool valid_username(const std::string& name) {
    if (name.size() < 3 || name.size() > 16) return false;
    for (unsigned char c : name)
        if (!(std::isalnum(c) || c == '_')) return false;
    return true;
}

std::string offline_uuid(const std::string& name) {
    const std::string input = "OfflinePlayer:" + name;
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_md5(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx.get(), input.data(), input.size()) != 1 ||
        EVP_DigestFinal_ex(ctx.get(), md, &len) != 1 || len != 16)
        throw std::runtime_error("MD5 failed");

    md[6] = (md[6] & 0x0f) | 0x30;   // version 3
    md[8] = (md[8] & 0x3f) | 0x80;   // IETF variant

    char out[33];
    for (int i = 0; i < 16; ++i) std::snprintf(out + i * 2, 3, "%02x", md[i]);
    return std::string(out, 32);
}

}