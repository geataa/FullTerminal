#pragma once
//
// KeyGenService: SSH anahtar cifti uretim servisi.
// OpenSSH ssh-keygen.exe kullanarak Ed25519 veya RSA anahtarlar uretir.
//
#include <string>

namespace ft {

enum class KeyAlgorithm {
    Ed25519,
    Rsa4096,
    Ecdsa256
};

struct KeyGenResult {
    bool success = false;
    std::wstring privateKeyPath;
    std::wstring publicKeyPath;
    std::wstring error;
};

class KeyGenService {
public:
    static KeyGenResult GenerateKey(
        KeyAlgorithm algo,
        const std::wstring& keyName,
        const std::wstring& passphrase = L"",
        const std::wstring& comment = L"",
        const std::wstring& targetDir = L"");

    static std::wstring FindSshKeygenExe();
};

} // namespace ft
